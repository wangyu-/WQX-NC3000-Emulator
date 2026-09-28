# WQX NC3000 模拟器（NC2000 模拟器移植版）

把 wangyu-/NC2000 的模拟器源码改造成能跑 **NC3000 官方固件**（含真机 NAND 数据）的模拟器。

> **两个仓库**：语音 DSP（SPCE061A）是独立仓库，通过 **git submodule** 挂在 `spce061a/`
> （[yao90s/WQX-SPCE061-Emulator](https://github.com/yao90s/WQX-SPCE061-Emulator)，见 `.gitmodules`）。
>
> ```powershell
> git clone --recursive https://github.com/yao90s/WQX-NC3000-Emulator    # 连子模块一起拉
> # 已经 clone 过的：git submodule update --init --recursive
> ```
>
> 子模块里**不含 061 固件**（凌阳版权物）：把你自己 dump 的 `061.dat` 放进 `spce061a/rom/`，
> 再生成 C 数组，`src_nc3000` 才编得过：
>
> ```powershell
> node spce061a\tools\mk_fw_c.js spce061a\rom\061.dat spce061a\emu\firmware_061.c firmware_061
> ```
>
> 芯片模拟器、播放器、主机探针与芯片文档见那个仓库的 README。

* **文档入口：[docs/README.md](docs/README.md)**（现状 / 快速上手 / 结论速查 / 文档地图）
* **最新交接文档：[docs/交接文档_2026-09-27_09-28.md](docs/交接文档_2026-09-27_09-28.md)**（本轮做完什么 / 还剩什么 / 踩过的坑）
* 编译：`cd src_nc3000; .\build.ps1`（GUI）、`.\build.ps1 -Headless`（无窗口调试台）
* 32 位 WinXP 版：`.\build.ps1 -XP` → `nc3000_xp.exe`（i686 + msvcrt + SDL2 2.0.22），双击 `run_nc3000_xp.cmd` 启动
* 运行：双击 `run_nc3000.cmd`，或 `src_nc3000\nc3000.exe`（ROM 会自动探测）
* 加文件到虚拟盘：`powershell -NoProfile -File tools\nc3k_put.ps1 -Rom roms\nc3000_dl -File <主机文件> -Name "/midi/音乐1.mid"`
* **本仓库不含 ROM**：NC3000 的 NOR/NAND 是官方版权物，请用自己机器 dump 出来的文件放进 `roms\`
  （见 `tools\nc3000_rom_prep.ps1`）。构建依赖（mingw/SDL2）用 `toolchain\download.ps1` 拉取。

主要目录：`src_nc3000/` 模拟器源码、`tools/` 分析调试脚本、`docs/` 文档（历史在 `docs/archive/`）、
`roms/`（自备 ROM）、`spce061a/`（另一个仓库，见上）、`info/` 原始资料（本地参考，不进仓库）。

许可：上游 wangyu-/NC2000 是 **GPL-3.0**，本仓库作为衍生作品同样以 GPL-3.0 发布（见 `LICENSE`）。
