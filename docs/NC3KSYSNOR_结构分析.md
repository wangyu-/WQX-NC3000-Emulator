# NC3000 固件 NC3KSYSNOR.nor 结构 / CPU 地址映射分析

分析对象：`info/NC3KSYSNOR.nor`（557,056 字节 = 0x88000），主控 SPDC1064（6502 兼容核，110 pin，带 NOR/NAND/外部 RAM 控制器）。
参考：`info/SPDC1064V043003.pdf`（主控数据手册）、`info/common.txt`、`info/NC3000_trap.txt`、`info/NC3000_io.txt`、
`src_nc2000/`（NC2000/SPDC1024 固件源码）、`src_nc2000_hw/`（NC2000 硬件文档）。

---

## 1. 结论速览

1. **文件 = 17 个连续 32KB 块 = NOR 的 bank 0x00 ~ 0x10**（1MB NOR = 32 × 32KB，导出文件只含前 17 个 bank）。
   文件偏移 `0x8000 * N` = **bank N 的 32KB 映像**。

2. **每个块 = 该 bank 在 CPU 窗口里的 32KB 内容**（不是芯片线性地址）：

   | bank | CPU 窗口（`io_bank_switch`=N 时） | 窗口内 4 个 8KB ↔ 芯片 8KB 块 |
   |---|---|---|
   | 0 | `0x8000-0xFFFF` | 块0,1,2,3（顺序，= 芯片 0x0000-0x7FFF） |
   | 1~31 | `0x4000-0xBFFF` | 块2,3,0,1（**上下 16KB 对调**） |

   即：`文件偏移 ↔ CPU 地址` 是直接对应的（bank0 基址 0x8000，bank1+ 基址 0x4000）；
   而 `文件偏移 ↔ 芯片线性地址` 在 bank1~31 需要把 16KB 上下半对调。

3. **每块开头 2 字节恒为 `60 EA`（字 0xEA60，占位/无效项），紧跟一张 2 字节一格的 INT 跳转表**，
   第 N 项（N=1,2,3…）位于块基址 `+2*N`，内容是本 bank 内处理程序的 16 位地址。

4. **CPU 地址空间的窗口划分**（详见 §4）：

   | CPU 区域 | 含义 | 选择寄存器 |
   |---|---|---|
   | `0x0000-0x03FF` | 零页 / 内部 RAM 低端（8 个零页 bank，兼作 I/O 0x00-0x3F） | `io_zp_bsw`(IO 0x0F) |
   | `0x2000-0x3FFF` | 内部 RAM（64KB 中选一个 8KB 窗） | `RAMS[2:0]`(IO 0x19 b2-0) |
   | `0x4000-0xBFFF` | 4×8KB 主窗口：外部 NOR / 内部 RAM / 外部 RAM | `io_bank_switch`(IO 0x00)、ROA(IO 0x0A b7)、Volume、RAMV |
   | `0xC000-0xDFFF` | 8KB “BIOS/BBS” 映像窗 | `io_bios_bsw`(IO 0x0A) 低 4 位 BBS |
   | `0xE000-0xFFFF` | 固定 BIOS（芯片 0x6000-0x7FFF），含 0xFFF4/0xFFFA/0xFFFC/0xFFFE 向量 | EXC(IO 0x1A b7) |

5. **`0xC000-0xDFFF` 映像窗的硬件规则：BBS = b ⇒ 芯片第 (b XOR 2) 个 8KB 块**。
   （SPDC1064 手册存储映射表给出的 BBS→块号关系；NC2000 源码 `bbs/makefile` 里的 `swap4000` 正是为满足该规则而做的搬移。）
   固件里实际使用的 6 个映像见 §5。

6. **INT（BRK）指令 = 3 字节：`00 <序号idx> <bank码>`**（内存里 idx 在前、bank 在后）。
   `bank码`：`0x00-0x3F` = NOR bank（在 0x4000/0x8000 窗口执行）；`0x80-0xBF` = RAM 程序；
   `0xC0-0xCF` = 8KB 映像（走 0xC000 窗口）；`0xFF` = 音乐。`idx=0` 表示“软错误”。

7. **两个文档里的记号约定**：
   - `common.txt` 的 `Axxxx`：`xxxx` 按**内存顺序**写，即 `A<idx><bank>`。所以 `A010b` ⇒ `INT $0B01`（idx=01，bank=0B）。
   - `NC3000_trap.txt` 的 4 位十六进制：按 `bank<<8 | idx` 写，即 `0B01` ⇒ bank=0x0B、idx=0x01。
   - 两者对同一函数是同一件事，只是字节顺序的写法不同。

---

## 2. 证据链（怎么确认的）

### 2.1 NC3000 固件自身的 INT 分发器

BRK/IRQ 向量在 bank 0 的 `0xFFFE/0xFFFF`（文件 0x7FFE/0x7FFF）= `0xEA36`（位于 BIOS 段）。
入口处先保存 A、检查系统就绪标志 `$03E4 == 0xA5`，再用“压栈的状态字节 AND IO 寄存器 `$F038`”
把硬件 IRQ 与 `BRK/INT` 区分开：走 `BRK/INT` 时跳 `0xE9E9`（NC2000 源码中对应 `irqser` 里
`pla / bit imd10h / jne is_break` 同一个套路）：

```
E9E9  78        SEI
E9EA  86 BC     STX $BC
E9EC  BA        TSX
E9ED  48        PHA
E9EE  BD 01 01  LDA $0101,X     ; 取压栈返回地址（= BRK 后第 3 字节地址）
E9F1  85 CC     STA $CC
E9F3  BD 02 01  LDA $0102,X
E9F6  85 CD     STA $CD
E9F8  A2 00     LDX #$00
E9FA  A1 CC     LDA ($CC,X)     ; = 第 3 字节 = bank 码
E9FC  C9 FF     CMP #$FF
E9FE  D0 05     BNE $EA05       ; ==FF 表示音乐中断，直接返回
...
EA11  A1 CC     LDA ($CC,X)
EA13  8D F5 03  STA $03F5       ; $03F5 = bank 码
EA1C  C6 CC     DEC $CC         ; 退回 1 字节 → 指向第 2 字节
EA1E  A1 CC     LDA ($CC,X)     ; = 第 2 字节 = 序号 idx
EA20  85 B1     STA $B1
EA22  D0 03     BNE $EA27
EA24  4C F2 E0  JMP $E0F2       ; idx==0 → 软错误/冷启动路径
```

随后 `0xE946` 段做跨 bank 调用（与 NC2000 源码 `bios/irq.s` 的 `brk_call_ser` 一一对应）：

```
E952  AD F5 03  LDA $03F5
E955  10 27     BPL $E97E       ; bank码 bit7=0 → NOR bank
E957  2C F5 03  BIT $03F5
E95A  50 15     BVC $E971       ; bit6=0 → RAM 程序；bit6=1 → 8KB 映像
E95C  AA        TAX
E95D  29 0F     AND #$0F        ; 8KB 映像：取低 4 位
E962  A5 0A     LDA $0A         ; io_bios_bsw(IO 0x0A)
E964  29 F0     AND #$F0        ; 保留高 4 位
E966  0D F5 03  ORA $03F5
E969  85 0A     STA $0A         ; 写入新的映像号
...
E97E  AA        TAX             ; NOR bank 情形
E981  A5 0A     LDA $0A
E983  29 7F     AND #$7F        ; 清 bit7（ROM 模式）
E985  85 0A     STA $0A
E987  A5 B1     LDA $B1         ; idx
E98E  29 7F     AND #$7F
E990  0A        ASL A           ; ×2 → 表内偏移
E991  85 B1     STA $B1
E993  2C F5 03  BIT $03F5
E996  50 04     BVC $E99C
E998  A9 C0     LDA #$C0        ; 8KB 映像 → 基址 0xC000
E99A  D0 08     BNE $E9A4
E99C  A9 40     LDA #$40        ; 其他 bank → 基址 0x4000
E99E  E0 00     CPX #$00        ; X = bank 码
E9A0  D0 02     BNE $E9A4
E9A2  A9 80     LDA #$80        ; bank 0 → 基址 0x8000
E9A4  85 B2     STA $B2
...
E9A8  A1 B1     LDA ($B1,X)     ; 读表项（低字节）
E9B1  A1 B1     LDA ($B1,X)     ; 读表项（高字节）
E9C4  20 E6 E9  JSR $E9E6       ; JMP ($00B1) 跳到表项地址
```

即：`目标 = 基址 + 2*idx` 处的 2 字节指针；`基址 = 0x8000(bank0) / 0x4000(其他 bank) / 0xC000(8KB 映像)`。
**这就是“bank 0 在 0x8000、其余在 0x4000”的直接证据。**

### 2.2 NC2000 源码（同一套架构，SPDC1024 → SPDC1064 未变）

- `h/6502.mac`：
  ```
  break      macro val,bank
             db 0,val,bank
             endm
  BREAK_FUN  macro lable
             db 0
             db >lable      ; 取高字节
             db <lable      ; 取低字节
             endm
  ```
  即 `INT` 三字节编码：`00, idx, bank`；符号值高字节 = idx、低字节 = bank。
- `bbs/bbs0/h/a.h` 定义了 `DEF_BRKBBS_FUN`：先 `db 60h,0eah`，再对每个函数 `dw labl`
  → **每个 bank / 8KB 映像开头正是 `60 EA` + 一串 2 字节地址**（与固件中观察到的完全一致）。
- `bios/irq.s` 的注释：
  ```
  ; Break 用 3 Byte: db 0, break_num, break_bank
  ; break_num  b7=1:Flash(RAM bank) 0:ROM  b6~b0 128 service num
  ; break_bank 00~3fh flash中跨bank程序调用
  ;	     80~bfh rom中程序调用
  ;	     c0~cfh bbs调用
  ;            ff     music
  ; 所有跨越BANK 的调用都用Break 来完成
  ; Break 不破坏Y; 调用完成后 A,X,Y 返回;BSW,BANK 恢复
  ```
- `makefile`：目标镜像 `obj.bin = 0x80000`（512KB），拼接顺序 = bank 0 起连续排布，
  bank 0 = `prom0(16K) + bbs0(8K) + bios(8K)`；`bbs/obj.bin = bbs4+bbs5+bbs6+bbs7`（4×8K）。
  → 一个 32KB bank = 16KB 程序 + 2×8KB，或 4×8KB。NC3000 的 bank 0、bank 2 与此完全对应。
- `bbs/makefile` 在拼好 4 个映像后执行 `swap4000 obj.bin`（把 32KB 的上下 16KB 对调）。
  对照手册 “BBS=b ⇒ 芯片块 (b XOR 2)” 可知：对调后 bbs4/5/6/7 正好落在芯片块 6/7/4/5，
  与 4^2=6、5^2=7、6^2=4、7^2=5 吻合 —— **这是 BBS 选择规则与“上下 16KB 对调”的独立印证**。

### 2.3 SPDC1064 主控手册

`info/SPDC1064V043003.pdf` 第 4-5 页 “MEMORY MAPPING (ALL)” 给出：
8KB 窗口划分、`ROA`(ROM/RAM)、`BSW`(bank)、`Volume`、`RAMV`、`BBS`、`EXC`、`RAMS`(内部 RAM bank) 各字段的作用，
以及 “多映射” 说明；第 6-14 页给出 IO 寄存器位定义（0x00 BANK SWITCH、0x0A BIOS BANK SWITCH、0x0F ZERO PAGE BANK SWITCH、
0x19 RAMS、0x1A EXC 等）。本报告 §4/§6 的映射结论即以此为准，并与固件、NC2000 源码互为印证。
（提取文本见 `out/SPDC1064_text.txt`，带坐标提取器 `tools/pdfpos.js` 可用于复核表格。）

### 2.4 文档交叉验证

- 用 `NC3000_trap.txt` 的 493 条记录逐条校验 “文件 = bank 顺序 + 表项在 +2*idx”：
  - bank 0/1/3/4/5/6/9/0x0B~0x10 的 394 条 ⇒ **393 条与文件逐字节一致**；
  - 唯一不一致的 `0B0A` 是文档笔误（文档写 `8595`，固件里是 `8559`，数字写反了）；
  - `C7/C8/CA/CB` 共 99 条 ⇒ 在文件 0xE000 / 0x10000 / 0x14000 / 0x16000 处 **99/99 一致**，
    且**条目数也完全相等**（55/9/23/12）。
- 用 `common.txt` 的 `__` 符号（其中 529 个属于 bank 0x00~0x10，16 个属于 RAM bank 0x80）
  反查表项地址是否落在该 bank 的窗口内：**529/529 命中**
  （bank0 全在 0x8000-0xFFFF，bank1~0x10 全在 0x4000-0xBFFF），无一例外。
- 抽查实际调用点：如文件 0x5159A 处 `... STY $7B / 00 01 C7 / JSR $55E7`，
  即 `INT $C701`（bank 0xC7，idx 1 = `get_16x16_font`），反汇编对齐正确 ⇒ 编码顺序确认。

---

## 3. 文件总体结构（文件 ↔ CPU ↔ 芯片）

```
文件偏移            长度  bank   CPU 窗口（io_bank_switch=N）  说明
0x00000-0x07FFF   32KB  0x00   0x8000-0xFFFF                主程序(16K) + 常驻8K模块 + BIOS(8K)
0x08000-0x0FFFF   32KB  0x01   0x4000-0xBFFF                bank1 程序 + 8K 映像(0xC7)
0x10000-0x17FFF   32KB  0x02   0x4000-0xBFFF                bank2 = 4 个 8K 映像(C8/C9/CA/CB)，无本级程序
0x18000-0x1FFFF   32KB  0x03   0x4000-0xBFFF                bank3
0x20000-0x27FFF   32KB  0x04   0x4000-0xBFFF                bank4（INT 表 87 项，最多）
0x28000-0x2FFFF   32KB  0x05   0x4000-0xBFFF                bank5
0x30000-0x37FFF   32KB  0x06   0x4000-0xBFFF                bank6
0x38000-0x3FFFF   32KB  0x07   0x4000-0xBFFF                bank7
0x40000-0x47FFF   32KB  0x08   0x4000-0xBFFF                bank8
0x48000-0x4FFFF   32KB  0x09   0x4000-0xBFFF                bank9（81 项）
0x50000-0x57FFF   32KB  0x0A   0x4000-0xBFFF                bank0A
0x58000-0x5FFFF   32KB  0x0B   0x4000-0xBFFF                bank0B（录音/记事）
0x60000-0x67FFF   32KB  0x0C   0x4000-0xBFFF                bank0C
0x68000-0x6FFFF   32KB  0x0D   0x4000-0xBFFF                bank0D
0x70000-0x77FFF   32KB  0x0E   0x4000-0xBFFF                bank0E
0x78000-0x7FFFF   32KB  0x0F   0x4000-0xBFFF                bank0F
0x80000-0x87FFF   32KB  0x10   0x4000-0xBFFF                bank10（只用 0x7413 字节，其余 0xFF）
```

### 3.1 bank 0（文件 0x00000-0x07FFF）

| 文件偏移 | CPU 地址 | 芯片 8KB 块 | 内容 |
|---|---|---|---|
| 0x00000-0x01FFF | 0x8000-0x9FFF | 块0 (0x0000) | 主程序开头 + INT 跳转表（16 项，表在 0x8000） |
| 0x02000-0x03FFF | 0xA000-0xBFFF | 块1 (0x2000) | 主程序 |
| 0x04000-0x05FFF | 0xC000-0xDFFF | 块2 (0x4000) | 常驻 8KB 模块 = 映像 BBS=0（39 项，表在 0xC000，主程序可直接 JSR） |
| 0x06000-0x07FFF | 0xE000-0xFFFF | 块3 (0x6000) | BIOS：0xE000 是跳转表 `jmptbl`，0xFFFA-0xFFFF 是向量 |

向量实测值（文件 0x7FFA-0x7FFF）：`NMI=0xE83B`、`RESET=0xFFF4`（该处 `JMP $E0D2`）、`IRQ/BRK=0xEA36`。
bank0 的 32KB 在芯片上是**顺序**的（0x0000-0x7FFF），因为 BBS=0 恰好选中块2、0xE000 窗固定显示块3。

### 3.2 bank 1~0x10（文件 0x8000*N）

窗口内 4 个 8KB 槽 ↔ 芯片 8KB 块为 **2,3,0,1**（上下 16KB 对调）：

| bank 内偏移 | CPU 地址 | 芯片内偏移（相对 bank 基址） | 说明 |
|---|---|---|---|
| 0x0000-0x1FFF | 0x4000-0x5FFF | +0x4000-0x5FFF | 本级跳转表在此（表项 = 基址+2*idx） |
| 0x2000-0x3FFF | 0x6000-0x7FFF | +0x6000-0x7FFF | 本级程序 |
| 0x4000-0x5FFF | 0x8000-0x9FFF | +0x0000-0x1FFF | 本级程序 |
| 0x6000-0x7FFF | 0xA000-0xBFFF | +0x2000-0x3FFF | 本级程序；bank1/bank2 的此处是 8KB 映像 |

例：文件 0x0DFF0（bank1 槽3）↔ CPU 0xA000 ↔ 芯片 bank1+0x2000；文件 0x14000（bank2 槽2）↔ CPU 0x8000 ↔ 芯片 bank2+0x0000。

> 等价说法：文件 = 用 `io_bank_switch=N` 时的 CPU 窗口内容；若导出工具改为按芯片线性地址读，只需把 bank1~31 每个 32KB 的上下 16KB 对调即可互换。

---

## 4. CPU 地址空间（SPDC1064 手册 “MEMORY MAPPING (ALL)”）

| CPU 区域 | 内容 | 选择方式 |
|---|---|---|
| `0x0000-0x003F` | I/O 寄存器（bank switch、timer、UART、RTC…） | 固定 |
| `0x0000-0x03FF` | 零页，8 个 bank | `io_zp_bsw`(IO 0x0F) b2-0 = ZB |
| `0x0040-0x1FFF` | 内部 RAM 低端（0040-07F/0080-... 分区） | 固定/零页 bank |
| `0x2000-0x3FFF` | 内部 RAM（64KB 里选 8KB） | `RAMS[2:0]`(IO 0x19 b2-0) |
| `0x4000-0xBFFF` | 4×8KB 主窗口：外部 NOR / 内部 RAM / 外部 RAM | `io_bank_switch`(IO 0x00) + ROA(IO 0x0A b7) + Volume + RAMV |
| `0xC000-0xDFFF` | 8KB 映像窗（BIOS/BBS） | `io_bios_bsw`(IO 0x0A) b3-0 = BBS |
| `0xE000-0xFFFF` | 固定 BIOS（芯片块3 = 0x6000-0x7FFF），含 0xFFF4/0xFFFA/0xFFFC/0xFFFE 向量 | `EXC`(IO 0x1A b7) 可改为外部 C000-FFFF |

要点：

- **ROA**(`io_bios_bsw` b7) 手册写作 “4000-BFFF ROM/RAM SELECT 0:ROM 1:RAM”；ROA=1 时 0x4000-0xBFFF 变成内部/外部 RAM，
  这也是固件里到处出现 `LDA $0A / AND #$7F / STA $0A`（切 ROM）的原因。
- `0x4000-0xBFFF` 的 4 个 8KB 由 `BSW`(= io_bank_switch，0x00-0xFF，实机 0-0x1F) 选 bank，配合 `Volume`(VSL0/VSL1)、
  `RAMV`(XM0CF b7) 决定是外部 NOR / 内部 RAM / 外部 RAM。
- 手册的 “Multi-Mapping” 提示：`0x4000/0x6000` 与 `0x2000-0x3FFF RAMS`、`0xC000 BBS=1` 与 `RAMS=011`
  在某些组合下是同一块物理 RAM 的双映射。

---

## 5. INT（BRK）机制小结

- 指令形态：`00 <idx> <bank>`（机器码三字节；NC2000 宏写作 `break val,bank` / `BREAK_FUN lable`）。
- 分发表位置：
  - NOR bank：`基址(0x8000 或 0x4000) + 2*idx`；
  - 8KB 映像：`0xC000 + 2*idx`。
- 表项 0 恒为 `0xEA60`（字节 `60 EA`）；`idx=0` 不会被查表，而是跳 `0xE0F2`（软错误/异常路径），
  所以该常量只是填充。
- 返回时恢复 `io_bank_switch` / `io_bios_bsw`（源码注释：“调用完成后 A,X,Y 返回；BSW,BANK 恢复”）。
- 功能号上限：bank 内最多 0x7F 个函数（表最大 256 字节）；bank 0x04 实际用了 87 项。

**查表速查公式**（已知 `INT $BBNN` 想找处理程序代码）：

```
bank = BB, idx = NN
表项位置  = 0x8000*bank + 2*idx            （文件偏移；bank 0x00~0x10 都在文件内）
表项内容  = 处理程序在本 bank 窗口内的地址 A
窗口基址  = 0x8000 (bank 0) / 0x4000 (bank 1~) / 0xC000 (bank 0xC0~0xCF 的 8KB 映像)
代码位置  = 0x8000*bank + (A - 窗口基址)    （bank 1~0x10）
          = A - 0x8000                     （bank 0）
```

---

## 6. 内嵌的 8KB “BIOS/BBS” 映像（INT $C?xx）

`0xC000-0xDFFF` 窗口显示哪个 8KB，由 `io_bios_bsw` 低 4 位 BBS 决定：
**BBS = b ⇒ 芯片第 (b XOR 2) 个 8KB 块**（手册映射表；NC2000 `swap4000` 步骤独立印证：bbs4/5/6/7 被搬成
芯片块 6/7/4/5，正满足 4^2=6、5^2=7、6^2=4、7^2=5）。

固件里实际存在的映像（`INT $C?xx`，第三字节 0xC0+b）：

| INT 段 | BBS | 芯片 8KB 块 / 地址 | 文件偏移 | 所属 bank / 槽 | 服务数 | 目标地址范围 |
|---|---|---|---|---|---|---|
| $C0xx | 0 | 块2 / 0x004000 | 0x04000 | bank0 / 槽2 | 39 | 0xC000-0xDF15 |
| $C7xx | 7 | 块5 / 0x00A000 | 0x0E000 | bank1 / 槽3 | 55 | 0xC000-0xDEBB |
| $C8xx | 8 | 块10 / 0x014000 | 0x10000 | bank2 / 槽0 | 9 | 0xC000-0xC94F |
| $C9xx | 9 | 块11 / 0x016000 | 0x12000 | bank2 / 槽1 | 27 | 0xC000-0xDE2C |
| $CAxx | 10 | 块8 / 0x010000 | 0x14000 | bank2 / 槽2 | 23 | 0xC000-0xDA02 |
| $CBxx | 11 | 块9 / 0x012000 | 0x16000 | bank2 / 槽3 | 12 | 0xC000-0xDAEB |

证据链：
1. `trap.txt` 中 C7/C8/CA/CB 四组的内容与条目数与上表 4 个偏移处**逐条、逐项完全吻合**；
2. 反汇编出的真实调用点：`00 01 C7`（idx=1 = `get_16x16_font`）、`00 01 CB`（`bbs_oldmon`）等，第三字节就是 BBS 值；
3. 全库统计 `00 idx C?` 的 idx 分布与该映像的服务数吻合（如 0xC7 共 801 处、其中 799 处 idx≤55；
   0xCB 共 123 处、120 处 idx≤12；0xCA 共 400 处、325 处 idx≤23）；
4. NC2000 源码里 `bbs0` 正位于“bank0 的第 3 个 8KB”，即以 BBS=0 调用（`DEF_BBS_ bbs0`）。

对照 NC2000 源码可知这些映像的职责（同一份程序的不同版本）：

- 0xC7 ≈ NC2000 的 bbs4：字模/图形输出（`ccg`、`get_8x10_font`、`get_16x16_font`、`update_lcd`…）；
- 0xCA ≈ NC2000 的 bbs6：数学/绘图（`to_ascii`、`int_divd`、`mul_ax`、`line_draw`…）；
- 0xCB ≈ NC2000 的 bbs7：`bbs_oldmon`、`proc_menu`、`get_key_word`、`NCWholeToPC/FromPC`…；
- 0xC0 是 bank0 的常驻 UI 模块（NC2000 的 bbs0 同位置）；0xC8/C9 为日历/其他功能。
- BBS=1 不是 ROM 而是内部 8KB SRAM（手册：“C000-DFFF, BBS=1 与 2000-3FFF RAMS=011 双映射”），
  固件里 62 处显式写 `BBS=1` 的代码可用于“下载到 RAM 后执行”。

---

## 7. IO 相关（本次用到/证实的）

| IO | 名称 | 作用 |
|---|---|---|
| 0x00 | BANK SWITCH | ROM/RAM bank（0x00-0x3F ROM；bit7=1 → RAM） |
| 0x0A | BIOS BANK SWITCH | b7 ROA(0:ROM,1:RAM)、b6 FPIEN、b5 FPICLR、b4 DIR401、b3-0 BBS |
| 0x0F | ZERO PAGE BANK SWITCH | b2-0 ZB + 若干方向位 |
| 0x19 | PORT4 CONFIG | b2-0 RAMS（2000-3FFF RAM bank），b4 TMACT，b3 TPIEN |
| 0x1A | PWM CONTROL | b7 EXC（外部 C000-FFFF 使能） |
| 0x25/0x26 | XM0CF / XM1CF（经 RTC SIF 索引寄存器配置） | 外部存储配置；XM0CF b7=RAMV |
| 0x3A-0x3D | BSReg/IRCReg/…、uart_BK/IVReg | 与 NC2000 源码定义一致（`brui equ 3ah`、`uart_BK equ 3dh`） |

---

## 8. 产物与工具

- `out/nc3000_int_table.tsv`：**全部 556 条 INT 记录**（INT 号 / bank / idx / 基址 / 处理程序地址 / CPU 表项地址 / 名称 / 名称来源），
  名称来自 `common.txt` 与 `NC3000_trap.txt` 合并。
- `tools/dis6502.js`：6502 反汇编器（可指定文件偏移区间与基址）。
- `tools/gen_int_table.js`：生成上面那张 INT 表，并打印 8KB 映像清单。
- `tools/scan_io0a.js`：扫描对 IO 0x0A 的读写点。
- `tools/pdftext.js` / `tools/pdfpos.js`：PDF 文本/带坐标提取（用于读 SPDC1064 手册，产物 `out/SPDC1064_text.txt`）。
- `src_nc2000/`：从 `info/nc2000f.rar` 解出的 NC2000 全量源码（26.9MB，作对照用）。
- `src_nc2000_hw/`：从 `info/nc2000.zip` 解出的 NC2000 硬件资料（原理图、hardware spec、SPDS104A programming guide 等）。

---

## 9. 尚需确认/后续可做

> 2026-09-27 更新：第 1、4 两条已经解决——
> `tools/nc3000_rom_prep.ps1` 会把 17 个 bank 的 dump 补 0xFF 到完整 32 bank（1 MB）；
> 中断那一条的"硬件到底压了几个字节"由 `xBRK()`（`ansi/w65c02macro.h`）实测定案：
> **BRK 压的是 PC+2**，压 PC+1 会让整个 INT 链路错位（见 `NC3000_键盘.md` §6）。
> 剩下第 2、3 条属于"手册表格核对/继续标注"，不影响模拟器运行。

1. ~~bank 0x11~0x1F（1MB NOR 的其余 15 个 bank）不在导出文件中；需要确认它们是否存在数据。~~
   → 已由 ROM 预处理补全（其余 bank 在真机上也是 0xFF/未被固件使用）。
2. 手册 “MEMORY MAPPING (ALL)” 表中 BBS=5,6,7,8,9,A,B 所在的列（应满足 BBS^2=块号）未在导出文本里完整对齐，
   建议用带渲染的 PDF 阅读器复核该表（脚本 `tools/pdfpos.js` 已能给出每个单元格的坐标）。
3. 每段代码的语义（如 bank 4 的 87 项、bank 9 的 81 项）可结合 `common.txt` 名称 + 反汇编继续标注。
4. 中断入口细节：该 CPU 的 IRQ 把“中断向量号”压在栈上（寄存器 `IVReg` 0x3D），函数 `0xEA36` 用
   “压栈字节 AND IO 0xF038”区分硬件中断与 INT 指令；`0xF038/0xF039` 具体是哪两个 IO 需再确认。

---

## 10. 与 SPCE061A 模拟器工作的合并（主控 ↔ DSP 协议）

本报告的副产品是**主控侧对 SPCE061A（语音/音乐 DSP）的完整驱动证据**，已与另一条
SPCE061A 模拟器/播放器工作合并到 `spce061a/`：

| 主控侧例程（bank0 CPU 地址） | 命令 | 用途 |
|---|---|---|
| `$F9E9` | `0xBB 0x0A` → `0xBB 0xFA` | 链路握手/唤醒 |
| `$FA44`、`$FCF3` | `0xAA 0x00` → `0xAA 0x00` | 停止（带应答） |
| `$FA83`、`$FA98`、`$FA71` | `0xAA 0x02/0x03/0x01` | 结束 / 结束带应答 / 睡眠 |
| `$FAE3` | `0x99 <参数>` | 选解码器（`0x00`=MS01 音乐、`0x21`=S600 单词发音…） |
| `$FAEE` / `$FB01` | `0x44` / `0x55` | 音量（0..15）/ 速度（表 06/09/0C/0F/12） |
| `$FC7D` / `$FCB6` | `0x33`+15B / `0x22`+len+payload | 数据流分帧 |
| bank0B `$9AFA`/`$9B27` | `0x11 0x01/0x03 …` | TTS（单词发音）包 |

物理通道是主控的 `0x3A-0x3D` 字节串口（`0x3A` 数据、`0x3B` bit5 可发送、`0x3C` bit4 可接收、
`0x3D` bank 选择）＋握手线（主控 IO `0x0E` bit4 ← 061 的 `P_IOB` bit14）。

细节、证据与一致性自检（22/22 通过、音频与参考相关系数 1.0000）见
`spce061a/docs/NC3000-061协议对照.md` 与 `spce061a/host/`。
