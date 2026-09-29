#include <SDL2/SDL.h>
#include "comm.h"
#include "nc2000.h"
#include <SDL_events.h>
#include <SDL_keycode.h>
#include <cstring>
#include <iostream>
#include <map>
#include "sound.h"
#include "spce061_bridge.h"   /* nc3_dsp_sample_rate()：音频背压按采样率折算阈值 */
#include "misc/udp_server.h"
#include "key.h"
#include "key_new.h"
#include "settings.h"
#include "display.h"
#include "console.h"
#include "lcdstripe/lcdpainter.h"

#if defined(_WIN32)
#include <windows.h>
#endif

/*
 * 资源（液晶格栅纹理）路径解析：优先用当前工作目录下的 resource/，
 * 找不到再试 exe 所在目录下的 resource/。这样把 exe 连 resource 一起拷到别处
 * 也能跑（哪怕工作目录不是 exe 目录）。
 */
string resolve_resource_path(const string &name){
    string p = "resource/" + name;
    if(fileExists(p)) return p;
#if defined(_WIN32)
    char exe[MAX_PATH] = {0};
    if(GetModuleFileNameA(NULL, exe, MAX_PATH)){
        string dir = exe;
        size_t pos = dir.find_last_of("\\/");
        if(pos != string::npos){
            string alt = dir.substr(0, pos+1) + "resource\\" + name;
            if(fileExists(alt)) return alt;
        }
    }
#endif
    return p;
}

using namespace std;

SDL_Window* window;
SDL_Renderer* renderer;
MyLCDView*  lcdview;

//Initialize Resource, this function is not supposed to be called repeatedly, otherwise there will be resource leak.
// If you are trying to create an emscripten/android/ios version, init_resource() should be called only once,
// emu_entry() is the only function you need to re-call after switching rom or model. 
void init_resource() {
  #if defined(__MINGW32__)
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
  int res1=SDL_SetThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
  if(debug_level>=1) printf("SDL_SetThreadPriority returned %d\n", res1);

  if(listen_port>0) init_udp_server(listen_port);

  lcd_effect_buffer = new unsigned char[SCREEN_HEIGHT*total_size* SCREEN_WIDTH*total_size * 4];
  memset(lcd_effect_buffer, 0, SCREEN_HEIGHT*total_size* SCREEN_WIDTH*total_size * 4);

  if (SDL_Init(SDL_INIT_EVERYTHING) == -1) {
    std::cout << " Failed to initialize SDL : " << SDL_GetError() << std::endl;
    exit(-1);
  }
  init_audio();

  window =
    SDL_CreateWindow(get_str_of_mode().c_str(), 0, 40, lcd_scale * (SCREEN_WIDTH +LEFT_GAP +RIGHT_GAP-1) *total_size +(LEFT_GAP_EXTRA+RIGHT_GAP_EXTRA)*lcd_scale, lcd_scale * SCREEN_HEIGHT *total_size, 0);
  if (!window) {
    std::cout << "Failed to create window : " << SDL_GetError() << std::endl;
    exit(-1);
  }
  /* 把窗口提到最前：不然双击启动时它可能躲在别的窗口后面，用户按的键都收不到
   * （2026-09-27 用户实测：以为按了 N 进了"debug 命令行"，其实是按键没进模拟器窗口）。*/
  SDL_RaiseWindow(window);
  renderer = SDL_CreateRenderer(window, -1, 0);
  if (!renderer) {
    std::cout << "Failed to create renderer : " << SDL_GetError() << std::endl;
    exit(-1);
  }

  lcdview = new MyLCDView(resolve_resource_path("lcdstripe_slice_"+lcdstripe_suffix+".json").c_str());
  lcdview->loadStripeTexture(resolve_resource_path("lcdstripe_"+lcdstripe_suffix+".bmp").c_str(), renderer);
  
}

long long get_current_time_milliseconds() {
#if defined(_WIN32)
    /*
     * 不要用 clock_gettime()：MinGW 里它是 winpthread 提供的，编出来的 exe 会
     * 从 libwinpthread-1.dll 导入 clock_gettime64。用户机器上 PATH 里若是
     * Git/MSYS2 之类带的**旧版** libwinpthread-1.dll 先被加载，就会报
     * "无法定位程序输入点 clock_gettime64"。改用 kernel32 的
     * GetSystemTimeAsFileTime（FILETIME = 1601-01-01 起的 100ns 计数）。
     */
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)((t - 116444736000000000ULL) / 10000ULL);
#else
    struct timespec spec;
    if (clock_gettime(CLOCK_REALTIME, &spec) == -1) {
        // Handle error, e.g., print an error message and exit
        perror("clock_gettime");
        return -1; 
    }
    // Convert seconds and nanoseconds to milliseconds
    return (long long)spec.tv_sec * 1000 + (long long)spec.tv_nsec / 1000000; 
#endif
}

void main_loop() {
  bool loop = true;
  bool power_save= false;

  u64_t start_tick = SDL_GetTicks64();
  u64_t expected_tick = 0;

  u64_t last_key_pressed_tick = 0;


  u64_t last_time_rtc=0;
  u64_t current_time_rtc=0;


  while (loop) {
    if(sync_on_resume && enable_auto_time_sync)
    {
      last_time_rtc = current_time_rtc;
      current_time_rtc = get_current_time_milliseconds();
      if(last_time_rtc && last_time_rtc > current_time_rtc) {
        if(debug_level>=1) printf("oops, time goes back, last=%llu current=%llu, delta=%llu\n",last_time_rtc,current_time_rtc, current_time_rtc - last_time_rtc);
      }
      if(last_time_rtc && current_time_rtc - last_time_rtc > 10*1000) {
        if(debug_level>=1) printf("detected time jump last=%llu current=%llu, delta=%llu\n",last_time_rtc,current_time_rtc,current_time_rtc-last_time_rtc);
        //there is timejump in between, likely because of system sleep and recover
        if(nc2000mode||nc3000mode){
            void sync_time_2000();
            sync_time_2000();
        }
        if(nc1020mode){
            void sync_time_1020();
            sync_time_1020();}
      }
    }

    if(power_save) {
      SDL_Delay(200);
    }
    if(! power_save){
      RunTimeSlice(SLICE_INTERVAL);
    }
  
    if(reload_pending){
      if(debug_level>=1) printf("reload pending, exit main loop\n");
      break;
    }

    SDL_Event event;
    map<signed int, bool> mp;
    bool key_pressed= false;
    
    while (SDL_PollEvent(&event)) {
      if ( event.type == SDL_QUIT ) {
        loop = false;
      } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        key_pressed = true;
        bool key_down = (event.type == SDL_KEYDOWN);
        if (debug_level >= 2 && event.key.repeat == 0) {
          printf("[keyev] sym=%d scancode=%d %s\n",
                 (int)event.key.keysym.sym, (int)event.key.keysym.scancode,
                 key_down ? "down" : "up");
          fflush(stdout);
        }
        //try to consolidate multiple key shoot into one
        //not sure if necessary. But it's helpful for debug
        mp[event.key.keysym.sym]= key_down;
        for(auto it=mp.begin();it!=mp.end();it++){
          if(it->first==SDLK_LSHIFT || it->first==SDLK_RSHIFT){
            shift_down=it->second;
            continue;
          }
          if(it->first==SDLK_LCTRL || it->first==SDLK_RCTRL){
            ctrl_down=it->second;
            continue;
          }
          bool console_on_saved=console_on;
          handle_console(it->first, it->second);// handles 1. console toggle 2. console itself
          if(console_on_saved){
            continue;
          }
          if(use_legacy_key_io) handle_key(it->first, it->second);
          else handle_key_wayback(it->first,it->second);
        }
      } else if (event.type == SDL_TEXTINPUT) {
        input_text(event.text.text);
      }
    }

    if(!power_save){
      Render(expected_tick);
    }
    u64_t current_time = SDL_GetTicks64();

    if (key_pressed) {
      last_key_pressed_tick = current_time;
    }

    if(current_time - last_key_pressed_tick >power_save_interval*1000ll){
      if(power_save == false){
        power_save = true;
        printf("enter power save\n");
      }
    }else{
      if(power_save == true) {
        power_save = false;
        if(enable_auto_time_sync&&sync_on_resume){
          if(nc2000mode||nc3000mode){
            printf("sync time on power save resume\n");
            void sync_time_2000();
            sync_time_2000();
          }
          if(nc1020mode){
            printf("sync time on power save resume\n");
            void sync_time_1020();
            sync_time_1020();
          }
        }
      }
    }

    /*
     * 音频背压（2026-09-27）。
     *
     * 061 的采样是跟着**模拟时间**产的，声卡是跟着**墙上时间**放的，中间那个队列就是
     * 两者的缓冲。模拟器为了追进度会短时间全速跑（下面那套 ±300 ms 漂移逻辑），
     * 那一小段里产样比声卡快 ~1.3 倍：实测队列 3307 → 15501 → 顶到 24000 上限，
     * 丢掉约 19000 个样本（≈0.3 s 的音乐），听感就是"播着播着突然快了几秒"
     * （用户 2026-09-27 报的现象，日志见 out/music_bug/gui_log6.txt）。
     *
     * 所以这里给它加背压：积压超过 ~47 ms 就停下来等声卡放掉一些，回到 ~24 ms
     * 再继续（阈值按当前采样率折算，所以音乐/发音/空闲都是同一个延迟量级）。
     * 只在音频真的在放时才等（队列不降就立刻放弃，免得声卡没开/暂停时把界面卡住）。
     */
    if (nc3000mode && !fast_forward) {
        int q = dsp061_queue_len();
        int rate = (int)nc3_dsp_sample_rate();
        int hi = rate / 21, lo = rate / 42;      /* 47 ms / 24 ms */
        if (hi < 1500) hi = 1500;
        if (lo < 700)  lo = 700;
        if (q > hi) {
            int spins = 0;
            while (q > lo && spins++ < 100) {
                SDL_Delay(1);
                int q2 = dsp061_queue_len();
                if (q2 >= q) break;      /* 没有在放：别死等 */
                q = q2;
            }
            if (getenv("NC3_SND_DEBUG"))
                printf("[mix-backpressure] waited %d ms, queue %d\n", spins, q);
        }
    }

    expected_tick+=SLICE_INTERVAL;
    u64_t actual_tick= current_time - start_tick;

    if(fast_forward && !fast_forward_limit) {
        expected_tick =actual_tick;
    }

    //if actual is behind expected_tick too much, we only remember 300ms
    if(actual_tick >expected_tick + 300) {
      expected_tick = actual_tick-300;
    }

    // similiar strategy as above
    if(expected_tick > actual_tick + 300) {
      actual_tick = expected_tick-300;
    }

    if(actual_tick < expected_tick) {
      SDL_Delay(expected_tick-actual_tick);
      long long exceed=current_time -start_tick  -expected_tick;
      if(exceed>10){
        if(debug_level>=1) printf("oops sleep too much %lld\n",exceed);
      }
    }

  }
}

//Entry Point of the emulator, this function can be called repeatedly if you need.
// repeating calling this function can be useful if you are creating an emscripten/android/ios version.
//E.g., you can change model and rom path, then call this function again to switch to new model and rom without restarting the whole program,
// check reload/load_nc2000/load_nc1020 command in cmd.cpp as an example
void emu_entry(){ 
    LoadNC2k();
    main_loop();
}

int main(int argc, char* args[]) {
#if defined(_WIN32)
  /*
   * GUI 版是用 -mwindows 编的（Windows 窗口子系统）。默认给用户**挂一个控制台窗口**
   * 看日志（用户 2026-09-27 要求）；不想看就加 --no-console。
   *
   * 注意顺序：控制台先出来，SDL 窗口随后创建并 SDL_RaiseWindow() 提到最前，
   * 这样键盘焦点在模拟器窗口上——早期版本直接用控制台子系统，控制台抢焦点、
   * 用户按的键全被它吃掉，看起来就像"按 N 进了 debug 命令行"。
   */
  bool want_console = true;
  for (int i = 1; i < argc; i++) {
    if (strcmp(args[i], "--no-console") == 0) { want_console = false; break; }
    if (strcmp(args[i], "--console") == 0) {
      want_console = true;
    }
  }
  if (want_console && AllocConsole()) {
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);
  }
#endif
  process_args(argc, args);
  init_resource();

  do {
    reload_pending=false;
    emu_entry();
  } while (reload_pending);

  SaveNC2kIfNeed(); // handle --auto-save-flash or --auto-save-all

  shutdown_audio(); //explictly shutdown audio to avoid bug on some platform. Other resources doesn't need this, since OS can always recollect them correctly.

  return 0;
}
