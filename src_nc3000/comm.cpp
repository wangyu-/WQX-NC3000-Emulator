#include "comm.h"
#include <cstdint>
#include <cstdlib>
#include <sys/types.h>

/*
===================
global switch
===================
*/
bool nc1020mode = false;
bool nc2000mode = false;
bool nc3000mode = false;
bool pc1000mode = false;
bool nc1020tw_mode =false;

CpuVersion cpu_version = CPU_HANDYPSP;
CpuLoopVersion cpu_loop_version = CPU_RUN3;
IoVersion io_version = IO_V2;

NorFormat nor_read_format = NorFormat::PHYSICAL_ORDER;
NorFormat nor_write_format = NorFormat::PHYSICAL_ORDER;

bool enable_load_state=false;
bool reset_after_load_state=false;
bool save_flash_on_exit=false;
bool save_state_on_exit=false;

bool sync_on_resume = true;

bool pro_key= false;

int headless_ms = 0;
int trace_interval_ms = 0;
string dump_lcd_path;
string dump_audio_path;
int nc3000_keymap_variant = 0;
int nc3000_key_trace = 0;
int nc3000_port0_trace = 0;
void (*headless_pc_cb)(uint16_t pc) = nullptr;
bool io_access_counting = false;
uint32_t io_access_count[2][0x40];
void (*headless_io_cb)(int is_write, int addr, int value) = nullptr;

/* NC3000 interrupt bookkeeping (headless diagnostics) */
uint32_t nc3_timebase_fired = 0;   /* setIrqTimeBase() calls */
uint32_t nc3_irq_pending = 0;      /* set_irq_pending() calls */
uint32_t nc3_irq_taken = 0;        /* IRQs actually serviced by the CPU */
uint32_t nc3_timebase_gate_fail = 0; /* timebase tick but timeBaseEnable()==0 */
bool nand_blk_counting = false;
uint32_t nand_blk_read[4096];
uint32_t nand_blk_write[4096];
struct NandLogEnt nand_log[64];
int nand_log_n = 0;
uint64_t nand_log_from_cycle = 0;
uint32_t nand_watch_page = 0xFFFFFFFFu;
uint8_t nand_wlog[128][3];
uint16_t nand_wpc[128];
uint8_t nand_win_snap[0x8000];
bool nand_win_taken = false;
int nand_wlog_n = 0;

int log_on_key_press = 0;

bool log_all_dsp_io = false;
/*
===================
debug related
===================
*/

string inject_code;
u64_t tick=0;  //tick is mostly for debug

bool enable_dyn_debug=false;
int enable_dyn_debug_next_n=0;
bool enable_quit_after_debug_next_n=false;


bool enable_debug_nand=false;

bool enable_debug_switch=false;
bool enable_debug_pc=false;
bool enable_oops=false;
bool enable_inject=false;

bool wanna_inject=false;
bool injected=false;

bool enable_debug_beeper=false;
bool enable_debug_dsp=false;

bool enable_debug_timer=false;
bool enable_debug_cks = false;

int enable_key_debug_once=0;

int debug_level = 0;

bool enable_assert_for_wqx_software = false;

/*
===================
emulation parameter
===================
*/
uint32_t SLICE_INTERVAL= 1;  //unit ms
uint32_t power_save_interval=1200;
uint32_t cpu_batch=64;
uint32_t mi_clear_delay_value=1;
bool enable_keepon = true;
bool enable_auto_time_sync= true;

double timer01_speed_fix=1.0;

bool enable_emulate_cks = false;

bool forced_erase_before_write = true;

bool fast_forward=false;

double speed_multiplier=1.0;
double rtc_speed=1.0;
double fast_forward_limit=0;
/*
===================
cycles related
===================
*/

double oc_factor=1.0;

//uint32_t static_multipler;
uint32_t CYCLES_SECOND;
uint32_t UNKNOWN_TIMER_FREQ;
uint32_t TIMER0_FREQ;
uint32_t TIMER1_FREQ;
uint32_t TIMEBASE_FREQ;
uint32_t CYCLES_UNKNOWN_TIMER;
uint32_t CYCLES_TIMER0;
uint32_t CYCLES_TIMER1;
uint32_t CYCLES_TIMEBASE;
uint32_t CYCLES_TIMER1_SPEED_UP;
uint32_t CYCLES_NMI;
uint32_t CYCLES_MS;
/*
===================
rom related
===================
*/
uint32_t num_nor_pages;
uint32_t num_nand_pages;
uint32_t num_rom_pages;
uint32_t ROM_SIZE;
uint32_t NOR_SIZE;

string rom_path;
/*
===================
display related
===================
*/
int pixel_size=4;
int gap_size=1;
int lcd_scale=1;
int total_size;

bool enable_lcd_latency_effect = true;
uint32_t LCD_INNER_REFRESH_INTERVAL=8; //unit ms
uint32_t LCD_OUTER_REFRESH_INTERVAL=16;
string lcdstripe_suffix;

int lcd_effect_charge_a=1;
int lcd_effect_charge_b=6;
int lcd_effect_discharge_a=1;
int lcd_effect_discharge_b=8;

const double rgb_base=0.90;
double r_scale=rgb_base+0.02,g_scale=rgb_base+0.04,b_scale=rgb_base;
/*
===================
misc
===================
*/
bool shift_down =false;
bool ctrl_down =false;
int battery_level=11;
bool patch_nc1020tw_nor=false;

bool patch_table_experiment=false;

bool reload_pending=false;

WqxRom nc2k_rom;

void init_parameters(){
    /*
    ===================
    cycles related
    ===================
    */
    //static_multipler=1; //tmp fix for speed and crash

    // cpu cycles per second (cpu freq).
    /*
     * NC3000 主控主频 = **9.65 MHz**（2026-10-02 按真机录音实测改；原来写 14.7456 MHz）。
     *
     * 依据（细节见本机文档 docs/NC3000_通讯管理提示声音.md §4、改造计划 §29 —— 那两份文档不入库）：
     *   真机录音（系统菜单 → 通讯管理 → 提示声音 → 3.通讯失败）里两个音是
     *     4223 / 6509 Hz，低音时长 114.7–119.7 ms；
     *   同一段提示音在模拟器 14.7456 MHz 下是 6472 / 9926 Hz（正好等于固件里那两个
     *     半周期常数在该主频下的理论值，误差 1% 以内 ⇒ 不是"常数模拟错了"）；
     *   把主频降到 9.6 MHz 重跑同一段 → 4200 / 6470 Hz、低音 120.3 ms（差 0.3–0.6%）；
     *   录音整体比 9.6 MHz 那次高约 0.5% ⇒ **取 9.65 MHz**（安全起见仍留 NC3_MCLK 微调）。
     *   原理：蜂鸣器是固件软件延时循环翻转 IO 0x18 bit7 发的声，音高 = 主频/常数，
     *     所以"音高对上了"就能反过来把主频钉住。
     *   旁证：TC1KS 固件自检第 10 项「cpu频测试」的合格窗口 = 9.0–10.9 MHz。
     *
     * 旧结论（第 20 轮，已被上面这次实测取代）：按 SPDC1064 手册第 42/43 页
     *   TEST[1:0]=01 → PLL(14.7456M)、CKS=3 → CPU 直接用 OSC（固件开机设 CKS=3），
     *   加上"10.24 MHz 时蜂鸣器偏低"的定性听感，把默认值定成了 14.7456 MHz。
     *   现在有真机录音的定量对比，按实测改。
     *
     * 想 A/B 回旧值：NC3_MCLK=14745600（只覆盖 nc3000）；觉得 9.65 偏高/偏低就 NC3_MCLK=9600000 / 9700000。
     * 061 的晶振 49.152 MHz 与主控主频的比值是现算的（nc3_dsp_insns_for），
     *   所以改这里**不会**动发音/音乐的音高与时长。
     */
    CYCLES_SECOND = 3686400*(pc1000mode) + 5120*1000*(nc1020mode||nc2000mode)
                    + 9650000*nc3000mode;
    /*
     * NC3_MCLK=<Hz>：临时覆盖 NC3000 主控主频（只影响 nc3000 模式）。
     *
     * 为什么留这个开关：默认值 2026-10-02 已按真机录音从 14.7456 MHz 改成 9.65 MHz
     * （依据见上面那段注释），但真机录音目前只有一段"提示音"，还想再拿一段
     * "单次按键音"复核（按键音和提示音高音是同一个 BIOS 入口 $E012，音高必然一致）。
     *   - 想复核旧值：NC3_MCLK=14745600
     *   - 录到新证据要微调：这个开关也能当场 A/B（蜂鸣器音高 = 主频/常数，很敏感）
     *
     * 注意：061（语音 DSP）那边的指令率是它自己的晶振决定的（49.152 MHz，固定），
     * 主控↔061 的比例按 49152000/CYCLES_SECOND 自动折算，所以改这里**不会**动发音/音乐。
     */
    if (nc3000mode) {
        const char *mclk = getenv("NC3_MCLK");
        if (mclk && atoi(mclk) > 0) {
            CYCLES_SECOND = (uint32_t)atoi(mclk);
            printf("[mclk] NC3_MCLK=%u Hz (默认 9650000)\n", CYCLES_SECOND);
        }
    }
    CYCLES_SECOND *= oc_factor;
    CYCLES_MS = CYCLES_SECOND / 1000;
    printf("cycles per second is %d\n",CYCLES_SECOND);
    
    // below are not used in new cpu loop,
    // but they are kept for old cpu loop for compare
    UNKNOWN_TIMER_FREQ = 2;
    TIMER0_FREQ = 2; //not used now
    TIMER1_FREQ = 200;//not used now
    TIMEBASE_FREQ = 250;
    CYCLES_UNKNOWN_TIMER = CYCLES_SECOND / UNKNOWN_TIMER_FREQ;
    // cpu cycles per timer0 period (1/2 s).
    CYCLES_TIMER0 = CYCLES_SECOND / TIMER0_FREQ;
    // cpu cycles per timer1 period (1/256 s).
    CYCLES_TIMER1 = CYCLES_SECOND / TIMER1_FREQ;
    CYCLES_TIMEBASE = CYCLES_SECOND / TIMEBASE_FREQ;
    // speed up
    CYCLES_TIMER1_SPEED_UP = CYCLES_SECOND / TIMER1_FREQ / 20;
    // cpu cycles per ms (1/1000 s).
    CYCLES_NMI = CYCLES_SECOND / 2;


    /*
    ===================
    rom related
    ===================
    */
    num_nor_pages =0x10+uint32_t(nc1020mode&&nc1020tw_mode)*0x10+uint32_t(nc3000mode)*0x10;

    //this is the nand pages of 528byte each
    num_nand_pages = 0+ uint32_t(nc2000mode)*65536  + uint32_t(nc3000mode)*65536*2;

    //const uint32_t num_nor_pages =0x20;
    num_rom_pages =0x300;
    ROM_SIZE = 0x8000 * num_rom_pages;
    NOR_SIZE = 0x8000 * num_nor_pages;

    /*
    ===================
    display related
    ===================
    */
    total_size=pixel_size+gap_size;

}

void ProcessBinaryRev(uint8_t* dest, uint8_t* src, uint32_t size){
	uint32_t offset = 0;
    while (offset < size) {
        memcpy(dest + offset + 0x4000, src + offset, 0x4000);
        memcpy(dest + offset, src + offset + 0x4000, 0x4000);
        offset += 0x8000;
    }
}

void ProcessBinaryLinear(uint8_t* dest, uint8_t* src, uint32_t size){
	uint32_t offset = 0;
    while (offset < size) {
        memcpy(dest + offset , src + offset, 0x4000);
        memcpy(dest + offset + 0x4000, src + offset + 0x4000, 0x4000);
        offset += 0x8000;
    }
}

//use vector<char> here, because some platform's string has issue in storing '\0' in the middle
void read_file(string name,vector<char> &v){
    FILE *f = fopen(name.c_str(), "rb");
    if(f==0) {
        printf("open file %s fail!\n",name.c_str());
        exit(-1);
    }
    fseek(f, 0, SEEK_END);
    int fsize = ftell(f);
    fseek(f, 0, SEEK_SET);  /* same as rewind(f); */
    v.resize(fsize);
    fread(&v[0], fsize, 1, f);
    fclose(f);
}

int read_file_noexit(string name,vector<char> &v){
    FILE *f = fopen(name.c_str(), "rb");
    if(f==0) {
        printf("open file %s fail!\n",name.c_str());
        return -1;
    }
    fseek(f, 0, SEEK_END);
    int fsize = ftell(f);
    fseek(f, 0, SEEK_SET);  /* same as rewind(f); */
    v.resize(fsize);
    fread(&v[0], fsize, 1, f);
    fclose(f);
    return 0;
}
