#include "comm.h"
#include "dsp/dsp.h"
#include "state.h"
#include "spce061_bridge.h"
#include <SDL2/SDL.h>
#include <cstdlib>

extern nc2k_states_t nc2k_states;

static SDL_AudioDeviceID g_audio_device = 0;
static FILE *audio_dump_fp; //for dump data

/* SPCE061A stream diagnostics (the queue itself lives further down) */
uint32_t dsp061_overruns = 0;
uint32_t dsp061_starves = 0;   /* resampler needed a sample but the queue was empty */
uint32_t dsp061_consumed = 0;
uint32_t dsp061_pushed = 0;
int dsp061_drain_disabled = 0;   /* set by the headless tool when it taps the raw stream */
/*
=============
beeper
=============
*/
struct BeeperSignal{
    long long cycle;
    int value;
};

static BeeperSignal last_beeper{};
static deque<signed short> sound_stream_beeper;
/*buffer to SDL_QueueAudio */
//static vector<signed short> beeper_buffer; 

/*filter out DC signal*/
double filter_beeper(double in){
	static double cuttmp=-8000;
	static double cutoff=2.0*3.141592654*40/BEEPER_AUDIO_HZ;
	double val=in-cuttmp;
	cuttmp+=cutoff*val;
	return val;
}
/*
double filter_dsp(double in){
	static double cuttmp=0;
	static double cutoff=2.0*3.141592654*40/DSP_AUDIO_HZ;
	double val=in-cuttmp;
	cuttmp+=cutoff*val;
	return val;
}*/
/*
==============
dsp
==============
*/

Dsp dsp; //make it non-static for dsp_test

static deque<signed short> sound_stream_dsp_wqx;
static deque<signed short> sound_stream_dsp_host;
/*
static long long last_audio_queue_check_time=0;
static long long last_audio_queue_increase_time=0;
static int target_audio_queue_size_shrink_thres=2000;
static int target_audio_queue_size=10000;
static int target_audio_queue_size_min=5000;
static int target_audio_queue_size_max=20000;
static int min_audio_queue_size_observed=int_inf;*/


void manipulate_beeper(int a){
    long long current_cycle=nc2k_states.cycles;
	//note: (BEEPER_AUDIO_HZ+20) is to make it a bit larger, so that queue will not drain because of clock mismatch
    int CYCLES_SECOND_adjusted= CYCLES_SECOND;
    /*if(fast_forward && fast_forward_limit!=0){
        CYCLES_SECOND_adjusted= CYCLES_SECOND*fast_forward_limit;
    }else if(speed_multiplier!=1.0){
        CYCLES_SECOND_adjusted= CYCLES_SECOND*speed_multiplier;
    }*/
    CYCLES_SECOND_adjusted= int(CYCLES_SECOND*speed_multiplier);
    long long samples_start=last_beeper.cycle*(BEEPER_AUDIO_HZ+20)/(CYCLES_SECOND_adjusted);
    long long samples_end=current_cycle*(BEEPER_AUDIO_HZ+20)/(CYCLES_SECOND_adjusted);
    //printf("%lld, %d  %lld %lld\n",current_cycle -last_beeper.cycle, nc1020_states.cycles, samples_start,samples_end);
    last_beeper.cycle=current_cycle;

    if (g_audio_device) SDL_LockAudioDevice(g_audio_device);
    for(int i=0;i<(samples_end-samples_start);i++){
        if(sound_stream_beeper.size() > 4096) {//avoid beeper queue too large. 4096 samples =~ 90ms
            break;
        }
        sound_stream_beeper.push_back(8000*last_beeper.value);
    }
    if (g_audio_device) SDL_UnlockAudioDevice(g_audio_device);

    last_beeper.value=a;
}

void beeper_on_io_write(int a){
    if (a!=last_beeper.value){
        long long current_cycle=nc2k_states.cycles;
        if (getenv("NC3_BEEP_TRACE")) {
            static long long prev = 0;
            if (prev) printf("[beep] toggle @%lld cycles (Δ=%lld, %.0f Hz)\n",
                             current_cycle, current_cycle - prev,
                             current_cycle > prev ? (double)CYCLES_SECOND / (double)(current_cycle - prev) : 0.0);
            prev = current_cycle;
            fflush(stdout);
        }
        //printf("%lld %lld, %d!!!!!!!!!!!\n",current_cycle, last_beeper.cycle, a);
    }
    manipulate_beeper(a);
}

void reset_dsp(){
    dsp.reset();
}
void write_data_to_dsp(uint8_t high,uint8_t low){
    dsp.write(high,low);
}

static void drain_dsp061_audio();

/* ---- optional WAV capture of the mixed output (headless verification) ---- */
static FILE *g_wav_fp = NULL;
static uint32_t g_wav_samples = 0;

static void wav_begin(const char *path) {
    g_wav_fp = fopen(path, "wb");
    if (!g_wav_fp) { printf("cannot write %s\n", path); return; }
    uint8_t hdr[44] = {0};
    memcpy(hdr, "RIFF", 4); memcpy(hdr + 8, "WAVEfmt ", 8);
    uint32_t v;
    v = 16; memcpy(hdr + 16, &v, 4);
    uint16_t w;
    w = 1;  memcpy(hdr + 20, &w, 2);   /* PCM */
    w = 1;  memcpy(hdr + 22, &w, 2);   /* mono */
    v = BEEPER_AUDIO_HZ; memcpy(hdr + 24, &v, 4);
    v = BEEPER_AUDIO_HZ * 2; memcpy(hdr + 28, &v, 4);
    w = 2;  memcpy(hdr + 32, &w, 2);
    w = 16; memcpy(hdr + 34, &w, 2);
    memcpy(hdr + 36, "data", 4);
    fwrite(hdr, 1, 44, g_wav_fp);
}

static void wav_write(const int16_t *p, int n) {
    if (!g_wav_fp) return;
    fwrite(p, sizeof(int16_t), n, g_wav_fp);
    g_wav_samples += n;
}

static void wav_end(void) {
    if (!g_wav_fp) return;
    uint32_t v;
    v = 36 + g_wav_samples * 2; fseek(g_wav_fp, 4, SEEK_SET);  fwrite(&v, 4, 1, g_wav_fp);
    v = g_wav_samples * 2;      fseek(g_wav_fp, 40, SEEK_SET); fwrite(&v, 4, 1, g_wav_fp);
    fclose(g_wav_fp);
    g_wav_fp = NULL;
    printf("[audio] wrote %u samples @ %d Hz  (061: consumed=%u starves=%u overruns=%u)\n",
           g_wav_samples, (int)BEEPER_AUDIO_HZ, dsp061_consumed, dsp061_starves,
           dsp061_overruns);
}

void post_cpu_run_sound_handling(){
    manipulate_beeper(last_beeper.value);
    if(nc3000mode) drain_dsp061_audio();
}

/*
 * The emulated SPCE061A produces signed 16 bit samples at whatever rate its
 * firmware programmed into TimerA (~32 kHz for A1600/S200, ~36.8 kHz for the
 * S600 word decoder).  Pull them out of the chip's ring into our own queue;
 * audio_mix_cb resamples from there to the 44.1 kHz output device.
 */
static deque<signed short> sound_stream_dsp061;
/*
 * 上限：24000（~0.38 s）原来太紧 —— 模拟器一次"追赶"最多会瞬产 300 ms 的音频
 * （≈19000 样），加上队列里已有的量就顶格丢样了（见 main.cpp 里的音频背压注释）。
 * 现在 main.cpp 会做背压，正常不会到这儿；这里放宽到 32000（0.5 s）当最后一道保险。
 */
static const size_t dsp061_max_queue = 32000;

/* 调试用：给别的模块看这一级队列有多长（见 spce061_bridge.cpp 的 500ms 日志） */
int dsp061_queue_len(void) { return (int)sound_stream_dsp061.size(); }

/*
 * 主控/061 停止播放时调用：把"已经解码、但还没播出去"的那一小段**淡出丢掉**。
 *
 * 为什么需要它：061 解码是自由跑的，队列里总会领先播放头一小段（现在压到 ~10-20ms）。
 * 一旦停止，TimerA 会从 63.8 kHz 改回 8 kHz，混音器的重采样比也跟着变 8 倍 ——
 * 队列里那点残留就会被"慢放"成一段怪声，截断处还有爆音（用户 2026-09-27 反馈
 * "退出游戏后有简短噪音"）。真机 DAC 的 FIFO 只有几个样本，不会出现这种现象。
 * 这里用一小段（约 1.5 ms）的线性淡出代替硬切，听感上就是干净地停住。
 */
void dsp061_flush_queue(void) {
    if (sound_stream_dsp061.empty()) return;
    int16_t last = sound_stream_dsp061.back();
    sound_stream_dsp061.clear();
    const int n = 96;
    for (int i = 0; i < n; i++)
        sound_stream_dsp061.push_back((int16_t)((int32_t)last * (n - i) / n));
}

static void drain_dsp061_audio(){
    signed short tmp[128];
    extern int dsp061_drain_disabled;
    if (dsp061_drain_disabled) return;      /* someone else is taping the raw stream */
    for(int guard=0; guard<8; guard++){
        int got = nc3_dsp_audio(tmp, 128);
        if(got<=0) break;
        if (g_audio_device) SDL_LockAudioDevice(g_audio_device);
        for(int i=0;i<got;i++){
            if(sound_stream_dsp061.size() >= dsp061_max_queue) { dsp061_overruns++; continue; }
            sound_stream_dsp061.push_back(tmp[i]);
            dsp061_pushed++;
        }
        if (g_audio_device) SDL_UnlockAudioDevice(g_audio_device);
        if(got<128) break;
    }
}

/*
void init_audio_dump_file(){
     audio_dump_fp=fopen("./audio1.dump","wb");
	 assert(audio_dump_fp!=0);
}
void close_audio_dump_file(){
     fclose(audio_dump_fp);
}
void write_audio_dump_file(unsigned char *p, int size){
    fwrite(p,size,1,audio_dump_fp);
}*/

const int dsp_busy_len_wqx = 5000; /* unit: samples */
const int dsp_drop_len_wqx = 10000;
const int dsp_drop_len_host = 10000;


// Linear resampler state for DSP -> output
static double g_dsp_phase = 0.0;  // in [0,1)
static float  g_dsp_s0 = 0.0f;    // last DSP sample
static float  g_dsp_s1 = 0.0f;    // next DSP sample (lookahead)

// Linear resampler state for the SPCE061A stream (rate set by its firmware)
static double g_d061_phase = 0.0;
static float  g_d061_s0 = 0.0f;
static float  g_d061_s1 = 0.0f;
static double g_d061_ratio = 31958.0 / 44100.0;

static const double g_dsp_ratio = (double)DSP_AUDIO_HZ / (double)BEEPER_AUDIO_HZ;

static inline Sint16 clamp_s16(int x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return (Sint16)x;
}

void dsp_call_back(unsigned char* p, int len) {
	if (enable_debug_dsp) {
        static int cnt = 0;
        cnt++;
        if (cnt % 1000 == 0) {
            std::printf("dsp fifo_len_wqx=%d, fifo_len_host=%d\n", (int)sound_stream_dsp_wqx.size(), (int)sound_stream_dsp_host.size());
        }
        if(sound_stream_dsp_host.size() ==0) {
            printf("audio queue drain!!! fifo_len_wqx=%d \n", (int)sound_stream_dsp_wqx.size());
        }
    }

    const Sint16* in = (const Sint16*)p;
    const int samples = len / 2;

    for (int i = 0; i < samples; ++i) {
        //if this is too small, then dic's repeat prounce will be cut off
        //if this is too large, then sound will not be stopped immediately
        //(if wqx program doesn't respect dsp busy, then this is last resort to stop queueing)
        if (sound_stream_dsp_wqx.size() >= dsp_drop_len_wqx) {
            if (enable_debug_dsp) {
                std::printf("audio queue dropping (dsp fifo = %d)\n", (int)sound_stream_dsp_wqx.size());
            }
            break;
        }
        sound_stream_dsp_wqx.push_back(in[i]);
    }
}

//this function simulates the consumption speed of wqx dsp
//so that wqx will not feel the glitch of host's sound card processing speed
void dsp_move(int len /*unit sample*/){
    if (g_audio_device) SDL_LockAudioDevice(g_audio_device);
    for(int i=0;i<len;i++){
        if(sound_stream_dsp_wqx.empty()) break;
        if(sound_stream_dsp_host.size() < dsp_drop_len_host) {
            sound_stream_dsp_host.push_back(sound_stream_dsp_wqx.front());
        }else {
            //cannot break here, need to pop sound_stream_dsp_wqx
        }
        sound_stream_dsp_wqx.pop_front();
    }
    if (g_audio_device) SDL_UnlockAudioDevice(g_audio_device);
}

// this value is tricky:
// if too small sdl will pop because queue too small
// if too large, then too many queued and sound cannot be stopped immediately.
// (some wqx program respect dsp busy, some doesn't)
bool sound_busy() {
	const int fifo_len = (int)sound_stream_dsp_wqx.size();
    return fifo_len > dsp_busy_len_wqx;
}

// Single audio mixing callback: pulls beeper (44100 Hz) and DSP (8 kHz), resamples DSP, mixes, clamps.
static void mix_block(Sint16* out, int frames) {

    // Clear output
    SDL_memset(out, 0, frames * (int)sizeof(Sint16));
    static int last_beeper_sample=0;

    if(nc3000mode){
        uint32_t r = nc3_dsp_sample_rate();
        /*
         * 061 的采样率由它自己的 TimerA 决定，会随时变（例如单词发音 36.8 kHz、
         * 音乐 63.8 kHz、空闲 8 kHz），所以这里必须**跟着它走**。
         *
         * ⚠️ 2026-09-27 教训：曾经写成"队列非空就不更新比例"（想让停止时残留的一小段
         * 按旧速率播完），结果单词发音一开始队列里就有样本 ⇒ 比例一直停在空闲的 8 kHz，
         * 发音被**慢了 4.6 倍**（用户立刻听出来了）。停止时的残留改用
         * dsp061_flush_queue()（解码门落下时淡出清掉）就够了，不要锁比例。
         */
        if(r) g_d061_ratio = (double)r / (double)BEEPER_AUDIO_HZ;
    }
    if (nc3000mode && getenv("NC3_SND_DEBUG")) {
        /*
         * 音频队列曲线。重点看 qmax（两次打印之间队列的峰值）和 overruns：
         *   dsp061_max_queue = 24000 样本 ≈ 0.38 s，一旦顶到就**丢新样**，
         *   听感就是"音乐往前跳一下 / 快了几秒"（用户 2026-09-27 报的现象）。
         */
        static int blk = 0;
        static unsigned qmax = 0, pushed_last = 0, consumed_last = 0, ovr_last = 0;
        unsigned q = (unsigned)sound_stream_dsp061.size();
        if (q > qmax) qmax = q;
        if ((blk++ % 25) == 0) {
            printf("[mix] blk=%d ratio=%.3f q=%u qmax=%u push+%u cons+%u starv+%u ovr+%u\n",
                   blk, g_d061_ratio, q, qmax,
                   dsp061_pushed - pushed_last, dsp061_consumed - consumed_last,
                   (unsigned)dsp061_starves, dsp061_overruns - ovr_last);
            fflush(stdout);
            qmax = 0;
            pushed_last = dsp061_pushed; consumed_last = dsp061_consumed;
            ovr_last = dsp061_overruns;
        }
    }

    for (int i = 0; i < frames; ++i) {
        // 1) Beeper @ output rate (44100 Hz)
        int beeper_sample = 0;
		if(enable_beeper && !getenv("NC3_NO_BEEPER")){   /* NC3_NO_BEEPER=1 关掉蜂鸣器，用于分离噪音来源 */
			if (!sound_stream_beeper.empty()) {
				beeper_sample = sound_stream_beeper.front();
				sound_stream_beeper.pop_front();
				last_beeper_sample=beeper_sample;
			}else{
				beeper_sample=last_beeper_sample;
			}
			beeper_sample=filter_beeper(beeper_sample);
		}

        // 2) DSP resample 8000 -> 44100 using linear interpolation with phase in [0,1)
        // Advance fractional position
        double dsp_ratio_adjusted=g_dsp_ratio;
        /*if(fast_forward && fast_forward_limit!=0){
            dsp_ratio_adjusted= dsp_ratio_adjusted*fast_forward_limit;
        }else if(speed_multiplier!=1.0){
            dsp_ratio_adjusted= dsp_ratio_adjusted*speed_multiplier;
        }*/
        g_dsp_phase += dsp_ratio_adjusted;
        // When phase crosses 1.0, we step to next DSP sample(s)
        while (g_dsp_phase >= 1.0) {
            g_dsp_phase -= 1.0;
            // shift look-back/forward window
            g_dsp_s0 = g_dsp_s1;
            if (!sound_stream_dsp_host.empty()) {
                g_dsp_s1 = (float)sound_stream_dsp_host.front();
                sound_stream_dsp_host.pop_front();
            } else {
                // If no new DSP sample
                g_dsp_s1 = 0;
            }
        }
        // Interpolate between s0 and s1
        float dsp_f = g_dsp_s0 + (g_dsp_s1 - g_dsp_s0) * (float)g_dsp_phase;

        //dsp_f=filter2(dsp_f);

        // 3) SPCE061A (NC3000) resampled the same way
        int d061_sample = 0;
        if (nc3000mode) {
            g_d061_phase += g_d061_ratio;
            while (g_d061_phase >= 1.0) {
                g_d061_phase -= 1.0;
                g_d061_s0 = g_d061_s1;
                if (!sound_stream_dsp061.empty()) {
                    g_d061_s1 = (float)sound_stream_dsp061.front();
                    sound_stream_dsp061.pop_front();
                    dsp061_consumed++;
                } else {
                    g_d061_s1 = 0;
                    dsp061_starves++;
                }
            }
            d061_sample = (int)(g_d061_s0 + (g_d061_s1 - g_d061_s0) * (float)g_d061_phase);
        }

        // 4) Mix and clamp (you can add per-stream gains here if needed)
        int mixed = beeper_sample + (int)dsp_f + d061_sample;
        out[i] = clamp_s16(mixed);
    }
}

static void audio_mix_cb(void* userdata, Uint8* stream, int len_bytes) {
    (void)userdata;
    Sint16* out = (Sint16*)stream;
    const int frames = len_bytes / (int)sizeof(Sint16);
    mix_block(out, frames);
    if(g_wav_fp) wav_write(out, frames);
}

/* ---- headless helpers: drive the mixer without an SDL audio device ------- */
void dump_audio_begin(){
    if(!dump_audio_path.empty()) wav_begin(dump_audio_path.c_str());
}

void dump_audio_pump(int frames){
    static Sint16 buf[4096];
    while(frames > 0){
        int n = frames > 4096 ? 4096 : frames;
        mix_block(buf, n);
        if(g_wav_fp) wav_write(buf, n);
        frames -= n;
    }
}

void dump_audio_end(){
    wav_end();
}

// Initialize one device with a callback, no SDL_mixer required.
void init_audio() {
    dsp.callback = dsp_call_back;
    if(!dump_audio_path.empty()) wav_begin(dump_audio_path.c_str());

    SDL_AudioSpec desired_spec = {};
    desired_spec.freq = (int)BEEPER_AUDIO_HZ;   // Pick beeper rate to avoid resampling it
    desired_spec.format = AUDIO_S16LSB;
    desired_spec.channels = 1;                  // mono
    desired_spec.samples = 1024;
    desired_spec.callback = audio_mix_cb;
    desired_spec.userdata = nullptr;

    // Reset DSP resampler state
    g_dsp_phase = 0.0;
    g_dsp_s0 = 0.0f;
    g_dsp_s1 = 0.0f;

    if(g_audio_device){
        //if previous audio device is not correctly shutdhown.
        //only possible for emscripten version, because variables are kept across runs
        printf("re-used audio device from last run\n");
    } else {
        SDL_AudioSpec obtained_spec = {};
        g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired_spec, &obtained_spec, 0);
        /*
         * ⚠️ 一定要看 SDL **实际**开出来的参数：如果声卡不支持 44100，SDL 会自己
         * 改成别的采样率，而我们混音/蜂鸣器都是按 44100 算的 ⇒ 整体音调会偏
         * （用户 2026-09-27 反馈"蜂鸣器音调偏低"）。这里打出来，偏了就看得见。
         */
        if (g_audio_device)
            printf("[audio] device opened: freq=%d fmt=%04X ch=%d samples=%d (wanted %u)\n",
                   obtained_spec.freq, obtained_spec.format, obtained_spec.channels,
                   obtained_spec.samples, (unsigned)BEEPER_AUDIO_HZ);
        fflush(stdout);
    }
    if (!g_audio_device) {
        std::printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }

    SDL_PauseAudioDevice(g_audio_device, 0);
}

// Call on shutdown
void shutdown_audio() {
    wav_end();
    if (g_audio_device) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
}
