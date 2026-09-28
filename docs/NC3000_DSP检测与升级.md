# NC3000 的 DSP(SPCE061A) 版本检测 与 DSP 固件升级 —— 固件里的实现

分析对象：`info/NC3KSYSNOR.nor`（BANK0 窗口 `0x8000-0xFFFF`，BANK1+ 窗口 `0x4000-0xBFFF`）、
`info/NC3KSYSNAND.nand`（`sysdir/升级061.bin`）、`spce061a/rom/061.dat`。
工具：`tools/dis6502.js`、`tools/nor_find.js`、`tools/nor_scan.js`、`tools/nand_deinterleave.js`、
`tools/fw_trace.js`、`tools/fw_align_map.js`、`tools/mk_fw_c.js`。

---

## 0. 结论速览

两个功能确实都在 **BANK0 的菜单里**，真正的实现代码在 **BANK16**，靠 BRK/INT 跳过去：

| 功能 | BANK0 菜单文字 | BANK0 位置 | 处理器 | INT | BANK16 实现 |
|---|---|---|---|---|---|
| DSP **版本测试** | `9.SPCE061检测` | `$9247` 起（第 9 项） | `$92FA` | `00 04 10` = INT `$1004`（bank16 idx4） | `$7335` |
| DSP **固件升级** | `4Upgrade 061` | `$804A` 起（第 4 项） | `$8DDC` | `00 08 10` = INT `$1008`（bank16 idx8） | `$8193` |

顺带挖到的第 3 个 DSP 相关功能：系统测试菜单第 4 项 `4.录放音检测` → BANK0 `$92F4` →
INT `$1005`（bank16 idx5）→ BANK16 `$775F`（送 `0xAA 0x00` 后等 `0xAA` 回声）。

---

## 1. BANK0：系统测试菜单（13 项）与派发表

菜单字符串（GBK，`0xFF` 分隔）在 **BANK0 `$9247`**：

```
1.自动检测 2.液晶检测 3.键盘检测 4.录放音检测 5.内存检测 6.Bus检测
7.Nand检测 8.Nandsys检测 9.SPCE061检测 10.Battery 11.irda 12.自动演示 13.自动加pid
```

字符串前面紧挨着一张 **13 × 4 字节的派发表**，从 `$9213` 开始，每项 = (2 字节处理器地址)(`68 9D`)：

```
$9213: de 92  ->  1.自动检测      -> $92DE（顺序调用下面几项）
$9217: 9a 93  ->  2.液晶检测      -> $939A
$921B: 2a 97  ->  3.键盘检测      -> $972A
$921F: f4 92  ->  4.录放音检测    -> $92F4   INT $1005 -> bank16 $775F
$9223: 1d 99  ->  5.内存检测      -> $991D   INT $1007 -> bank16 $7F4E
$9227: 21 99  ->  6.Bus检测       -> $9921
$922B: 62 9b  ->  7.Nand检测      -> $9B62   INT $0705
$922F: 66 9b  ->  8.Nandsys检测   -> $9B66   INT $0720
$9233: fa 92  ->  9.SPCE061检测   -> $92FA   INT $1004 -> bank16 $7335   ★
$9237: 09 93  -> 10.Battery       -> $9309
$923B: 57 93  -> 11.irda          -> $9357   INT $100C -> bank16 $8A35
$923F: 49 9c  -> 12.自动演示      -> $9C49
$9243: dd 92  -> 13.自动加pid     -> $92DD（只有一个 RTS，空实现）
```

序号与文字的对应关系不是猜的：每一项显示的画面文字都能在 BANK0 的字符串块里对上
（`$9C81`=`demo`、`$9C95`=`Curr Battery`、`$9CA9`=`IRDA TEST`、`$9CC0`=`IRDA OK` …），
第 10/11/12 项与菜单文字 `10.Battery / 11.irda / 12.自动演示` **逐项吻合**。

菜单本身的入口是 **INT `$000E`**（bank0 `$801C` 的 INT 表项指向 `$9207`）。

### 1.1 `9.SPCE061检测` 的 BANK0 壳子

```
$92FA: 00 04 10      ; INT $1004 -> bank16 idx4 = $7335
$92FD: 20 2A E0      ; JSR $E02A（延时）
$9300: 20 2A E0
$9303: 20 2A E0
$9306: 4C 7E 93      ; JMP $937E（公共收尾）
```

### 1.2 BANK16 `$7335`：真正干活的版本/校验测试

```
$7335: LDX #$64 : 把 $76C0 起 100 字节搬到显示缓冲 $02BF  ; "SPCE061 SRAM TEST: ..."
$7340: JSR $75C3                                          ; 清屏/刷新（走 INT $C719，8KB 映像 $C7 的文本库）
$7344..$734A: JSR $E0B1($F951) / $E096($F9CE) / $E09F($F988)   ; 端口与 FIFO 准备
$734F: 延时 255 × JSR $7603
$7357: JSR $E093($FABE) 等 061 的 IOB14 变低（就绪）
$7362: JSR $E0AB($F9E9) 发 0xBB 0x0A，等 0xBB 0xFA       ; 链路握手
$736D: JSR $76A4        发 0xCC 0x0F                     ; SRAM 测试命令
$7387: 收字节 -> 期望 0xCC，接着期望 0xFF                ; == "$CC $FF"
$73A3: 发 0xCC 0x01                                       ; 读 SRAM
$73DC: 根据结果打印 $7738 "SPCE061 SRAM ok!!" / $7725 "SPCE061 SRAM ERR!!"
$73EC: 打印 $7640 "SPCE061 Check_Sum:            Version:"
$7428: JSR $75AD 发 0xBB 0x0B（DATA_CHK）
$74B0: 收 2 字节，必须 = 0xBB 0xFB（否则 JMP $7538 出错）
$74C6: 再收 4 字节 -> $0B00/$0B01（= [0x8200]）与 $0B02/$0B03（= [0x8201]）
$74FB: 发 0xBB 0x00
$751C: 收 2 字节 -> $0B04/$0B05（= [0x8204] = 版本字）
$7549: 把 0x0B00..0x0B05 以十六进制填进画面，返回 C=1
```

也就是说 **“DSP 版本测试”= `0xBB 0x0A` 握手 → `0xBB 0x0B` 取 Check_Sum → `0xBB 0x00` 取版本**，
和 BANK0 菜单上显示的 `Check_Sum:` / `Version:` 完全对应。
相关字符串（BANK16）：`$7619` `SPCE061 ERR!!`、`$762D` `SPCE061 OK!!!`、
`$7640` `SPCE061 Check_Sum: … Version:`、`$76C0` `SPCE061 SRAM TEST:`、
`$7725` `SPCE061 SRAM ERR!!`、`$7739` `SPCE061 SRAM ok!!`。

---

## 2. BANK0：工厂升级菜单（6 项）与 `Upgrade 061`

这块菜单在 **BANK0 `$804A`**（就是 BANK0 开头、紧跟在 INT 表后面）：

```
1Send nand data all   2Send sst from bank1   3Send nand data part
4Upgrade 061          5Receive Data          6Send sst from bank0
```

字符串前面同样是 4 字节一格的表（`$8030` 起，后面接常量 `$8FC6` = 画面标题 `===== 系统复制 =====`）：

| 项 | 处理器 | 说明 |
|---|---|---|
| 1 Send nand data all | BANK0 `$85BD` | 整片 NAND 经串口送 PC |
| 2 Send sst from bank1 | BANK0 `$80C8` | 置 `$6E=1` 后进 `$80CC` |
| 3 Send nand data part | BANK0 `$8D0E` | 按输入的起止地址送 NAND |
| **4 Upgrade 061** | **BANK0 `$8DDC`** | **INT `$1008` → BANK16 `$8193`** ★ |
| 5 Receive Data | BANK0 `$80B8` | INT `$C023` |
| 6 Send sst from bank0 | BANK0 `$80C1` | 置 `$6E=0` 后进 `$80CC` |

```
$8DDC: 20 30 8F   JSR $8F30     ; 关链路/复位 061 的串口侧（$F951/$F988/$F9CE + 0xBB0A + 0xAA01…）
$8DDF: 78         SEI
$8DE0: 00 08 10   INT $1008     ; -> bank16 idx8 = $8193
$8DE3: 60         RTS
```

### 2.1 BANK16 `$8193`：DSP 固件升级的实际流程

```
$8193: 清进度计数器 $0B64；显示 $8624 起 100 字节 "updating.... DSP flash!!"
$81A3: JSR $8487（清屏）
$81A6: 把 $8724 起 20 字节（文件名区）搬到 $088C
$81C8: 3×256 次 JSR $E0BA（延时）
$81D5: JSR $E093($FABE) 等就绪；JSR $E0AB($F9E9) 发 0xBB 0x0A 等 0xBB 0xFA
$8209: JSR $8433 = 发 0xCC 0x0F  -> 期望回收 0xCC 0xFF    （061 进入烧写/擦除）
$824F: JSR $8401 = 发 0xCC 0x0A  -> 期望回收 0xCC 0xFA    （准备收数据）
$827E: JSR $8410（$3A/$3C/$3D 端口切换到数据通道）
$8281..$8293: 7 次延时；显示 $8549 起 20 字节 "Sending Data ..."
$82A9: $0B6A = 0xF4 = 244                                        ; ← 记录数
     循环：INC $0B64；JSR $849A（显示进度）；JSR $83EB（从文件取下一块 256 字节到 $3000）
           JSR $833E（发 32 次 × 8 字节）；DEC $0B6A；BNE
$82C5: JSR $83EB / $837C（收尾），出错则打印 $8585/$8598/$85A9
        "end DSP error" / "DSP Init error" / "DSP trans restar"
```

单块发包用的是 **`0x22 <长度> <数据>`**（`$8458`：`LDA #$22` → 发；发长度；发 N 字节；延时），
和 BANK0 里 `$FCB6` 那条 STREAM_CMD1 是同一个命令码。

**传输总量 = 244 × 256 = 62464 字节 = 0xF400**，正好是 061 内部 flash 的
`0x8200-0xFBFF` 这一段（(0xFC00-0x8200)×2 = 0xF400 字节）。
也就是 **DSP 升级只重写 0x8200-0xFBFF，0xFC00-0xFFFF 的向量区不动**。

固件来源文件名（BANK16 `$8725`）：**`/sysdir/升级061.bin`** —— 就是 NAND `sysdir` 里 inode 38 那个文件。
另外 `$8449` 还有一个只发 `0xCC 0x0B` 的小工具，升级收尾/校验时使用。

---

## 3. NAND 里的 `升级061.bin`：真实结构

先前按“文件大小 = 块数 × 16KB”截出来的是**错的**（那样截出来的映像在模拟器里跑不起来）。
实测结构如下：

* 物理页 = **512 字节数据 + 16 字节 OOB**（528 字节），OOB 形如
  `ff ff ff ff ff ff XX YY ZZ aa WW ff ff ff ff ff`。
* 文件数据起点 = **NAND 物理偏移 `0x323DC00`**；
  `逻辑偏移 x ↔ 物理 0x323DC00 + x + 16*floor(x/512)`。
  这条公式在 `x = 0x400 … 0x5000` 上逐点验证（每次 0x400 步进、物理步进恒为 0x420）。
* 文件内容 = **62464 字节（0xF400）**，正好 122 个物理页 —— 与升级程序送出的 244×256 完全一致，
  也解释了为什么它占 4 个 16KB 块（62464 ≤ 65536）。

去交织工具：`tools/nand_deinterleave.js`（产物 `out/升级061_denand.bin`）。
用它生成的 `spce061a/host/firmware_061_nand.c` 现在**能启动、能握手**：

```
固件: 升级061.bin (NAND)
  启动到 0x823E: OK
  链路握手 0xBB0A -> 0xBBFA OK
  0xBB0B 版本查询应答: BB FC EB 95 6A 30
  [BB00] 版本字: 4F 28 -> 0x284F
```

---

## 4. 两份 061 固件对比（哪份更新）

两份文件的**前 10 字节都是 ID 头**，结构 = [Check_Sum 低][Check_Sum 高][同样的两字][版本]：

| | 0x8200 / 0x8201（Check_Sum） | 0x8202 / 0x8203（副本） | **0x8204（Version）** |
|---|---|---|---|
| `061.dat`（`spce061a/rom/`，64KB） | `10F7` `303B` | `10F7` `303B` | **`283D`** |
| `升级061.bin`（NAND sysdir，62464 B） | `0000` `0000` | `793B` `3131` | **`284F`** |
| `a1600_image.c`（同内核的另一份） | `10F7` `303B` | `10F7` `303B` | `283D` |

* 061 侧的检查就是 **`[0x8200]==[0x8202] && [0x8201]==[0x8203]`**（`0xCF65` 起，
  一致回 `0xBB 0xFB`，不一致回 `0xBB 0xFC`）。
  `061.dat` 两对相等 → 校验通过；NAND 那份第一对是 **`0000 0000`**（空的占位），
  第二对是 `793B 3131` → 校验不过（实测回 `BB FC`）。
  这说明 NAND 里那份是**“待烧写的升级文件”**（校验区留空），
  而 `061.dat` 更像是**“烧好之后/发布版”的镜像**（校验区已填）。
* **代码不是同一份**：把两份对齐后，前面 21KB 只有零星几十字节差异（版本差异），
  到逻辑偏移 `0x5400` 处出现 **+50 (0x32) 字节** 的整体位移，`0x9300-0x9B00` 之间又多了
  **~48 字节**，也就是 NAND 那份在 `0x9B00` 之前多出约 **98 字节代码**
  （`tools/fw_align_map.js`；`0x5400` 之后按 +0x32 对齐后单页吻合度从 25% 回到 95%+）。
* **版本字不同**：`061.dat` = `0x283D`，NAND 那份 = `0x284F`。两者高字节同为 `0x28`，
  只有低字节 `0x3D(61) → 0x4F(79)` 不同。

**判断：NAND 里那份（版本字 `0x284F`、代码多约 98 字节）比 `061.dat`（`0x283D`）新。**
依据是两条互相独立的信号都指向同一方向：版本字更大 + 代码更多（典型的“后一版加了东西”）。
严格意义上要 100% 确认，还差一个第三者（再来一份不同版本，或机器上 `9.SPCE061检测`
显示的实际数字）；目前的证据我按“NAND 那份更新”下结论。

---

## 5. 复现命令

```powershell
# 结构确认
node tools/dis6502.js info/NC3KSYSNOR.nor 1250 12e0 8000     # 测试菜单文字
node tools/dis6502.js info/NC3KSYSNOR.nor b3 200 8000         # 升级菜单与派发
node tools/nor_find.js str "SPCE061" "Upgrade 061"            # 字符串所在 bank/地址

# NAND 里的 061 文件（去交织）
node tools/nand_deinterleave.js 0x323DC00 0x10000 spce061a/rom/061.dat out/升级061_denand.bin
node tools/mk_fw_c.js out/升级061_denand.bin spce061a/host/firmware_061_nand.c firmware_061_nand

# 用真实 061 固件跑“版本测试”这条命令
cd spce061a
tcc -O2 -I emu -o build/probe_ref.exe  -DREF_FW host/nc3000_dsp.c host/dsp_probe.c emu/spce061a.c emu/unsp.c emu/firmware_061.c
tcc -O2 -I emu -o build/probe_nand.exe           host/nc3000_dsp.c host/dsp_probe.c emu/spce061a.c emu/unsp.c host/firmware_061_nand.c
build/probe_ref.exe ; build/probe_nand.exe
```

---

## 6. 模拟器侧：`9.SPCE061检测` 报 "SPCE061 ERR!" —— 三个根因（2026-09-28 全部解决）

> **状态（2026-09-28）**：本节列的根因一、根因二在 09-27 修掉之后**自检仍旧 ERR**，
> 真正的最后一块拼图是**主控的 061 复位线 `IO 0x0E` bit3 没建模**（见下面第 3 条与 **§7**）。
> 补上之后自检第 9 项在模拟器里**通过**，读数 `Check_Sum f7 10 3b 30` / `Version 3d 28`
> 与真机一致。完整过程见 `NC3000模拟器_改造计划.md` 第 22 节与**第 25 节**。

真机的自测菜单（**W+V+ON/OFF** 打开）第 9 项走的就是本文 §1.1 那条链路：
`BB 0A` 握手 → `CC 0F`/`CC 01` SRAM 测试 → `BB 0B` 数据校验 → `BB 00` 版本。
模拟器里它一直报 ERR，最后查出是两件事叠加（完整过程见
`NC3000模拟器_改造计划.md` 第 22 节）：

1. **NAND 里的 `升级061.bin` 是"待烧写映像"，校验和标头没填**。
   284F 那版固件在 `BB 0B` 里是**自己现算** 32 位字和再与 `[0x8202]/[0x8203]` 比：
   `Σ[0x8000..0x81FF] + Σ[0x8204..0xFBFF] + Σ[0xFFF5..0xFFFF]`（不含标头本身）。
   文件里 `0x8200/0x8201` 是留给烧写时填的 `0000`，而 `0x8000-0x81FF` 这一段
   （升级不重写、所以不在文件里）模拟器读出来是全 0 ⇒ 算出来必然对不上。
   → `boot_fw()` 现在按固件自己的算法把字和重算并写回两对标头（`0xFFFF` 要按 0 算，
   模拟器里它是 SACM 用的 shadow 寄存器）。
2. **主控等回包只等 ~14 ms，而我们的 061 被 `gate=0` 的 3000 步/片限速拖成 ~37 ms**，
   于是主控重发 `BB 0B`，队列里堆了几份回包，后面每一步都读到陈旧字节。
   → 新增"活跃窗口"：真正和 061 通信过的 150 ms 内不限速。改完主控一次发完，061 在 13 ms 内回包。
3. **主控给 061 的复位线 `IO 0x0E` bit3 从来没建模**（2026-09-28 补）——BIOS 在自测项开跑前
   会把这个 bit 拉低再放开，真机那颗 061 因此重跑"复位路径"，紧接着的 `CC 0F` 才是
   "复位后第一条命令"、才回 `CC FF`。模拟器把这次写当成普通端口值存了丢掉，061 一直停在
   主循环 ⇒ 恒回 `CC 0F` ⇒ ERR。详见 **§7**。

验证（`call1` 是这轮新加的调试命令：只发一次 INT 并把返回进位写到 `$03F0`）：

```powershell
src_nc3000\nc3000_headless.exe roms\nc3000 --ms 33000 --hold 150 --press 4000 5 6 `
    --cmd-at 30000 "call1 04 10" --dump-ram 0x3F0 1 out\result.bin
# $03F0 = 0x01（进位=1）→ 自检成功；画面：Check_Sum "eb 95 6a 30"、Version "4f 28"
```

> 两版固件在这件事上的差别值得记住：`061.dat`(283D) 只比较 `[0x8200]/[0x8201]`
> 与 `[0x8202]/[0x8203]` 两对是否相等（它两对相同，所以一直 OK）；
> `升级061.bin`(284F) 是另外一份代码，会**现算字和**。所以"换哪份固件"会直接改变自检结果。

---

## 7. 主控 ↔ 061 的**复位线**：`IO 0x0E` bit3（2026-09-28 补）

前面几节讲的都是 UART（`0x3A`–`0x3D`）上的协议。但主控和 061 之间还有一条**不在串口上**的
控制线，不理解它就解释不了"自测第 9 项为什么真机 OK、模拟器 ERR"：

| 主控端口位 | 方向 | 含义 |
|---|---|---|
| `IO 0x0E` bit4 | **输入** | 061 的 ready/busy 线（061 侧是 `P_IOB bit14` 输出）：1 = 忙，主控要发字节前先等它变低 |
| **`IO 0x0E` bit3** | **输出** | **061 的 `/RESET`（低有效）**：拉到 0 = 按住复位，放回 1 = 放开、061 从复位向量重新跑固件 |

### 7.1 BIOS 里的复位脉冲代码

bank0，文件偏移 `$7970` / `$797A`（运行时 `$F972` / `$F97C`）：

```asm
LDA $0435 / AND #$F7 / STA $0E     ; bit3 = 0  ⇒ 拉住 061 复位
JSR $FB73                          ; 延时
LDA $0435 / ORA #$08 / STA $0E     ; bit3 = 1  ⇒ 放开复位（061 重跑固件）
STA $0435                          ; 记回影子寄存器 $0435
```

同一族还有 `$F951 / $F988 / $F9CE`（工厂升级流程 `$8F30` 里调的"关链路 / 复位 061 串口侧"，
见本文 §2.1）。

### 7.2 什么时候会脉冲

用 headless 的端口写日志 `--io-watch 0x0e` 实测，一次"关机 → `W+V+ON/OFF` 进自测 → 按 `9`"：

```
[io] t=26018ms  pc=$F972  $0E <- 00      ← 关机
[io] t=26018ms  pc=$F97C  $0E <- 08
[io] t=30341ms  pc=$F972  $0E <- 00      ← W+V+ON/OFF 再开机
[io] t=30341ms  pc=$F97C  $0E <- 08
[io] t=33563ms  pc=$F972  $0E <- 00      ← ★ 启动自测第 9 项，紧跟握手之前
[io] t=33563ms  pc=$F97C  $0E <- 08
```

也就是说 **BIOS 每次要正经跟 061 说话之前，都会先把 061 复位一次**。所以 `--dsp-trace`
日志里会反复出现 `[061] 061.dat(283D,真机同款) firmware, reset entry $CB9F`
（华容道那次会话前半段就打了 8 次）——这是**正常现象**，排查音频问题时要知道
"每个会话都是从刚复位的 061 重新开始的"。

### 7.3 为什么这条线决定了自测的结果

`061.dat`（283D，真机同款）里 `CC 0F` 只有一处回 `CC FF`，在**复位路径**上：

```
$8213  sp = 0x07FF
$821A  call $823A          ; 复位路径：握手循环 pop，直到拿到 0x0ABB，回 0xFABB
$821C  [0x0796] = 0
$821F  pet watchdog
$8222  call $CDC4          ; ★ PEEK（只看不取）
$8224  cmp r1, 0 ; je $821F         ; 队列空就继续等
$8226  cmp r1, 0x0FCC               ; ★ 是不是"CC 0F"这条命令
$8228  jne $8232                    ; 不是 → 进主循环（之后再也回不到这里）
$822E  r1 = 0xFFCC                  ; ★ 是 → 回 CC FF  ✓
```

主循环里的那条（跳表 `$CB72` → `$CC54` 再派发 → `$CF89` 的 `cmp r2,0x0F` / `je $CFB6` →
`$CFB9 r1 = 0x0FCC`）恒回 **`CC 0F`**；而 BIOS（bank16 `$739B`）读第二个字节后是
`CMP #$FF`，不是 `$FF` 就 `JMP $7538` 画 `SPCE061 ERR!!`（连 Check_Sum / Version 都不读）。

所以真机的顺序是：**先复位 061 → 发 `BB 0A`（被复位路径的握手吃掉）→ 发 `CC 0F`
（此刻它是"复位后第一条命令"）→ 收到 `CC FF` → 继续 `CC 01` / `BB 0B` / `BB 00`**。
模拟器在 2026-09-28 之前没实现 bit3，061 一直停在主循环 ⇒ 必然 ERR。

### 7.4 模拟器怎么建模

`src_nc3000/io_new.cpp` 的 `io_v2_write()` 原来没有 `0x0E` 分支（落到
`default: ioReg[address] = value`）——那根复位线等于被当成普通寄存器存了。现在补成：

```cpp
case 0x0e: {
    uint8_t old = (uint8_t)ioReg[address];
    ioReg[address] = (uint8_t)(value & ~0x10);   /* bit4 是输入，别存 */
    if (!(old & 0x08) && (value & 0x08))
        nc3_dsp_boot();                          /* bit3 0→1 = 放开复位，061 重启 */
    return;
}
```

`nc3_dsp_boot()` 会把 061 映像重新装载、从复位向量 `$CB9F` 跑起来（就是日志里那行
`reset entry $CB9F`）。改完实测：自测第 9 项通过，画面 `Check_Sum: f7 10 3b 30` /
`Version: 3d 28` 与真机一致；华容道 BGM、词典发音都照旧。

> 复现命令（headless，进自测菜单跑第 9 项）：
> ```powershell
> src_nc3000\nc3000_headless.exe roms\nc3000 --ms 40000 --hold 600 `
>     --press 26000 4 0 `                                    # 关机
>     --press 30000 1 4 --press 30100 3 6 --press 30300 4 0 ` # 按住 W+V 再按 ON/OFF
>     --press 33500 6 4 `                                    # 菜单里按 9
>     --io-watch 0x0e --dsp-trace
> ```
