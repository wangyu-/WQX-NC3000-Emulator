# WQX NC3000 模拟器（NC2000 模拟器移植版）

把 wangyu-/NC2000 的模拟器源码改造成能跑 **NC3000 官方固件**（含真机 NAND 数据）的模拟器。

> **两个仓库**：语音 DSP（SPCE061A）已经拆成独立仓库，本仓库只保留 6502 主控这一侧。
> 本仓库编译时需要一个 `spce061a/` 目录（里面是那个独立仓库的代码）：
>
> ```powershell
> git clone https://github.com/yao90s/WQX-SPCE061-Emulator spce061a     # 简单做法
> # 或者：git submodule add -f https://github.com/yao90s/WQX-SPCE061-Emulator spce061a
> #        （同时把 .gitignore 里 "spce061a/" 那行删掉）
> ```
>
> `spce061a/` 里的芯片模拟器、播放器、主机探针与文档见那个仓库的 README。

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
