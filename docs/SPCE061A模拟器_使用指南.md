# SPCE061A 模拟器 使用指南

> 适用对象：想给**文曲星模拟器**（NC3000 / TC2000 这一类"6502 主控 + SPCE061A 语音 DSP"
> 的机器）配上声音，或者只想把 `celp_data` / `.vnt` / `.a16` 这类资源解码成音频的人。
> 代码在 `spce061a/`，本文只讲**怎么用**；内部机制见 `docs/SPCE061A模拟器_代码解析.md`。
> 协议细节见 `spce061a/docs/NC3000-061协议对照.md`。

---

## 0. 它能做什么、不能做什么

**能**（都跑**真实 061 固件**，不是桩程序）：

| 能力 | 说明 |
|---|---|
| A1600 流式语音（`.a16`） | 端到端与官方参照逐样本相关 **1.0000** |
| S200 教材（`.vnt` 一般品质 / 官方 `.s20`） | 直驱通路与官方 `S200.exe` 参照包络相关 **0.9911** |
| S600 单词/教材（`celp_data` 18 字节帧、`.vnt` 高品质） | 用户实听确认"声音正常了，完美" |
| MS01 音乐 | 模拟器跑**真机 061 固件**（`061.dat` 或 NAND 的 `升级061.bin`），按主控时序喂原机 `/midi/音乐N.mid`；引擎自己合成（整曲 28.21 s 等结果与旧 SDK 方案逐样本一致） |
| **主控 ↔ DSP 的字节协议** | 与 NC3000 主控固件逐条对齐，22 项一致性自检全过 |

**不能**：不模拟 flash 编程、不做录音通路（`0x80..0x85`）、PWM/ADC 只有骨架、
EXT1 外部时钟未建模、TTS 合成音库没接通。

---

## 1. 先跑通：不用编译自己的代码

仓库里已经有几条"一条命令出 WAV"的路径（都是本机实测过的）：

| 工具 | 输入 | 说明 |
|---|---|---|
| `tools\vnt2wav.ps1 <in.vnt> [out.wav] [-MaxFrames N]` | S200 教材 / `.s20` | 走 **S200 直驱**（`s200_map` 内核），8 kHz 单声道 |
| `tools\s600vnt2wav.ps1 <一般教材.vnt> [out.wav]` | S600 教材 | 走**修好的 ROM 协议**（`word.exe` mode 27），36,790 Hz |
| `tools\nc3000_word2wav.ps1 <起帧> <帧数> [out.wav] [-Speed 0..4] [-Inode 26]` | `celp_data` 的 18 字节帧 | 单词发音；`-Speed 2` 是项目标准 |
| `tools\celp_frames.js <起帧> <帧数> <out.bin> [inode=26]` | NAND 整合镜像 | 从 `celp_data` 里切帧（自己按页去交织、自己解析 inode 块表） |

```powershell
# 例：把 S200 教材解成 WAV
.\tools\vnt2wav.ps1 "info\视听教材\...\一般品质\中级读物-2.vnt" out\a.wav

# 例：把 celp_data 第 0 帧起的 300 帧解成 WAV（速度档 2 = 芯片默认）
.\tools\nc3000_word2wav.ps1 0 300 out\b.wav -Speed 2
```

这些脚本依赖 `spce061a\build\*.exe`（仓库里已带）。要自己编见 §3。

---

## 2. 最小文件集

```
emu\unsp.c  emu\unsp.h            # µ'nSP CPU 核心（必需）
emu\spce061a.c emu\spce061a.h     # 外设模型（必需）
emu\firmware_061.c                # 061.dat 映像 0x8200..0x10000（必需）
host\nc3000_dsp.c host\nc3000_dsp.h   # 要"主控协议"时才需要
emu\a1600.c emu\a1600.h emu\a1600_image.c emu\a1600_player.c/.h  # A1600 播放器（可选）
emu\main_linux_ms01.c host\firmware_061_nand.c                    # MS01 音乐（可选）
```

* 纯 C99，无第三方库（连 `math.h` 都不需要，`host\*.c` 里用整数开方）。
* 接口只有 `<stdint.h>` / `<string.h>`；把 `spce_t`/`unsp_t` 塞进你的外设结构即可。
* 本机编译器：`C:\Users\YYGSM\Documents\work\WQX_NC3K_SYS\toolchain\tcc\tcc.exe`
  （2026-09-28 从早期工作区 `Documents\Codex\2026-09-21\ce\work\tools\tcc\` 搬进仓库，跟着仓库走）
  （TCC 0.9.27）。MSVC / gcc / clang / Keil 也能编。

---

## 3. 从源码编译（本机已验证）

```powershell
$tcc = "C:\Users\YYGSM\Documents\work\WQX_NC3K_SYS\toolchain\tcc\tcc.exe"
cd spce061a

# 1) 主控协议一致性自检（22 项）+ 音频回归  ← 建议先跑这个
& $tcc -O2 -I emu -o build\nc3000_conformance.exe host\nc3000_dsp.c `
       host\nc3000_conformance.c emu\spce061a.c emu\unsp.c emu\firmware_061.c
.\build\nc3000_conformance.exe testdata\t09_a.a16 build\conf.wav
# 实测输出：22 passed, 0 failed; ... wrote build\conf.wav: 73042 samples @ 31958 Hz

# 2) 整机模拟器：跑固件 + 用主控同样的流协议放 A1600
& $tcc -O2 -I emu -o build\fwstream.exe emu\main_linux_fwstream.c `
       emu\spce061a.c emu\unsp.c emu\firmware_061.c

# 3) 单词发音探针（S600 / celp_data）
& $tcc -O2 -I emu -o build\word.exe host\nc3000_dsp.c host\nc3000_word.c `
       emu\spce061a.c emu\unsp.c emu\firmware_061.c

# 4) A1600 命令行播放器（黑盒跑 a1600 映像）
& $tcc -O2 -I emu -o build\a1600play.exe emu\main_linux.c emu\unsp.c emu\a1600.c emu\a1600_image.c

# 4b) MS01 音乐播放器：模拟器加载真机固件 + 主控协议（不再用 SDK 示例映像）
& $tcc -O2 -I emu -I host -o build\ms01play.exe emu\main_linux_ms01.c `
       host\nc3000_dsp.c emu\spce061a.c emu\unsp.c emu\firmware_061.c host\firmware_061_nand.c
.\build\ms01play.exe 1 out\音乐1.wav                 # 整曲
.\build\ms01play.exe 1 out\音乐1.wav -fw nand        # 换 NAND 里的 升级061.bin

# 5) S200 直驱（vnt2wav.ps1 用的就是它）
& $tcc -O2 -I emu -o build\s200_mapr.exe host\s200_map.c emu\spce061a.c emu\unsp.c emu\firmware_061.c
```

`-I emu` 是必须的（源文件里 `#include "spce061a.h"`）。

---

## 4. 把它接进"文曲星模拟器"（重点）

有两条路，按你要的**保真度**选：

### 路线 A：DSP 侧整体仿真 —— 跑真机固件 + 真协议（推荐）

你的 6502 模拟器只负责两侧信号：
**① 发/收串口字节；② ready 线（061 的 IOB14 输出 → 主控 IO 0x0E bit4 输入）；
③ `/RESET` 线（主控 IO 0x0E bit3 输出 → 061 芯片复位，低有效）**。
其它全交给这边。最小骨架：

```c
#include "nc3000_dsp.h"                    /* 它已经 include 了 spce061a.h */

extern const uint16_t firmware_061[];
static spce_t         dsp;
static nc3000_link_t  link;

void dsp_reset(void) {
    int i;
    spce_init(&dsp, firmware_061, 0x8200u, 0x10000u);
    for (i = 0; i < 11; i++)                   /* 061.dat 缺 0xFFF5..0xFFFF 的向量表， */
        dsp.cpu.vec_area[i] = firmware_061[0xFC00 + i - 0x8200];  /* 从 0xFC00 复制重建 */
    dsp.cpu.vec_loaded = 1;
    dsp.cpu.r[0] = 0x07FF;                     /* SP */
    dsp.cpu.r[7] = 0xCB9F;                     /* PC = 复位入口 */
    n3_init(&link, &dsp, 40);                  /* 40 条指令/字节的喂数节拍 */
}

/* 你的主循环：每帧跑一点 */
void dsp_run(uint32_t insns) {
    n3_run_steps(&link, insns);                /* 内部已含 poll_irq + 计时 + 音频节拍 */
}

/* 主控侧发命令（这些函数一一对应主控固件里的例程，见 nc3000_dsp.h 注释） */
void dsp_link(void)        { n3_link(&link); }              /* 0xBB 0x0A -> 0xBB 0xFA */
void dsp_stop(void)        { n3_stop_ack(&link); }          /* 0xAA 0x00 -> 0xAA 0x00 */
void dsp_idle(void)        { n3_sleep(&link); }             /* 0xAA 0x01（之后必须重新握手） */
void dsp_volume(uint8_t v) { n3_volume(&link, v); }         /* 0x44 0..15 */
void dsp_speed(uint8_t i)  { n3_speed(&link, i); }          /* 0x55，i=0..4（项目标准 = 2） */
void dsp_start(uint8_t p)  { n3_start(&link, p); }          /* 0x99：0x00=MS01 0x21=S600 0x40=A1600 … */
void dsp_tts_head(uint8_t n){ n3_tts_header(&link, n); }    /* 0x11 0x01 <条数> */
void dsp_tts_rec(uint8_t a, uint8_t b, uint8_t c) { n3_tts_record(&link, a, b, c); }
int  dsp_send_block(const uint8_t *b15) { return n3_stream2(&link, b15); } /* 0x33 + 15B */

/* 主控读 ready 线（IO 0x0E bit4：低 = 可以发） */
int  dsp_ready_pin(void)   { return !n3_busy(&link); }

/* 主控收字节 */
int  dsp_recv(uint8_t *b)  { return n3_recv(&link, b); }

/* 取音频：DAC 值（无符号，0x8000 为中心）已经减掉中心，得到 16 位单声道 PCM */
void dsp_audio_pump(int16_t *out, int n) {
    int i;
    for (i = 0; i < n; i++) spce_audio_get(&dsp, &out[i]);   /* 无数据时填 0 */
}
```

要点：

* **主控发字节 = `n3_send()`**（内部就是 `spce_uart_rx()` + 跑一段指令）；`n3_send` 不会替你等
  ready 线，**要按主控固件那样先等**（`dsp_ready_pin()` 为真再发）。仓库里
  `host\nc3000_word.c` 的 `n3_send_slow()` 就是这么做的（ready 等待上限 2,000,000）。
* **收字节 = `n3_recv()`**，它按主控固件的超时计数（`LDX #$FF / LDY #$FF`）折算成指令数。
* **`/RESET` 线（IO 0x0E bit3，低有效）不要漏**：真机 BIOS 每次要跟 061 说话前都会把它
  脉冲一次（`LDA $0435/AND #$F7/STA $0E` → 延时 → `ORA #$08/STA $0E`）。仿真侧"放开复位"
  那一拍等于**重新装载 061 映像、从复位向量 `$CB9F` 跑起来**（本项目就是
  `spce061_bridge.cpp` 的 `nc3_dsp_boot()`）。漏了它的后果不是"少个信号"而是**行为不同**：
  复位后 `CC 0F` 走复位路径回 `CC FF`，没复位过就走主循环回 `CC 0F` —— 自测第 9 项就是
  栽在这上面（`NC3000_DSP检测与升级.md` §7）。
* `n3_speed()`/`n3_volume()` 必须在 **stop/idle 之后**发（固件 `$C41E` 会把速度重置成 0x0C）。
* 一次最多**填满一个环**（280 字 = 560 字节）就要等 IOB14 再次变低，别连发。

### 路线 B：只要声音 —— 直接驱动解码内核（不要协议）

不需要 6502、不需要串口。以 A1600 为例（`emu\a1600.h`）：

```c
#include "a1600.h"
extern const uint16_t a1600_image[];        /* A1600 内核映像（tools/mkimage.py 生成） */
extern const uint16_t a1600_image_first;    /* = 0x8200 */
extern const uint32_t a1600_image_count;    /* = 20480 字（0x8200..0xD200） */

static uint16_t image[A1600_IMAGE_WORDS];   /* 以地址 0x0800 为 0 号元素的映像 */
static a1600_t  dec;
static int16_t  pcm[A1600_FRAME_SAMPLES];

void a1600_begin(const uint8_t *a16, uint32_t size) {
    /* 内核放在它自己的地址上；其余空间留给"码流窗口"（a1600.c 会自动往下搬） */
    memcpy(image + (a1600_image_first - A1600_ROM_BASE), a1600_image,
           a1600_image_count * sizeof(uint16_t));
    a1600_init(&dec, image);                 /* 128 KB 映像版 */
    a1600_load(&dec, image, a16, size);      /* 自动跳过 4 字节长度头 */
}

/* 每次一帧 128 样本；A1600 是 2bit/样本的 256bit 帧，固件里再 4 倍上采样到 8 kHz */
int a1600_pull(int16_t **out) {
    int n = a1600_decode(&dec, pcm);
    *out = pcm;
    return n;
}
```

* **A1600**：`a1600.[ch]`（内核入口 `0xB5D4/0xB5D6/0xB5E6`，64 点 FFT 在 `0xCFE8/0xD113`；
  上面的写法就是 `emu\main_linux.c` 的做法）。**内存小的 MCU** 用 `a1600_init_split()`
  —— 内核放 flash（按 `a1600_image` 直接索引），只给码流留一小块 RAM 窗口。
* **MS01 音乐**：`emu\main_linux_ms01.c` —— 模拟器跑真机固件（`061.dat`／`升级061.bin`），
  宿主按主控时序发 `0x99 0x00` + `0x33`/`0x22` 推乐谱；取音是 **DAC2（`0x7016`）**、
  减 `0x8000` 中点、按固件 TimerA 的 **63833 Hz** 写 WAV。细节（含短乐谱必须补齐、
  MS01 引擎地址地图）见 `../docs/NC3000_MS01音乐复现_20260926.md`。
* **S200**：照 `host\s200_kern.c` / `s200_map.c` 的做法 —— 把码流放进 RAM 窗口
  （`cpu.win`/`win_base=0x1000`），按固件自己的音频初始化设置 RAM
  （`[0x0000]`、`[0x0001]=0x8000`、`[0x0002]=0x000C`、`[0x0003..5]=2`、`[0x04CE]=2`、
  `[0x04C0]=流起始`），然后依次调
  `0x8254`（流头解析）→ `0xC8C6`（编解码初始化）→ 反复 `0x82C6`（解一帧），
  **合成结果在 `RAM[0x0541]`，长度 = `RAM[0x04D4]`**。
  "调用一个函数"的做法参见 `s200_kern.c` 的 `call_fn()`：
  `SP=0x07FF; PC=入口; RAM[0x07FF]=SENT(0x1000); RAM[0x07FE]=SR; SP=0x07FD; unsp_run_until(cpu, SENT, budget);`
* **S600 单词**：走协议最省事（路线 A + `n3_start(0x21)` + TTS 记录），仓库里
  `host\nc3000_word.c` / `tools\nc3000_word2wav.ps1` 已经封装好。

---

## 5. 音频取出、采样率与语速

| 项 | 做法 |
|---|---|
| 取样本 | `spce_audio_avail()` / `spce_audio_get()`（4,096 样本环形缓冲，溢出计数 `audio_overruns`） |
| 或直接挂钩 | `spce_t.io_trace` 里监听 `0x7016`（P_DAC2，音频 FIQ 写的就是它；A1600 路径） |
| 数值约定 | DAC 是无符号、`0x8000` 为中点；环里存的是**已减中点**的 `int16` |
| 采样率 | `spce_dac_sample_rate(&dsp)`：由固件写的 `P_TimerA_Data` 推出 `Fosc/2/(0x10000-preload)` |
| 实测速率 | A1600/S200 路径 31,958 Hz；S600 单词（速度档 2）36,790 Hz；S600 教材 36,790 Hz |
| 语速 | `n3_speed(idx)`，`idx=0..4` → 表 `{0x06,0x09,0x0C,0x0F,0x12}`；**项目标准档 = 2**（芯片默认 0x0C） |
| 要输出 44.1/48 kHz | 先按上面的固件速率取样本再重采样（别改固件的 TimerA 去凑输出设备） |

---

## 6. 必须遵守的时序规则（踩过的坑）

| # | 规则 | 违反后的症状 |
|---|---|---|
| 1 | 发数据前等 **IOB14 变低**（`spce_dsp_busy()`） | 061 侧解析器永远回不到状态 0，双方互等 |
| 2 | 一次最多填 **一个环**（280 字 = 560 B） | 同上（`0xCD18` 一直等状态 0） |
| 3 | **不要发半个包**（`0x33+15` / `0x22+len+payload` 要发完） | 环重装永远等不到"状态 0" |
| 4 | 收字节要**一字节一手握**（`n3_send_slow` 的 ready 等待上限给足，≥2,000,000） | 喂数快 ~46 倍 → 环被覆盖 → 成片削顶 |
| 5 | `0xAA 0x01`（睡眠）之后**必须重新 `0xBB 0x0A` 握手** | 061 不再响应任何命令 |
| 6 | 命令**没有统一应答位**，要按**状态**判断（如 `[0x04CE]` 是否更新） | 按固定指令数判断会误判"卡死" |
| 7 | **先 stop/idle，再发 `0x44` 音量 / `0x55` 速度，最后 `0x99` 选解码器** | 速度被 `$C41E` 重置成默认 0x0C（三档速度解出同一个 WAV） |
| 8 | `0xAA 0x02`（END_CMD）要在**数据流中间**发（首块之后） | 发早了门限立刻触发、061 停机；发晚了 `[0x04C8]` 冻结、解码停 |
| 9 | UART 接收 FIFO 只有 **64 字节**且**静默丢弃**（`uart_rx_drops`） | 丢一个字节 → 包解析器失步，整条流报废 |
| 10 | 主控读端口要读**引脚电平**（`P_IOx_Data`）而不是输出锁存 | 061 的 IOB2 flow-control 会死循环 |
| 11 | **听感/回归必须用当前源码重新编译的 exe** | 历史 exe 会冻结当时的核心 bug：例 `build\s200_probe.exe`（9/23 构建）不含 `SB` 中断保护修复，S200 直接解出"rms 5746 / 满量程 1543 个"的削顶杂音。判据：exe 时间戳 ≥ `emu\unsp.c`、`emu\spce061a.c` 的最后修改时间 |

---

## 7. 性能与移植

* **不要让 `spce_tick()` 按满速晶振跑音频**：`emu\spce061a.h` 提供了
  `spce_timer_fire_audio()` —— 由宿主"每个输出样本调一次"，把音频时钟和解释器速度解耦。
  默认节拍 `fire_every = 1538`（≈ 49.152 MHz/31,958 Hz 的指令预算）。
* `nc3000_link_t.tick_div` 可以整体放慢外设时钟；`no_hold` 控制"数据用完时是否继续给音频节拍"。
* 本机 TCC -O2 实测：A1600 端到端 4,578 字节输入 → 73,042 样本，运行时间与音频时长同量级
  （可实时）；探针跑一帧 S600 只需要几十万条指令。
* STM32F407VGT6（Keil）工程原来在 `Documents\Codex\2026-09-21\ce\work\*`（几个
  `vf_*/vg_*/vt_*` 副本里各有一份 `test_061`），**2026-09-28 随早期工作区清理掉了**：
  那份移植没有继续，仓库里只留了 `emu\main_stm32*.c`（裸机入口）作为参考
  （两个 Target：整机模拟器 / A1600 播放器，文件清单相同、靠宏区分）。
  Linux 端有 `emu\main_linux*.c`；另有一份同语义的 Python 解释器 `emu\unspemu.py`（慢，只适合查指令）。

---

## 8. 环境自检清单

```powershell
cd spce061a
# 1) 编译并运行一致性自检 —— 期望 "22 passed, 0 failed"，并写出 conf.wav
#    （命令见 §3 第 1 条）
# 2) 解一个 S200 教材，检查 RMS/峰值不是 0，也不是持续满量程
..\tools\vnt2wav.ps1 "..\info\视听教材\...\一般品质\*.vnt" ..\out\check_s200.wav
# 3) 解一段 celp_data
..\tools\nc3000_word2wav.ps1 0 300 ..\out\check_word.wav -Speed 2
```

三条都通过，说明 CPU 核心、外设模型、固件映像、音频通路都就绪。
