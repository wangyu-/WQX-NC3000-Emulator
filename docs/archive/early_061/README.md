# 早期 SPCE061A 工作区（`Documents\Codex\2026-09-21\ce`）里留下来的报告

这一批是 **2026-09-21 / 09-22** 那轮"先啃 061.dat 本身"的产物，当时的工作区是
`C:\Users\YYGSM\Documents\Codex\2026-09-21\ce`（`outputs\` + `work\`）。那 1.3 GB
里绝大部分是 STM32F407/CMSIS 厂商树的重复副本、临时 wav 和脚本，2026-09-28 清理掉，
只把**报告和反汇编**搬进了这里（其余活的东西见下面"现在去哪找"）。

| 文件 | 内容 |
|---|---|
| `061_分析报告.md` | 总报告：固件格式判定、启动流程、中断系统、数据结构、遗留问题 |
| `061_启动流程与串口协议.md` | **复位 → 握手 → 主循环**的逐步对照表（固件在做什么 / 上位机要发什么），命令常量来自 `V6100_061.h` |
| `061_符号表与统计.md` | 指令使用频率、165 个子程序清单、I/O 与 RAM 访问表、数据区概况 |
| `061_声音格式与解码分析.md` | 061 声音数据的格式分析 |
| `MS01_播放器分析报告.md` | MS01 播放器"只响一声"的根因（1 kHz IRQ4 没触发）与修法 |
| `模拟器与播放器实施计划.md` | 当时的实施计划（已按后来 `spce061a/` 的实现走完，留作背景） |
| `061_disasm.asm` | **带标号/注释的完整反汇编**（0x8200–0xFFFF，1 万多行）。和仓库里工具现生成的 `out/061_disasm_full.txt` 不是同一份东西：这份是人工标注版 |
| `emulator_README_20260921.md` | 旧 `outputs\emulator\README.md`（40 KB）：**STM32F411/F407 移植**、MS01 定时/中断分工（"只响一声"的根因）、鼓点/ADPCM、流式协议、CubeMX 配置——这些只有这一份成文记录 |
| `tools\fw_smoke.c` | `061_启动流程与串口协议.md` 里说的"整条链路跑通"的那个宿主机工装 |
| `tools\mk_ms01_image.py` / `mk_ms01_drums.py` | 生成 `ms01_image*.c`（旧的 SDK 链接版 MS01 引擎）的脚本，留作可复现 |
| `tools\Makefile_20260921` | 旧树自己的 Makefile（现在的构建见 `spce061a\README.md`，用 TCC 或仓库 `src_nc3000\build.ps1` 那套 mingw） |

## 有意没搬的（删了不影响任何东西）

* `outputs\emulator\` 的其余文件 = 仓库 `spce061a\emu\` 的**旧快照**（同名文件逐个比过，仓库那份都更新）。
* `ms01_image.c` / `ms01_image_drums.c` / `ms01_lib.c`（共 1.2 MB）：项目**主动淘汰**的"SDK 示例引擎"方案，
  `spce061a\emu\main_linux_ms01.c` 顶部注释写了为什么不用它（现在跑真机固件）。想复现旧方案就用上面两个脚本重新生成。
* `work\txt\第*.txt`（教材 8 章文本）：仓库 `info\凌阳16位单片机应用基础\` 里有同一套 PDF；
  `work\ref\`、`work\p2\` 同样是 `info\` 里已有的书/例程副本。
* `work\fw_*.c`、`unsp_test.c`、`unsp_dbg.c`、`_*.py`、`ms01\ev_*.txt` 等：`spce061a\host\` 那批探针的前身或分析中间产物。
* `work\sacm_d10\`（SACM_D10 工具包，15 MB）：用户明确说不需要；它的结论已经写进本目录的
  `061_声音格式与解码分析.md`。
* `work\gpce063\`（厂商 demo 包，9.5 MB / 607 文件）：整包删掉，但**仓库文档引用到的三样样本**
  已经挑出来放在 `spce061a\ref\gpce063_samples\`（S200 语音样本、A1600 样本、MS01 鼓点 20 个 ADP）。

## 现在去哪找（别再用旧路径）

* **TCC 编译器**（这些报告里编译宿主机探针用的）：`toolchain\tcc\tcc.exe`（2026-09-28 搬进仓库）
* **MAME 的 unSP CPU 内核源码**（写新探针/对照语义用）：`spce061a\ref\mame_unsp\`
* 活文档：`docs\NC3000_DSP检测与升级.md`、`docs\SPCE061A模拟器_代码解析.md`、
  `docs\SPCE061A模拟器_使用指南.md`、`spce061a\docs\NC3000-061协议对照.md`
* 当时的完整对话记录（导出）：`spce061a\docs\raw_spce061a_thread_export.md`
