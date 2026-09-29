# WQX NC3000 模拟器（NC2000 模拟器移植版）
本项目是使用DeepSeek-V4.1-Flash模型，基于 [wangyu-/NC2000](https://github.com/wangyu-/NC2000) 制作而成，
支持运行从真机提取的完整固件，并带有声音支持。仓库中的文档全部是AI生成，写的可能比较乱，本人打算近期抽空整理，
别的没啥可介绍的了…………

## 需要注意
目前声音部分还存在很大缺陷，已测试的三台电脑中，只有一台能够流畅运行：
1、ROG幻X 2021版  I9-12900H  声音部分流畅
2、MACBOOK M1款  声音拖慢
3、台式机 I7-7700  声音严重拖慢

## 模拟器特点
通过对SPCE061核心的模拟，使得模拟器可以完整支持真机语音及音乐格式。目前已测的格式包括：
* A1600 - 歌曲音乐格式（.a16）、视听教材中的高品质格式（.vnt）
* MS01  - 和弦音乐格式（.mid）
* S600  - 单词发音
* S200  - 视听教材中的一般音质（.vnt）

## 当前模拟器存在的问题
* 屏幕两侧的小图标还是NC2000的，待更换
* 不支持热键唤醒
* 061模拟部分没有做针对061片内FLASH的擦写模拟，所以x+on/off进工厂模式菜单，其中的更新061固件功能目前无法使用
* 录音功能没有实现

## 编译方法
> ```powershell
> git clone --recursive https://github.com/yao90s/WQX-NC3000-Emulator    # 连子模块一起拉
> # 已经 clone 过的：git submodule update --init --recursive
> ```
> 需要从真机提取 DSP 固件（**出厂 283D 那份，64 KB**）放进 `spce061a/rom/061.dat`，
> 再生成 C 数组，`src_nc3000` 才编得过：
>
> ⚠️ 注意别拿 NAND 里的 `sysdir/升级061.bin` 改名顶替：那是 **284F 的升级包**，
> 而且是"待烧写映像"（校验区前一对是全 `0000`）+ 交织存放。真机跑的是出厂 283D
> （自测读数 `f7 10 3b 30` / `3d 28` 就是它）；用 284F 的话自检 `BB 0B` 会回 `BB FC`。
>
> ```powershell
> node spce061a\tools\mk_fw_c.js spce061a\rom\061.dat spce061a\emu\firmware_061.c firmware_061
> ```

* 编译：`cd src_nc3000; .\build.ps1`（GUI）、`.\build.ps1 -Headless`（无窗口调试台）
* 32 位 WinXP 版：`.\build.ps1 -XP` → `nc3000_xp.exe`（i686 + msvcrt + SDL2 2.0.22），双击 `run_nc3000_xp.cmd` 启动
* 运行：双击 `run_nc3000.cmd`，或 `src_nc3000\nc3000.exe`（ROM 会自动探测）
* 加文件到虚拟盘：`powershell -NoProfile -File tools\nc3k_put.ps1 -Rom roms\nc3000_dl -File <主机文件> -Name "/midi/音乐1.mid"`
* **本仓库不含 ROM**：请自行从网络搜索下载运行所需的NOR和NAND数据，文件放进 `roms\`
  （见 `tools\nc3000_rom_prep.ps1`）。构建依赖（mingw/SDL2）用 `toolchain\download.ps1` 拉取。

主要目录：`src_nc3000/` 模拟器源码、`tools/` 分析调试脚本、`roms/`（自备 ROM）、`spce061a/`（子模块）。

许可：上游 wangyu-/NC2000 是 **GPL-3.0**，本仓库作为衍生作品同样以 GPL-3.0 发布（见 `LICENSE`）。
