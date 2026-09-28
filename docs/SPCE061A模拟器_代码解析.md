# SPCE061A 模拟器 代码解析

> 目的：**看懂它怎么工作**、以及"为什么代码里要这么写"。
> 用法见 `docs\SPCE061A模拟器_使用指南.md`；链路协议见 `spce061a\docs\NC3000-061协议对照.md`。
> 全部结论都对应仓库里的实际实现，关键处给出文件与行号。

---

## 0. 一张图看全

```
                 ┌──────────────────────────── 你的程序 / 工具 ───────────────────────────┐
                 │  tools\*.ps1（vnt2wav / nc3000_word2wav / s600vnt2wav）                 │
                 │  host\nc3000_word.c（单词探针，30+ mode）  emu\main_linux*.c（整机/播放器）│
                 └───────────────┬───────────────────────────────┬────────────────────────┘
                                 │                               │
         ┌───────────────────────┴──────────┐        ┌───────────┴───────────────┐
         │ host\nc3000_dsp.c  主控协议模型   │        │ emu\a1600.c                │
         │  n3_link/volume/start/tts_*…     │        │ （直接驱动解码内核）        │
         └───────────────┬──────────────────┘        └───────────┬───────────────┘
                         │  spce_uart_rx / spce_audio_get        │ unsp_run_until()
                 ┌───────┴──────────────────────────────────────┴───────┐
                 │ emu\spce061a.c  外设模型（TimerA/B、UART、端口、DAC） │
                 └───────────────────────┬──────────────────────────────┘
                                         │ unsp_step() / unsp_read() / unsp_write()
                 ┌───────────────────────┴──────────────────────────────┐
                 │ emu\unsp.c  µ'nSP CPU 核心（+ unsp.h 里的 RAM/中断表）│
                 └───────────────────────┬──────────────────────────────┘
                                         │ 取指
                 ┌───────────────────────┴──────────────────────────────┐
                 │ emu\firmware_061.c  真实 061 固件映像 0x8200..0x10000 │
                 └──────────────────────────────────────────────────────┘
```

数据流（播放一段压缩语音）：

```
码流字节 ──(UART 收)→ 固件解析器 → 解码环(RAM) → 解码内核(FIQ 驱动) → DAC(0x7016)
        → audio[] 环形缓冲 → 你的 I2S/声卡      （或 io_trace 抓 DAC → WAV）
```

---

## 1. 地址空间（`unsp_read` / `unsp_write`，`emu\unsp.c:29` / `:47`）

| 地址 | 内容 | 说明 |
|---|---|---|
| `0x0000-0x07FF` | **RAM**（2 KB 字） | 固件的工作区；`unsp_t.ram[0x800]` |
| `0x1000-0x67FF` | **可选 RAM 窗口**（`cpu.win`） | 给"内核在 ROM、码流在 RAM"的省内存布局用；`a1600_init_split()`、`s200_kern.c` 用它把码流放进 RAM |
| `0x7000-0x7FFF` | **I/O**（`struct unsp_io`） | 接到 `spce061a.c` 的 `io_read/io_write` |
| `0x8200-0xFFFF` | **ROM**（`cpu.rom`） | `firmware_061.c`，`rom_base=0x8200`、`rom_end=0x10000` |
| `0xFFF5-0xFFFF` | 中断向量表 | **不在 061.dat 里**，见 §4 |
| `0xFFFF` | `shadow_ffff` | SACM 内核把它当暂存/分段寄存器用（不能当普通 ROM 读） |

两个坑（注释里都写了原因）：

* `rom_end` 必须是 **32 位**：64 KB 映像的"结尾"是 `0x10000`，用 `uint16_t` 会回绕成 0，
  于是**所有取指都失效**（`emu\spce061a.h` 的注释）。
* RAM 窗口的优先级**高于 I/O**：`0x1000-0x67FF` 在 I/O 之下，所以两者不冲突。

---

## 2. CPU 核心 `emu\unsp.c`（µ'nSP，约 590 行的 16 位机解释器）

### 2.1 数据结构（`emu\unsp.h`）

```c
uint16_t r[8];      // 0=SP 1=r1 2=r2 3=r3 4=r4 5=BP 6=SR 7=PC   ← 索引宏见 unsp.c:5
uint32_t sb;        // 4 位移位缓冲（FR 的一部分）
uint8_t  fir_mov;   // FIR_MOV 标志：MULS 之后是否搬移输入窗口
uint8_t  enable_fiq/enable_irq;
uint8_t  int_depth, fiq_depth, irq_depth;
uint8_t  int_is_fiq[8]; uint16_t int_sp[8]; uint32_t int_sb[8];   // 中断现场
uint16_t vec_area[11];            // BREAK,FIQ,RESET,IRQ0..IRQ7
uint16_t ram[0x800];              // 2 KB 字
const uint16_t *rom; uint16_t rom_base; uint32_t rom_end;
uint16_t *win; uint16_t win_base, win_end;
const struct unsp_io *io;
void (*trace)(...);  void (*wr_trace)(...);   // 调试钩子
```

`SR` 的标志位：`N 0x0200 / Z 0x0100 / S 0x0080 / C 0x0040`（`unsp.h:19`）。
**SB 不在 SR 里而是单独字段**，原因见 2.3。

### 2.2 取指与译码（`unsp_step` @ `unsp.c:501`，`execute` @ `:191`）

```c
uint16_t lpc = cpu->r[7];            // PC
uint16_t op  = unsp_read(cpu, lpc);  // 取指
cpu->r[7] = lpc + 1;                 // 先自增（多数指令的"下一条"）
cycles = execute(cpu, op);
```

`execute()` 把 16 位操作码按位域切开（这是 µ'nSP 的"四种格式"）：

```
op0 = op >> 12   (4 bit, 操作类型)
opa = (op >> 9) & 7    (目标寄存器)
op1 = (op >> 6) & 7    (寻址方式)
opn = (op >> 3) & 7    (子操作/位数)
opb = op & 7           (源寄存器)
```

按这个划分走：`op0==0xD` 是 **存数**（`[r2] = r0`）、其余是**运算**；
`op1` 决定第二个操作数怎么来（`0`=`[BP+imm6]`、`1`=立即数、`3`=间接/自增自减、`4`=寄存器/立即数16/`[imm16]`/移位族、
`5`=LSL/LSR、`6`=ROL/ROR、`7`=直接6位地址）。

三条用"整条 16 位值"而不是位域判定的特殊指令：

| op | 含义 | 代码 |
|---|---|---|
| `0x9A90` | `RETF`（弹 SR、PC） | `unsp.c:193` |
| `0x9A98` | `RETI`（弹 SR、PC，并**释放一级中断**） | `unsp.c:222` |
| `0xF040` 家族 | `CALL imm16` / `GOTO imm16` / `GOTO MR`（目标地址在**下一条**字里） | `unsp.c:245` 起 |

条件跳转：`jump_condition()`（`unsp.c:107`）把 `op0`（0..13）映射成 `jb/jae/jge/jl/jne/je/jpl/jmi/jbe/ja/jle/jg/jvc/jvs`；
相对偏移是 `op & 0x3F`，`op1==0` 向后、`op1==1` 向前（`unsp.c:280` 起）。

### 2.3 移位与 `SB`（**这个项目最贵的一个坑**）

µ'nSP 的移位指令把"移出去的位"放进 **SB**，多条移位可以串起来处理 32 位数据。
代码里 `op1==5`（LSL/LSR）与 `==6`（ROL/ROR）都是
`shift = (sb<<16 | r[opb]) << n` 这种"拼上 SB 再算"的形式（`unsp.c:447` 起）。

为什么值得单列：**中断硬件会把 FR（含 SB）压栈**，所以中断里的运算不能污染 SB。
早期版本没保存 SB，音频 FIQ 里的 `MULS`（MAME 语义会把 SB 清 0）正好撞进 S200 内核的多步移位，
LPC 滤波器状态发散、每帧削顶。修法见 2.6 与 `unsp.h` 里 `int_sb[]` 的注释。
实测（`lrc_800.s20` 188 帧）：满量程写 **12898 → 1**，RMS 13066 → 1650（参照 1677）。

### 2.4 `MUL` 与 `MULS`（`do_mul` @ `:131`，`do_muls` @ `:152`）

* `MUL`：`MR(r4:r3) = Rd(*Rs)`，`ss` 决定是否**两边都当有符号**（`unsp.c:131`）。
  结果高字进 `r4`、低字进 `r3` —— 这解释了 SACM 内核为什么总是 `r3/r4` 成对出现。
* `MULS`：**n 项内积**（`MR = Σ [Rd+i]*[Rs+i]`），指针自动前移、`n==0` 表示 16 项；
  若 `fir_mov` 置位，还要把输入窗口整体上移一位（`mem[i] = mem[i-1]`）。

  两个关键实现点：
  1. 累加器用 **int64**：n≥3 时 32 位会溢出（S200 内核用 n=4/10/11/15），
     A1600 只用 n=2 所以以前没暴露；溢出会计数到 `unsp_muls_ovf`（全局诊断量）。
  2. 执行完**清 SB**（MAME 的 `execute_muls_ss()` 语义），所以必须靠 2.6 的现场保护兜住。

### 2.5 调用/返回与栈（`push`/`pop` @ `:71`/`:77`）

* 栈是**向低地址**增长：`push` 先写 `[SP]` 再 `SP--`，`pop` 先 `SP++` 再读。
  `CALL` 压 PC、再压 SR；`RETF/RETI` 反序弹出（`unsp.c:245`、`:193`）。
* 固件的"函数指针调用"是 `CALL abs16` + 在数据里放目标地址；
  工装里手工调内核时用的是`SP=0x07FF; [0x07FF]=哨兵; [0x07FE]=SR; SP=0x07FD; PC=入口` + `unsp_run_until(哨兵)`
  （见 `host\s200_kern.c:40` 的 `call_fn()`）。

### 2.6 中断（`take_interrupt` @ `:526`）—— 三个"教科书级"细节

```c
uint16_t vector = cpu->vec_area[index & 15];   // 注意是 & 15，不是 & 10！
int is_fiq = (index == 1);
if (is_fiq) { if (!enable_fiq || fiq_depth) return; fiq_depth++; }
else       { if (!enable_irq || irq_depth || fiq_depth) return; irq_depth++; }
// 记录现场：SP、是不是 FIQ、以及 **SB**
int_is_fiq[depth]=is_fiq; int_sp[depth]=SP; int_sb[depth]=sb; depth++;
push(PC); push(SR); PC = vector;
```

1. **FIQ 可以抢 IRQ，反之不行**；FIQ 内部不再嵌套。
2. **`RETF` 不解除中断锁**：061 固件的 UART 处理是用 `CALL` 进去、`RETF` 返回的，
   真正的 ISR 收尾才 `RETI`。所以退出逻辑用"**SP 是否回到入栈时的高度**"判断
   是不是真的退到了上一层（`unsp.c:200` 起的 `while` 循环），否则电平触发的 UART 中断
   会立刻重入、每收一字节吃掉两个栈字。
3. **SB 随中断保存/恢复**（`int_sb[]`），原因见 2.3；`RETI` 时恢复（`unsp.c:231`）。

`vec_area` 那行 `index & 15` 的注释值得一看：曾经写成 `& 10`（十进制 10），
把位 0 抹掉，于是**奇数向量全部串到偶数邻居**（TimerA 的 FIQ 跑去跑 UART 处理程序）。

### 2.7 周期与钩子

`unsp_step` 返回本条指令的周期数并累加 `cicount`（`unsp.c:508`）。
周期数只是**相对权重**（用于节拍/预算），不是精确时序。两个钩子：

| 钩子 | 用途 |
|---|---|
| `cpu.trace(ctx, pc, op)` | 每条指令后回调（指令级追踪，`nc3000_word.c` 的 `tr_cb`） |
| `cpu.wr_trace(ctx, addr, value)` | 每次**数据写**回调（抓 DAC、抓环指针、抓内核输出） |

---

## 3. 外设 `emu\spce061a.c`（只建模固件真正用到的寄存器）

### 3.1 端口（`io_read` @ `:81` / `io_write` @ `:127`）

| 寄存器 | 语义 |
|---|---|
| `0x7000/0x7001/0x7002/0x7003` | P_IOA 数据/缓冲/方向/属性 |
| `0x7005/0x7006/0x7007/0x7008` | P_IOB 数据/缓冲/方向/属性 |
| `0x7009` | P_IOB 反馈（A 口读回 B 口） |

**读 `P_IOx_Data` 返回的是引脚电平**：`(latch & dir) | (ext & ~dir)` ——
输出脚回读锁存，输入脚回读外部电平（`spce061a.c:85`）。当年这里返回锁存，
导致 061 的 `P_IOB2` 流控死循环。
宿主用 `spce_set_io_a/b()` 驱动外部电平（按键、握手线）。

### 3.2 中断控制器

| 寄存器 | 处理 |
|---|---|
| `0x7010` | `P_INT_Ctrl`：只记掩码。**注意**：这里**不改** `cpu.enable_irq` —— INT ON/OFF 归 CPU 核，UART 中断是由 `P_UART_Command1` 使能的 |
| `0x7011` | `P_INT_Clear`：写 1 清对应 `int_flags`；清了以后把 `fiq_pending/irq_pending` 归零 |
| `0x702D` | `P_INT_Mask` |

位含义（`spce061a.c:20` 起）：`b13 FIQ_TMA`、`b12 IRQ1_TMA`、`b11 FIQ_TMB`、`b10 IRQ2_TMB`、
`b8/b9 IRQ3_EXT1/2`、`b7 IRQ3_KEY`、`b4 IRQ4_1KHz` …（与手册 page 91 对齐）。

### 3.3 TimerA / TimerB（`spce_tick` @ `:310`）

* 时钟源用三张表 `src_a/src_b/src_c` 描述（`spce061a.c:277` 起），除频用"分子/分母"整数比，
  不引入浮点；`110` 表示"常数 0"（停）、`101` 表示"常数 1"（透传）。
  TimerA = **源 A 与源 B** 相与，TimerB 只有源 C。固件给 TimerA 配的是 `0x0030`
  （源 A = `Fosc/2`，源 B = 常数 1）—— 音频 FIQ 就来自它。
* **计数器是向上溢出**：从 `P_TimerA_Data` 加到 `0x10000`，
  所以"剩余周期 = `0x10000 - 当前值`"（不是 `data - count`）。
  这是第二个大坑：以前按向下计数写，导致音频 FIQ 每百万条指令才来几次。
* 溢出时置 `INT_FIQ_TMA|INT_IRQ1_TMA`，按 `P_INT_Ctrl` 决定走 FIQ 还是 IRQ1
  （`spce061a.c:340` 起）；TimerB 类似（IRQ2/FIQ_TMB）。

### 3.4 UART

| 寄存器 | 语义 |
|---|---|
| `0x7021` | `P_UART_Command1`：`b7` = RxIntEn、`b6` = TxIntEn（**UART 中断由它使能**） |
| `0x7022` | `P_UART_Command2` 读：`b7` RxRDY、`b6` TxRDY（发送瞬时完成，所以 TxRDY 恒 1） |
| `0x7023` | `P_UART_Data`：写=发送（回调 `uart_tx`），读=取接收 FIFO |
| `0x7024/0x7025` | 波特率分频（`spce_uart_baud()`：≥30 MHz 用 `Fosc/4/scale`，否则 `Fosc/2/scale`），变化时回调 `uart_baud` 让宿主重设真实串口 |

接收侧：64 字节 FIFO（`uart_rx_fifo[64]`），满了**静默丢弃**并计 `uart_rx_drops`；
`spce_uart_rx()`（`:228`）投递字节。**UART 中断是电平触发的**
—— 只要 FIFO 非空且 RxIntEn 且 INT ON，就一直请求 `_IRQ7`，
所以宿主必须在指令循环里调 `spce_poll_irq()`（`:248`），
这也解释了"固件先初始化 UART、后开中断，字节不会丢"。

### 3.5 DAC → 音频环

* `0x7016`(P_DAC2) / `0x7017`(P_DAC1) 写入都会 `audio_push()`（`:32`）：
  DAC 是无符号、`0x8000` 为中点，环里存 **减去中点** 的 `int16`。
* 环 `audio[4096]`，`audio_head/tail` 无锁递增；写满时**丢最旧**并计 `audio_overruns`。
* 取样本：`spce_audio_avail()` / `spce_audio_get()`（`spce061a.h:138`）；
  也支持用 `io_trace` 直接抓 `0x7016`（A1600 的播放器就是这么抓的）。

### 3.6 时钟、分频与"固件当前采样率"

* `spce_init()` 默认 `fosc = 49,152,000`（语音型号常见值），可用 `spce_t.fosc` 覆盖；
  `0x7013` 写的是 CPU 分频（1/2/4/…/128，`spce061a.c:186` 起）。
* `spce_dac_sample_rate()`（`:49`）：`Fosc/2 / (0x10000 - P_TimerA_Data)`。
  实测：`P_TimerA_Data=0xFCFF` → **31,958 Hz**（A1600/S200，等于 8 kHz 的 4 倍上采样）；
  S600 单词播放期间 `0xFD64` → **36,790 Hz**（≈12 kHz 内容的 3 倍多）。

### 3.7 "音频节拍"设计：把音频时钟交给宿主

`spce_timer_fire_audio()`（`:378`）**只做一件事**：把 TimerA 重装成固件写的值并拉一次 FIQ。
于是宿主可以按"**每个输出样本调一次**"来驱动音频（`emu\main_linux_fwstream.c` 的注释
和 `host\nc3000_dsp.c` 的 `audio_pace()`），不必真的按 49 MHz 去跑外设时钟：

* 用解释器全速跑 `spce_tick()` 也能对，但**慢且对参数敏感**；
* 用"样本时钟驱动"后，音频内容与宿主速度无关，只要求每样本给足够指令数
  （默认 `fire_every = 1538` ≈ 49.152 MHz / 31.958 kHz）。
* `audio_pace()` 里额外的"**别跑过半缓冲**"判断（比较 `[0x04C1]` 与 `[0x04CD]`）
  是 A1600/S600 内核的硬要求：FIQ 每 4 次吃一个环字，跑过头就把解码器顶死。

### 3.8 DSP 握手的两个引脚（`spce061a.h:108` 起）

| 位 | 方向 | 含义 |
|---|---|---|
| `P_IOB bit14` | 061→主控（输出） | 本机接收环还能不能收：拿完一包清低（`0xCCE9`），重装环时抬高（`0xCD02`）。宿主用 `spce_dsp_busy()` 读；**只有它为低才能发下一个包** |
| `P_IOB bit2` | 主控→061（输入） | "主机还能收数据"，061 发字节前轮询（`0xCFE0`）；宿主用 `spce_set_host_ready()` 控制 |

除了这两根 port 线，**主控还有一根芯片级的 `/RESET` 接在 061 上**，不在 `P_IOB` 里、
也不在串口协议里：主控端口是 **`IO 0x0E` bit3（输出，低有效）**，bit4 才是上面那根
ready 线的输入。

* 那是"文曲星这一侧的建模"，不在本目录（`spce061a/`）的代码里 —— 落在
  `src_nc3000/io_new.cpp`：bit3 出现 0→1 的上升沿就调 `nc3_dsp_boot()`（重装映像、从
  `$CB9F` 重跑）。2026-09-28 之前这条线没建模，是自测第 9 项报 ERR 的真因。
* 对 061 固件的影响是实打实的：**复位后 `CC 0F` 走复位路径（回 `CC FF`），没复位过就走
  主循环（回 `CC 0F`）**。协议/反汇编细节见 `NC3000_DSP检测与升级.md` **§7**，
  排查全过程见 `NC3000模拟器_改造计划.md` **§25**。

---

## 4. 固件映像与向量表（`emu\firmware_061.c`）

* 文件头两行说明一切：`firmware_061_first = 0x8200`、`firmware_061_count = 32256`
  → 覆盖 `0x8200..0xFFFF`（64 KB 映像去掉 RAM/I/O 区），是**原始固件字节**（`tools/mkimage.py` 生成）。
* **向量表在 `0xFFF5..0xFFFF`，但 061.dat 里是空的**：真实芯片这一段是 ROM 尾部，
  而 dump 里没有；固件自己把 11 个向量留在 `0xFC00`。所以所有宿主都要先做
  ```c
  for (i=0;i<11;i++) m.cpu.vec_area[i] = firmware_061[0xFC00 + i - 0x8200];
  m.cpu.vec_loaded = 1;
  ```
  顺序按 `unsp.h`：`[BREAK, FIQ, RESET, IRQ0..IRQ7]`。
* 另有一份 **`host\firmware_061_nand.c`**：NC3000 机器里那份"升级061.bin"(64 KB)，
  与 `061.dat` 同族但不同版本；`host\nc3000_version.c` 会把两份都跑一遍做对照。

---

## 5. 解码内核在哪里、怎么被调用

| 编解码器 | 位置 | 触发方式 |
|---|---|---|
| **A1600** | 内核 `0xB5D4/0xB5D6/0xB5E6`，64 点 FFT `0xCFE8/0xD113` | 流式：`0x99 0x40` 选模式；也可用 `emu\a1600.c` 直接调（`a1600_decode()` 每次一帧 128 样本） |
| **MS01**（音乐） | 直接在固件里跑：`0x99 0x00` 选模式 → `0xC8C6` 分派到 `0xAADD` 初始化 → 宿主用 `0x33`/`0x22` 推乐谱 → 引擎在 `0xACCC` 推进读指针；输出在 **DAC2**（`emu\main_linux_ms01.c` 就是这条） | `0x99 0x00` |
| **S200** | `0x8254`(流头) / `0xC8C6`(初始化) / `0x82C6`(解一帧，主体 0x8339) | `0x99 0x10`；`host\s200_kern.c` 走直驱 |
| **S600**（单词/教材） | 内核 `0x9EA3` → `0xD225` | `0x99 0x21`；单词走 `0x11 0x01/0x03` 的 TTS 记录 |
| S480 / S600-1 | — | `0x99 0x20` / `0x22` |

`0x99` 参数 → 固件内部模式号（`RAM[0x04CE]`）的映射见
`spce061a\docs\NC3000-061协议对照.md` §3（0x00→3、0x10→2、0x21→4、0x40→1 …）。

---

## 6. 主控协议模型 `host\nc3000_dsp.c`

这个文件是"**主控固件的 C 版**"：每个函数都标注了对应的 6502 地址。

| 函数 | 主控例程 | 行为 |
|---|---|---|
| `n3_ready()` | `$FABE` | 循环等 `IOB14` 变低，超时 512×64 步 |
| `n3_send()` | `$FB1C` | `spce_uart_rx()` 投一个字节 + 跑 `gap` 条指令 |
| `n3_recv()` | `$FB49` | 从 `link.rx[]` 取字节，超时折算成指令数 |
| `n3_link()` | `$F9E9` | `0xBB 0x0A` → 等 `0xBB 0xFA`，最多重试 10 次 |
| `n3_stop_ack()` | `$FA44/$FCF3` | `0xAA 0x00` → 等 `0xAA 0x00` |
| `n3_end_ack()` | `$FA98` | `0xAA 0x03` → 条件应答（`[0x0000]` bit10） |
| `n3_start()` | `$FAE3` | `0x99 <参数>` |
| `n3_volume()` | `$FAEE` | `0x44 <level>`，先按 `RAM[0x0A98]` 夹住 |
| `n3_speed()` | `$FB01` | `0x55 <表[idx]>`，表 `{06,09,0C,0F,12}` |
| `n3_tts_header/record()` | bank0B `$9AFA/$9B27` | `0x11 0x01 n` / `0x11 0x03 r3 cnt r4` |
| `n3_stream2/1()` | `$FC7D/$FCB6` | `0x33`+15B 定长块 / `0x22`+长度+负载 |

执行与节拍（`n3_run_steps` @ `:61`）：

```c
for (i = 0; i < steps; i++) {
    spce_poll_irq(m);          // 电平触发的 UART 中断
    unsp_step(&m->cpu);        // 跑一条指令
    if (tick_div <= 1 || (steps % tick_div) == 0) spce_tick(m, 1);
    audio_pace(l);             // 到点就 spce_timer_fire_audio()
}
```

`rx_cb`（`:6`）挂在 `spce_t.uart_tx` 上，把 061 发出的字节收进 `link.rx[]`（256 深，溢出计 `rx_overrun`）。

---

## 7. 上层工装（写新探针时照抄这些模式）

| 文件 | 用途 / 学什么 |
|---|---|
| `emu\main_linux_fwstream.c` | **流式播放的参考实现**：boot 握手 → 按 `RAM[0x0714]`（解析器状态）与 FIFO 排空推进的 4 阶段状态机（`tx_phase 0..3`）→ 抓 DAC 出 WAV。想懂"怎么喂码流"看它 |
| `host\s200_kern.c` / `s200_map.c` | **直驱**：不跑协议，手工调内核（`call_fn` + 窗口化码流 + 从 `RAM[0x0541]` 取 PCM）。S200 提取工具用的就是这条 |
| `host\nc3000_word.c` | 单词发音的 **30+ 个 mode**：mode 27 = 真实传输（`0x33` 块 + 逐字节等 ready）；mode 28/29/30/31 = S600 直驱/收割工装；mode 26 = 挂钩解码器装载做逐字节校验；还有 bit 反转、静音帧自校验等对照实验 |
| `host\nc3000_conformance.c` | **22 项一致性自检**：握手、六种解码器选择、音量/速度、TTS 包、停止/结束应答、A1600 端到端音频回归 |
| `emu\main_linux.c` / `main_linux_ms01.c` | A1600 / MS01 命令行播放器。MS01 这个跑**真机 061 固件**（`-fw nand` 可换 NAND 的 `升级061.bin`）+ 主控协议；早期"链接 SDK 示例 MS01.S37 映像"的版本已删除 |
| `emu\unspemu.py` | 同语义的 Python 解释器（慢，查指令用） |

---

## 8. 调试手法（这个项目实际怎么定位问题）

1. **PC 计数与命中点**：`host\nc3000_word.c` 里对 `0xC742`（音频 FIQ 入口）、
   `0xB88F`（A1600 帧解码入口）、`0x823E`（握手循环）等地址计数。
2. **`trace` 钩子**：指令级打印（谁在写哪个寄存器）。
3. **`wr_trace` 钩子**：只抓数据写 —— 抓 DAC（`0x7016`）、抓环指针
   （`0x04C0/0x04C1/0x04C8`）、抓解码器装载目标，`s200_map.c` 用它把"内核输出落在哪"分类统计出来。
4. **`io_trace` 钩子**：抓外设寄存器写（`spce061a.c:129`），用来确认固件到底把 TimerA/端口配成什么。
5. **RAM 快照对比**：在同一个 PC 上把 `cpu.ram[0x800]` 整块 dump 出来 diff
   （工装 vs 真协议路径的"上下文差异 304 字"就是这么找到的）。
6. **一次性打印诊断量**：`muls_ovf`、`audio_overruns`、`uart_rx_drops`、
   `rx_overrun`、`timeouts`、`fires_held`、栈指针范围 `[min_sp..max_sp]`。
7. **可控旋钮**：`fire_every`（每样本指令数）、`tick_div`、`no_hold`、`gap`（字节间隔）、
   `byte_gap`（状态机里的字节间隔）—— 把"时序假设"变成可实验的变量。
8. **参照对拍**：`tools\s200_align.py` / `s200_cmp.py` 把 ROM 通路输出与直驱/官方参照
   按重采样 + 逐窗找 lag 做相关（长段必须逐窗找 lag，否则采样率微差会把相关系数压到 0.4）。

---

## 9. 关键常量的来历

| 常量 | 值 | 为什么 |
|---|---|---|
| 复位入口 | `0xCB9F` | 固件自己的入口（`main_linux*.c` 的 `RESET_ENTRY`） |
| 握手循环 | `0x823E` | 固件等 `0xBB 0x0A` 的地方 |
| 音频 FIQ 向量 | `0xC742` | `P_INT_Ctrl` 的 `FIQ_TMA` 走这里 |
| 向量表副本 | `0xFC00` | 061.dat 缺 `0xFFF5..0xFFFF`，固件把 11 个向量留在这 |
| `fire_every` | 1538 | 49.152 MHz / 31.958 kHz ≈ 每输出样本的指令预算 |
| A1600/S200 采样率 | 31,958 Hz | `P_TimerA_Data=0xFCFF`（重载 769），`Fosc/2/769` |
| S600 单词采样率 | 36,790 Hz | 播放期间 `0xFD64`（重载 668） |
| 语速表 | `{06,09,0C,0F,12}` | 主控 `$FB16` 的表；**项目标准档 = 2**（值 0x0C，芯片默认） |
| 音量中点 | `0x8000` | DAC 无符号中点；固件 `$C41E` 默认也把它写回 0x8000 |
| UART FIFO | 64 字节 | 固件接收环；满了静默丢，必须一字节一手握 |
| 一个环 | 280 字 = 560 字节 | 固件暂存环容量，决定"一次最多发多少" |

---

## 10. 边界与未做的事（别误用）

* 不模拟 **flash 编程**（对 ROM 地址的写被忽略），因此不能做"固件自升级"类实验。
* ADC/PWM/EXT1 只有骨架或未建模；录音通路（`0x80..0x85`）没接通。
* `spce_tick()` 的周期只是**相对权重**，不要拿它算真实微秒。
* 端口只建模到"电平"这一层：没有上拉/驱动能力/开漏等电气特性。
* 音频环是**单声道**；SACM 的立体声/双 DAC 只是两条 `audio_push`。
* `vec_area` 需要宿主显式重建；忘了这一步的表现是"取指正常但一有中断就跑飞"。

## 11. 2026-09-26 全量排查记录（含发现的 bug 与修复）

排查范围：`emu/` 全部源码（`unsp.[ch]`、`spce061a.[ch]`、三个 `.py`、各 `main_*.c`）、
`tools/unspdis.py` 的注释表，以及与本机官方资料（`info/SPCE061A芯片资料/` 的手册、
上一线程 SDK 里的 `SPCE061A.h`、MAME 的 `unsp_fxxx.cpp`）逐项对照。

### 11.1 三个 `.py` 是什么

| 文件 | 作用 | 谁在用 |
|---|---|---|
| `unspcore.py` | µ'nSP **反汇编器**（移植 MAME 的 `unspdasm*.cpp`，ISA 1.0） | `tools/unspdis.py` ✓ |
| `unspemu.py` | µ'nSP **解释器**（只有 CPU 核：指令、标志位、SB/FIR_MOV） | 被 `unsp061.py` 引用 |
| `unsp061.py` | SPCE061A **SoC 模型**（外设/中断/端口，C 版的 Python 移植） | **本仓库没有脚本用** |

结论：只有反汇编器在服役；解释器+SoC 模型是"不依赖 C 工具链"的备用通路，
且比 C 版落后三处宿主侧能力（`ld_sets_flags`、`khz_on`、`rd_trace`）。

### 11.2 修复 1：`MULS` 的 `us/ss` 被当成同一种

* 固件里 `MULS` 共 805 条，其中 **us 编码（opcode 组 2/3）312 条**、ss（组 6/7）493 条
  （集中在 `0x8A00-0x9C00`，即 S600/S480 CELP 解码器）。
* 旧模型 `case 2: case 3: case 6: case 7: do_muls(..., 1)`——**把 us 也当有符号×有符号**。
* 依据：MAME `unsp_fxxx.cpp` 里 `MUL us`（已实现）明确是 `m_r[opa] * sign_extend(m_r[opb])`，
  即"**Rd 无符号 × Rs 有符号**"；`MULS us` 在 MAME 里是未实现桩，按同一约定补齐。
* 修复后 A/B（把修复前后各编一份二进制对拍）：

  | 通路 | 修复前 | 修复后 | 差异 |
  |---|---|---|---|
  | A1600 一致性 22 项 + 音频（只用 ss） | md5 `65f16e8205b9…` | 同 | **0 样点** |
  | S600 单词 60 帧（`nc3000_word2wav.ps1 0 60`） | md5 `03fcd6b01b45…` | 同 | **0 样点** |
  | MS01 整曲（音乐1，NAND 固件） | md5 `f236ff1d08f5…` | 同 | **0 样点** |

  即：修复是"潜在正确性"修复——当前素材里 us 站点的 Rd 最高位从未置位，所以听感/波形不变；
  换素材（Rd ≥ 0x8000）就会体现。Python 的 `unspemu.py` 已同步修复（另修正了它的
  FIR_MOV 移位方向，之前是旧版反向实现）。

### 11.3 修复 2：`tools/unspdis.py` 的 I/O 名称表大面积错标

旧表把 `0x7000-0x7006` 标成 UART/INT、`0x7015` 标成 `P_DAC1_DATA` 等——像抄了**别的芯片**的映射。
用官方 `SPCE061A.h` 配平后（本次已改）：

| 地址 | 官方名 | 旧注释（错） |
|---|---|---|
| 0x7000-0x7004 | P_IOA_Data/Buffer/Dir/Attrib/Latch | P_UART_DATA/CMD/BAUD/STATUS/CTRL |
| 0x7005/0x7006 | P_IOB_Data/Buffer | P_INT_CTRL / P_INT_STATUS |
| 0x7008/0x7009 | P_IOB_Attrib / P_Feedback | P_TIMERB_CTRL / P_TIMERB_DATA |
| 0x7010 | P_INT_Ctrl | P_INT_CTRL2 |
| **0x7015** | **P_ADC_Ctrl** | **P_DAC1_DATA**（就是它把上一轮排查带偏的） |
| 0x7016 / 0x7017 | **P_DAC2 / P_DAC1** | 0x7016 对、0x7017 缺 |
| 0x7020 | P_SIO_Stop | P_ADC_DATA（ADC 数据在 0x7014） |

手册出处：`info/SPCE061A芯片资料/SPCE061A编程手册.pdf`（= `SPCE040A/060A/061A
PROGRAMMING GUIDE v1.2`，102 页，正文可提取）§12/§13 明确
`P_ADC_Ctrl(R/W)($7015H)`、`P_DAC2(R/W)($7016H)`、`P_DAC1(R/W)($7017H)`，
且 DAC 数据是 **10 位无符号**（`PCM = DAC − 0x8000` 的取法是它推出的）。
本次把手册正文导出到 `out/061_programming_guide.txt`，以后查寄存器/时序直接搜它。

### 11.4 修复 3：`P_ADC_Ctrl` 读回 0（挂死风险）

手册：`P_ADC_Ctrl` 的 **b15 = RDY**（转换完成）。旧模型 `io_read` 没有 `0x7015` 分支，
返回 0 ⇒ **任何 poll RDY 的固件都会死循环**。现在返回 `adc_ctrl | 0x8000`
（A/D 是瞬时模型，永远"完成"）。三条已验收通路不受影响（同上表）。

### 11.5 复核过、确认没问题的地方

* 标志位语义：`update_nzsc()` 的 S 位 = `C_out ^ (sign(Rd) ^ sign(Rs))`，与
  "有符号小于"等价（`jge/jl/jg/jle` 用得上），逐例验算通过。
* `MUL` 的 us/ss 与 MAME 已实现版本逐行一致（`MUL us`：Rd 无符号、Rs 有符号）。
* 中断：FIQ 可抢占 IRQ、IRQ 互不抢占、`RETF` 只在栈回到中断入口位置时才释放层
  （`unsp.h` 的长注释），与实机行为一致；中断入口不动 `FIR_MOV` 也是必须的。
* 定时器：源码选择表 `src_a/b/c` 与手册 §6 的分频/固定频率对照一致；
  计数"从预置值向上溢出"的写法已在 S200 修复中验证。
* 宿主时钟：所有宿主都是 `spce_tick(&m, 1)`（1 指令 = 1 tick），因此
  `timer_frac += cycles * step` 不存在溢出风险；`khz_on` 的 1 kHz 时基默认关闭。
* `audio_push()` 的环索引掩码要求 `SPCE_AUDIO_RING` 是 2 的幂（头文件里是 0x2000），
  溢出时丢最旧样本并累加 `audio_overruns`，逻辑正确。

### 11.6 一次假警报：S200 试听里的"杂音"= 用了 9/23 的历史 exe

2026-09-26 晚，四路试听（A1600/S200/MS01/S600）交付后反馈"S200 又是老毛病：播放中有杂音"。
结论：**不是这次改的 bug，是 S200 那一路用了 `build\s200_probe.exe`**——它的时间戳是
**9/23 23:02**，早于 9/25 的 `SB` 中断现场保护修复（见 §2.3 / 《使用指南》§6 第 11 条），
等于把"FIQ 的 `MULS` 清掉内核正在用的 `SB` → LPC 滤波器发散 → 输出削顶"这个旧病原样搬了回来。

证据链（全部可复现）：

| # | 对象 / 命令 | 结果 |
|---|---|---|
| 1 | `out\试听_2_S200_辅音2.wav` vs 旧 exe 重跑的 `build\t_s200_old.wav` | md5 同为 `5219C07C…` ⇒ 交付的那份就是旧 exe 的产物 |
| 2 | 旧 exe 输出统计 | rms 5746、峰值 32768、满量程 1543、`|x|≥32000` 共 2933 个 |
| 3 | 用**当前源码**重编跑同一条命令（`0x10 2`，600 帧） | rms 479.5、满量程 **1**（直驱参照 rms 487.0 / 峰值 9180） |
| 4 | 当前源码跑文档里的 lrc_800 验收向量（§11.2 的那条，28 个参数） | `build\lrc800_new.wav` md5 `393F98D4…` **与 9/25 修复后的 `build\lrc800_v.wav` 逐字节相同**；`railing DAC writes: 1` |
| 5 | 当前源码重跑 A1600 一致性 + S600 300 帧 + MS01 整曲 | `22 passed, 0 failed`、md5 `65F16E82…` / `3134E3A2…` / `6FFAF098…` 全部可复现 |

第 4 条同时回答了"9/26 的 `MULS us/ss` 修复（§11.2）是不是动到 S200 了"：**没有**。
当前核心在 S200 通路上与 9/25 修复后的产物**逐字节一致** ⇒ 该改动在这条通路里 0 样点影响
（S200 内核 + 音频 FIQ 用的都是 `ss` 组）。

**教训**（已同步进《使用指南》§6）：听感/回归必须用**当前源码重新编译**的 exe；
`build\` 下的历史 exe（`s200_probe.exe`、`s200_probe2..Q.exe`、`fwstream.exe` 等）
会冻结当时的核心 bug。`build\s200_probe.exe` 已按当前源码重编覆盖（116224 B，与 `s200_probeC.exe` 同源）。
