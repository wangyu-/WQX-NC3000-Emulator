# V5100 (TLCS-900 / TMP92CH21) 编译工具

这里的脚本用 `ngpc.zip` 里的 Toshiba T900 工具链来编译 V5100 的 C 源码。

## 为什么需要脚本（原厂 CC900.EXE 在这台机器上用不了）

原厂 `CC900.EXE` 只是“驱动”：它把参数通过 **`-z@` + 环境块** 传给
`THC1/THC2/ASM900`。Windows 10/11 的 `CreateProcessA` 会校验环境块
（每个条目必须是 `NAME=VALUE`），而驱动塞进环境块的是一串纯参数
（例如 `" "`, `"C:\t9k_a1"`, `"-g"`, `"t.c"`），因此调用直接失败：

```
cc900-Fatal-116: cannot execute "…\T900\bin\thc1"
```

实测（Frida 挂钩 `CreateProcessA`）：返回 0，`GetLastError = 87`
（ERROR_INVALID_PARAMETER）。这与 ngpc 包里 `IMPORTANT.TXT` 提到的
“Windows 7 上跑不了、要退回 XP 虚拟机”是同一个根因。

`cc900.ps1` 直接按 2006 年原厂 TIDE 工程的实际调用方式依次执行三个阶段：

```
THC1    解析 C 源码     -> 中间文件 .i
THC2    代码生成        -> 汇编源码 .asm
ASM900  汇编            -> 可重定位目标 .rel
```

## 用法

```powershell
cd info\V5100\V5100code\IDE        # 相对 -I 路径以这里为基准
..\..\..\..\tools\v5100\cc900.ps1 `
    -Source ..\Gui_smulator\Application\aptest\demo.c `
    -Out    out\demo.rel `
    -Include .\include,..\Gui_smulator\mwingui,..\Gui_smulator\Application\src\pda\include `
    -Define  PIC_PID=1
```

参数：

| 参数 | 说明 |
|---|---|
| `-Source` | 要编译的 `.c`（必需） |
| `-Out` | 输出的 `.rel`，默认与源码同名 |
| `-Include` | 额外的头文件搜索目录（会自动加 T900\INCLUDE） |
| `-Define` | 宏定义，如 `GGV_FS`、`PIC_PID=1` |
| `-Codegen` | 传给 THC2 的优化选项，默认 `-O0` |
| `-NoDebug` | 不生成调试信息（不传 `-g`） |
| `-ToolRoot` | T900 根目录，默认 `..\ngpc\ngpc\T900` |

## 已知限制（踩过的坑）

1. **源码必须是 CRLF**：Toshiba 工具只认 DOS 换行，LF 会被判
   `ASM900-Fatal-152 : Illegal source file format`。脚本会自动把源码复制到
   临时目录并转成 CRLF（同时把原目录加进 `-I`，保证 `#include "xxx.h"` 能找到）。
2. **ASM900 最多 4 个 `-I`**：传第 5 个会直接崩溃（0xC0000005）。脚本对
   ASM900 只传前 4 个。
3. **`-O0` 只能给 THC2**：THC1 不认识 `-O0`（那是驱动级的选项）。
4. 汇编阶段的工作目录必须是输出目录：ASM900 把 `.rel` 写在当前目录。

## 编译后的链接 / 转换（需手动执行）

```powershell
$env:THOME = "$PWD\tools\ngpc\ngpc\T900"
$env:PATH  = "$env:THOME\BIN;$env:PATH"

# 链接（-lcf 用 SDK 的内存布局文件；c900ml.lib 是 SDK 自带的运行库）
TULINK.EXE info\V5100\V5100code\IDE\v6100.lcf -T900 info\V5100\V5100code\C900ml.lib `
    -la -o demo.abs demo.rel -$

# 转 S 记录
TUCONV.EXE -Fs24 -o demo.s24 demo.abs
```

> 注意：`-lcf`/`.lib`/`.rel` 的路径请按实际目录调整；原厂出厂链接命令可在
> `info\V5100\V5100code\IDE\output\v6100.map` 第 6 行看到。

## 出厂预编译目标文件（relfile\*.rel）的现状

`relfile` 里的 347 个 `.rel` 是原厂用 **T900 v4.70（ASM900 2.2i / TULINK 2.0l）**
生成的，本包里的 TULINK 是 1998 年的 **V2.0f**，其中 **31 个**读不了：

```
TULINK-Fatal-120: Bad object format in "relfile\winmain.rel"( 0x0000b28d )
```

也就是说：**用这套 1998 版工具链可以编译新代码，但无法重新链接出厂系统镜像。**
详细分析见 `docs\V5100_SDK编译说明.md`。
