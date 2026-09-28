# AGENTS.md — WQX_NC3K_SYS（NC3000 模拟器）

把上游 wangyu-/NC2000 模拟器改造成能跑 **NC3000 官方固件 + 真机 NAND** 的模拟器。
本文件是常驻约定（每次新会话自动读），**详细档案在 `docs/`**。

## 先读什么（按顺序）

1. `docs/README.md` —— 总入口：现状表 / 快速上手 / 结论速查 / 文档地图
2. `docs/交接文档_2026-09-27_09-28.md` —— 最近一轮交接：做完什么 / 还剩什么 / 踩过的坑
3. `docs/NC3000模拟器_改造计划.md` —— 开发日志，按轮次追加（2026-09-28 已到**第 25 节**）

> **2026-09-28 起分成两个仓库**：语音 DSP（SPCE061A）拆到
> [yao90s/WQX-SPCE061-Emulator](https://github.com/yao90s/WQX-SPCE061-Emulator)，
> 本地 clone 在 `spce061a/`（在本仓库 `.gitignore` 里；编译 `src_nc3000` 要用到它）。
> 本仓库是 [yao90s/WQX-NC3000-Emulator](https://github.com/yao90s/WQX-NC3000-Emulator)。

## 硬约束（弄错要重跑很多东西）

* 主控主频 **14.7456 MHz**（`comm.cpp` 的 `CYCLES_MS = 14745`）；061 = **49.152 MHz**，
  主控给 061 的指令预算是 **×10/3**（`N3_INSN_PER_SEC = 49152000`）。
* 061 默认固件是 `spce061a/rom/061.dat`（**283D，真机同款**；真机自测读数 `f7 10 3b 30` / `3d 28`）。
  NAND 里的 `升级061.bin`(284F) 只是随机器带的升级包，`NC3_DSP_NAND=1` 才切过去。
* **纪律（改造计划 §22.9）**：不许靠改 061 映像、或改自检流程，把自测的 ERR“修”成 OK。
  `NC3_DSP_FIX_HEADER` 默认关闭就是这个意思。
* `obj/`(x64) 与 `obj_xp32/`(32 位) **不能混**；`tools/*.ps1` 必须存成 **UTF-8 with BOM**。
* 本环境**禁止 `Remove-Item`**（命令会被策略直接拦）；要清理文件请让用户手动删。
* `curl` 要加 `--ssl-no-revoke`；`developers.openai.com` / `platform.openai.com` 在本机是 403（查不了官方文档）。

## GitHub（本机怎么推）

* **直连 GitHub 会被重置**：`git push` 报 `Recv failure: Connection was reset`。
  本机装了 Clash Verge，mixed 端口是 **`127.0.0.1:7897`**，push 时带上代理：

  ```powershell
  git -c http.proxy=http://127.0.0.1:7897 -c https.proxy=http://127.0.0.1:7897 push
  ```

* 第一次会弹 **Git Credential Manager** 的浏览器授权（系统级已配 `credential.helper=manager`），
  在浏览器里点一下授权即可，之后免登录。

## 常用命令

```powershell
cd src_nc3000
.\build.ps1              # nc3000.exe            (x64 GUI)
.\build.ps1 -Headless    # nc3000_headless.exe
.\build.ps1 -XP          # nc3000_xp.exe         (32 位 / WinXP)
.\build.ps1 -XP -Headless
```

* 运行：双击 `run_nc3000.cmd` / `run_nc3000_xp.cmd`；独立绿色包在 `..\nc3k_emu`（两个 cmd 全按 `%~dp0` 定位）
* 装 ROM：`powershell -File tools\nc3000_rom_prep.ps1`；往虚拟盘加文件：`tools\nc3k_put.ps1`
* 进**系统自测菜单**：先关机 → 按住 **W+V** 再按 ON/OFF（GUI 是按住 W、V 再按 F12；
  headless 用 `--hold 600`，短了 W 会先松开）。菜单里**直接按数字键**运行某项（`9` = SPCE061 检测）
* 查“某个自检项到底过没过”：`--cmd-at <ms> "call1 <idx> <bank>"` + `--dump-ram 0x3F0 1 out.bin`（1 = 成功）
* GUI 抓日志必须加 `--no-console`（否则 stdout 被 `AllocConsole` 抢走）；音频队列看 `NC3_SND_DEBUG=1`
* 分析音质用 `--dump-061`（061 原始 DAC 流）；`--dump-audio` 是离线混音泵，长曲子自己会丢样

## 当前挂起 / 待确认

* ~~自测第 9 项 `SPCE061检测` 显示 ERR~~ **2026-09-28 已解决**：根因是主控 `IO 0x0E` bit3
  （061 复位线）从来没建模 —— BIOS 在自测项开跑前会拉低再放开这根线，真机那颗 061 因此重跑
  “复位路径”（`$821A` 握手 → `$821F` peek），紧接着的 `CC 0F` 才回 `CC FF`。已按硬件行为补上
  （`src_nc3000/io_new.cpp` 的 `case 0x0e`），自检现在通过，读数 `f7 10 3b 30` / `3d 28` 与真机一致。
  细节与回归见改造计划 **第 25 节**。
* INI 配置文件（像 DOSBox 那样）：用户说先不加，等他想好需求。

## 工作方式（用户偏好）

* 用**中文**回答；结论要**带证据**（截图 / 字节流 / 日志 / 实测数字），不要只给推理。
* 改完要**跑回归**：开机进菜单、词典发音、华容道 BGM、音乐整曲（这几条都踩过坑）。
* 重要结论**当场写进 `docs/`**（聊天会被压缩、换 chat 会丢，文件不会）；阶段性收尾
  **追加一节**到 `改造计划`，不要重写历史。
