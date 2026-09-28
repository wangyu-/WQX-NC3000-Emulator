# NC3000 NAND（NC3KSYSNAND.nand）与 NGFFS 文件系统分析

> **结论速览（2026-09-27，最新）**
>
> ```
> FS 扇区 N  ↔  NAND 设备页 32*N              （恒等，没有坏块重映射）
> .nand 文件偏移 = (设备页 - 64) * 528        （.nand 第 0 页 = 设备第 64 页）
> 一个扇区 = 32 × 528 = 16,896 字节
> 目录块 = 16 字节条目 [2B inode][14B GBK 名]，写在 blk[0] 扇区的页 0（条目从 +0x10 开始）
> inode 表 = 32 字节/条、槽位 (id-1)*0x20、以 A8 A8 结尾；
>            NOR 里有两份世代（0xFC000 / 0xFD000），更新走"日志 + INT $051C 整理"
> ```
>
> **给 FS 加文件的正确姿势**（见 §8）：`open($088d)` → `write` → `close` →
> **`INT $051C`(InodeReclaimAll)**。少了最后一步，数据都写对了、但固件看不见。
>
> ⚠️ **本文的 §5、§6 是早期结论，已被 §7、§8 取代**（当时以为"逻辑块→物理偏移不是线性"、
> "需要 SuperBlock"，后来实测是恒等映射）。§1–§4 的结构性内容仍然有效。
> 当前总览见 `docs/README.md`。

对象：`info/NC3KSYSNAND.nand`（53,779,970 字节 ≈ 51.3MB，64MB NAND 的数据区）、
`info/NGFFS.ppt`（2002 年的《通用NAND闪存文件系统 GNFFS V1.2 规划》）、
`info/NC3000_io.txt` 里的 Inode 说明。

工具：`tools/nand_extract.js`（列表/抽取）、`nand_inodes.js`（Inode 表）、
`nand_findname.js`（按名字找目录块）、`nand_regions.js`/`nand_map.js`（物理区域与内容特征）。

---

## 1. NGFFS 结构（ppt 原文要点）

| 项 | 值 |
|---|---|
| 擦除块 | **16KB**（每块可写指定次数，页写、顺序定位慢） |
| SuperBlock | 2KB，驻留 BusFlash（NOR），Magic `GGV NGFFS V1.0`，含 SectorSize/BadList/InodeBank/InodeAddr 等 |
| SectorBitmap | 2KB Free/Used 位图 |
| Inode | **32 字节/个，最多 1024 个**；字段：Inode#、Attribute(R/W/X/DIR/ROOT/SYS/RAW…)、Status(DISCARD/RECYCLE/DIRTY)、创建/修改/到期时间各 3B、FileSize 3B、3 个直接块 + 2 个一级索引 + 1 个二级索引、IBlockValid |
| 目录内容 | 每条 **16 字节**：`[2B 目标 Inode][14B GBK 名字，\0 结束，FF 填充]`，以 `FF FF` 记录结束 |
| 能力 | 998 个文件、4 层目录、文件名 14B、同时打开 3 个文件 |

**本机实测的对应关系**：

- `NC3KSYSNAND.nand` 的 **0x0000-0x3FFF = Inode 表**（512 个 32 字节记录，用 `A8 A8` 结尾判定），
  实际使用到 **94 个节点**（id 1~94，连续）。
- 每个 Inode 的块表是 `[起始块, +1, +2, 结束块]`，**块号 × 16KB** = 文件大小；
  例：`#9 ahddata` 1184 块 × 16KB = 18.5MB、`#8 dict_cam` 768 × 16KB = 12MB —— 与物理区域尺寸精确吻合。
- 目录块（16 字节记录）在镜像里有三处真实目录：根目录、sysdir，以及“美国传统词典”等子目录。

## 2. 目录树（实测）

**根目录**（NAND 偏移 0x4000，= 块 1）：

```
0:.. | 1:sysdir | 52:midi | 73:下载辞典 | 78:应用程序 | 82:旅游全球话 | 91:歌曲音乐 | 94:文本文件
```

**sysdir**（目录块在 0x3345C00 起，含 50 项；这是主控固件里被大量引用的目录）：

| 类别 | 文件（Inode:名字，大小 = 块数×16KB） |
|---|---|
| 内核/字模 | 2:CCG.BIN(1184KB) 3:INPUT.BIN(192KB) 4:GB2PY 5:HZPY 6:uni2gb 7:dict_graph 25:word_ID |
| 词典数据 | 8:dict_cam(12MB 剑桥) 9:ahddata(18.5MB 美国传统) 10:dictdata(4.6MB) 11:hydata 12:synonym 13:antonym 14:wordnet 15:deriv 16:fuhe 17:tongyin 18:diff 19:sents 20:phras 21:voc2_0 22:voc2_1 23:voc2_2 24:fenlei2_0 |
| **语音（单词发音）** | **26:celp_data(5.0MB)** **27:ahd_celp(256KB)** **28:wqx_celp(320KB)** **29:cb_celp(256KB)** 48:celps 49:jinqu 50:hexuan 51:xinhua |
| 新华字典包 | 31:新华数据.bin(3.6MB) 32:xh_to_uni 33:xh_input 34:xh_exfont 35:xh_bs 36:xh_bh 37:xh_graph |
| DSP | **38:升级061.bin(64KB)** ← **SPCE061A 固件升级镜像** |
| 应用 | 39:time 40:other 41:calculator 42:change 43:namecard 44:game 45:hero 46:txt_view 47:zijian 30:midigame |

主控固件（`NC3KSYSNOR.nor`）里能直接看到这些路径字符串，互相印证：
`/sysdir/celps`、`/sysdir/dictdata`、`/sysdir/hydata`、`/sysdir/voc2_*`、`/sysdir/wordnet`、
`/sysdir/namecard`、`/sysdir/061.bin`、`/sysdir/hero` ……

## 3. 单词发音数据在哪里

**就是 sysdir 里的这几个 CELP 语音库**（与 `common.txt` 里的
`CELP_MODE/CELP_SECTOR/CELP_SEC_NUM/CELP_DATA_SLOT/CELP_ADDR_SLOT/CELP_BIN_SLOT`
这组变量对应）：

| Inode | 文件 | 大小 | 说明 |
|---|---|---|---|
| 26 | `celp_data` | 313 块 = 5.0MB | 主语音库（词条音频） |
| 27 | `ahd_celp` | 16 块 = 256KB | 美国传统词典语音 |
| 28 | `wqx_celp` | 20 块 = 320KB | 文曲星词典语音 |
| 29 | `cb_celp` | 16 块 = 256KB | 另一部词典语音 |
| 48/49/50/51 | `celps`/`jinqu`/`hexuan`/`xinhua` | 各 2 块 | 语音相关小程序/小库 |

物理位置上，这些文件分布在字典数据区附近（例如 `ahd_celp` ≈0x32C1800、`wqx_celp` ≈0x2DAE000、
`cb_celp` ≈0x2074000 的已用区域里），文件内部能看到 **递增的 16 位偏移表（词条→音频偏移）**，
这就是“单词 → CELP 音频”的索引。

## 4. 顺带发现：061 的固件就在 NAND 里

`sysdir/升级061.bin`（Inode 38，4 块 = **64KB**，正好是 SPCE061A 的 flash 容量）里
存在与模拟器所用 `061.dat` **相同的 ID 头**：

```
061.dat 偏移 0x400 起： 19 d3 cf 04 40 f0 c1 85 11 93 d5 04 09 03 36 05 11 03 d1 06 …
NAND 0x323E020 起    ： 19 d3 cf 04 40 f0 c1 85 11 93 d5 04 09 03 36 05 11 03 d1 06 …
```

逐块对拍显示：这是**同一族但不同版本**的 061 固件（局部字节完全一致，整体有明显差异），
也就是说 —— **NC3000 自己的 061 固件可以从这份 NAND 里抽出来**，模拟器就应该跑它
（上一线程用的 `061.dat` 来自别的机型/版本）。

> ✅ 2026-09-27 已落地：`src_nc3000/spce061_bridge.cpp` 现在**优先从 NAND 的
> `sysdir/升级061.bin` 取 061 固件**（启动日志：`[061] NAND 升级061.bin firmware, reset entry $CBB1`），
> 取不到才退回 `spce061a/rom/061.dat`。

## 5. 抽取工具与已知限制

> ⚠️ **本节（§5、§6）是早期结论，已被 §7、§8 取代**：
> 后来实测确认 `FS 扇区 N ↔ NAND 设备页 32*N` 是**恒等映射**（没有坏块重映射），
> 而且 NOR 的高 bank（SuperBlock/inode 日志）我们已经有完整 1 MB 镜像。
> 只是 README 里那两个抽取脚本的"尽力抽取"思路仍然可以当特征定位法用。

`tools/nand_extract.js` 会输出文件清单（`--list`）并按“尺寸 + 物理顺序”做**尽力抽取**：

```
node tools/nand_extract.js info/NC3KSYSNAND.nand out/nand_files
```

限制：NAND 有坏块/搬移（NGFFS 的 `SectorReclaim`），所以 *逻辑块号 → 物理偏移*
不是线性映射；它记录在 **SuperBlock 的 BadList / Sbank-Sbaddr** 里，而 SuperBlock
按设计驻留在 BusFlash（NOR）——本仓库的 NOR 导出只含 bank 0x00~0x10，**不含文件系统元数据区**，
因此精确映射需要：

1. 补一份 NOR 高 bank（或 NAND 的 5 个 SuperBlock）的导出；或
2. 按文件内容特征定位（本报告已用这种方法确认了大文件与 CELP 库的位置）。

已确认无损的部分：Inode 表、目录结构与文件清单、所有文件的**名字+大小+块表**、
以及“061 固件在 NAND 里”这一事实。

## 6. 与模拟器工作的衔接（下一步）

---

## 7. ★ 2026-09-28：**扇区 ↔ 物理页映射搞清了（并且实测验证）**，注入工具已可用

之前认为"逻辑块 → 物理偏移不是线性、需要 SuperBlock"——**这个结论作废**。用新的
`--nand-after <ms>` 过滤 + 模拟器自带的 NAND 读日志（打印固件发来的页地址）直接对拍：

```
固件读 sysdir 时发出的地址:  addr=00 00 8E 01  ->  page = 0x18E00 = 101888 = 0xC70 * 32
```

而 sysdir 的 inode 里写的正是 **扇区 0xC70**。再看文件内容：

```
sysdir 目录项 "CCG.BIN"(inode 2) 在 .nand 里的偏移 = 0x3345C10
预测: (32*0xC70 - 64) * 528 + 0x10 = 0x3345C00 + 0x10   ✓ 完全吻合
```

### 7.1 三条结论（现在可以照抄）

| 关系 | 公式 |
|---|---|
| FS "扇区" N ↔ NAND 设备页 | **page = 32 × N**（恒等，没有坏块重映射） |
| 扇区 N ↔ `.nand` 文件偏移 | **offset = (32·N − 64) × 528**（`.nand` 的第 0 页 = 设备第 64 页） |
| 一个扇区的容量 | 32 × 528 = **16,896 字节**（文档里写的"16KB"是近似） |

FS 元数据：inode 表 = `.nand[0x0000:0x4000]`（512 条 × 32 字节，`A8 A8` 结尾）；
目录块 = 16 字节记录 `[2B inode][14B GBK 名字\0 FF 填充]`，以 `FF FF` 结束。

### 7.2 新增工具 `tools/nand_addfile.py`

```powershell
python tools\nand_addfile.py <nand> --dir <目录扇区> --name "<GBK 名字>" --file <主机文件> [--inode N] [--sector S] [--dry-run]
```

它会：找一个全 `FF` 且未被任何 inode 引用的空闲扇区 → 写数据（按扇区补齐 `FF`）→
建 inode 记录（attr 0x808A / status 0xC0 / count / 块表）→ 往目录块末尾追加目录项。
原镜像已备份为 `roms/lee2/nc3000.nand.orig`。

### 7.3 首次注入实验（**还没成功，卡在"app 怎么找文件"这一步**）

> **2026-09-27 已解决，见下一节 7.4。** 结论是：不要手工拼 inode，直接用固件自己的
> `INT $0515/$0518/$0517` 写，然后**必须再调 `INT $051C`（InodeReclaimAll）** 才会生效。

把原机的 `音乐1.mid`（16,384 B，正好一个 16KB 块）注入到 `/midi`（inode 52，扇区 0xE2E）：

```
inode 98, 扇区 0xC89, 名字 音乐1.mid     ← 工具输出
```

再进"网络 → 和弦音乐"，仍然显示 **"空目录！请连接文曲星网站下载语音文件"**。

已知线索：

* NOR bank0 `$DD78` 有 **20 条字符串指针**，指向 `$DDA1` 起的
  `/midi/音乐1.mid` … `/midi/音乐20.mid`（GBK，逐条 +0x10 字节）——说明固件是按这 20 个
  **固定路径**去要文件的，而不是列目录。
* NAND 读日志（`--nand-after`）显示进"和弦音乐"后固件主要在重读 sysdir 那几页
  （0xC70 / 0xCF0 / 0xC41…），**没有出现明显的 `/midi` 块读取**；而日志目前只记录
  每条 NAND 命令的**第一次**读，跨页目录扫描会被漏掉。

下一步（很具体）：

1. 把日志改成"每页都记"（或记录跨页命令的页范围），确认固件到底有没有去读扇区 0xE2E；
2. 若确实读了却仍报空目录 ⇒ 说明 FS 还要求别的东西（例如 SuperBlock 里的"已用 inode 数/
   扇区位图"，而 SuperBlock 在 NOR 的高 bank，我们没有）；那就换成**改写一个已有 inode**
   （复用它的扇区，不新增）来验证；
3. 用 NOR 里那 20 条路径反查应用代码的 open 流程（BBS 窗口 `$C000-$DFFF` 里）。

1. 用同一套方法抽出 `升级061.bin`（4 块，物理上在 0x323C000~0x324BFFF 一带），
   替换模拟器里的 `firmware_061` 映像 → 模拟器跑的才是 NC3000 真机固件。
2. 从 `celp_data` 取出一个词条的 CELP 数据；主控侧读取/发送逻辑在 NOR 的 bank 9
   （`CELP_*`、`__play_cur_sound`、`__play_syll_*`），按它把数据包成
   `0x99 0x21` + `0x11 xx payload` 发给 061 → 用 `spce061a/host/nc3000_dsp.c`
   的协议驱动模拟器出声，完成“单词发音”的端到端复现。

---

## 8. ★ 2026-09-27：**加文件彻底打通**（第 7.3 节的问题解决了）

一句话：**inode 表是"日志式 + 双世代"的，写到 NOR 日志区之后还要让固件整理一次才生效。**

### 8.1 正确的加文件姿势

```
1) 固件 FS API：把文件名写进 $088d → INT $0515(open/create)
                数据缓冲 $3200 / 长度 $08C6-$08C8 → INT $0518(write)
                INT $0517(close)
2) ★ 紧接着 INT $051C(InodeReclaimAll)：把 NOR 日志整理成一份完整新表
   —— 少了这一步，open/write/close 全成功、NAND 里字节都对，但开机后目录仍是空的
```

模拟器侧已经落在 `src_nc3000/cmd.cpp` 的 nc3000 `put` / `create_folder` 里；
主机侧入口 `tools/nc3k_put.ps1`。

### 8.2 inode 表的实际布局（实测）

```
记录 = 32 字节，槽位（老世代）= (id-1)*0x20，以 A8 A8 结尾
  id(2) attr(2) st(1) date(4) time(5) cnt(2) ?(2) blk[4](8) 保留(4) A8A8(2)
  st = 0xC0 当前 / 0x40 已作废 / 0x80 过渡

NOR 双世代：0xFC000 与 0xFD000（每世代 0x1000 字节 = 512 条）
  日志区：0xFDBC0 起（老世代）/ 整理后落在新世代里
NAND 副本：.nand 偏移 0x0000..0x3FFF
  = FS 扇区 0x2 的页 0..30（31 页），该扇区最后一页（页 95）是根目录块
```

注意：**固件读的是 NOR 那一份**（用 `--lcd` 对照实验证明：把 NAND 里的 inode 52 改成
`0xC8D`、NOR 保持原样 → 仍显示空目录；反过来 NOR 日志被整理过、NAND 不动 → 能列出文件）。

### 8.3 一次"下载 `/midi/音乐1.mid`"落盘的全部字节

| 位置 | 内容 |
|---|---|
| NAND 扇区 `0xC8D` 页 0 +0x10 | 目录项 `83 00 "音乐1.mid"`（inode 131） |
| NAND 扇区 `0xC95` | 文件数据 16384 字节（16KB 正好一个扇区） |
| NAND 扇区 `0xC8D` 页 0 的 spare +6 | `02 02 00 AA CC`（页状态标记） |
| 新写页的 spare +6 | `00 00 00 AA 00`（页已占用标记） |
| NOR 日志区 | `<131→0xC95>`、`<52(/midi)→0xC8D>`，并把旧的那条 52 记录 `st` 改成 `0x40` |
| NOR（`INT $051C` 之后） | 一整套新世代表（本例写到 `0xFC000`） |

### 8.4 复现

```powershell
powershell -NoProfile -File tools\nc3k_put.ps1 -Rom roms\nc3000_dl `
    -File "info\NC3000原机的midi\音乐1.mid" -Name "/midi/音乐1.mid"
```

验收（无头）：开机 → `(0,0)` 网络 → `↓↓↓` → 输入 → 列表出现 `音乐1.mid`；
再按一次输入即开始播放（061 收到 1156 字节）。
证据图：`out/nc3000/seq_final/list3.bmp`（音乐1+音乐2）、`out/nc3000/seq_play/`。

### 8.5 附带更正第 5 节的旧结论

第 5 节说"逻辑块号 → 物理偏移不是线性映射"——**不成立**。实测是恒等：
`FS 扇区 N ↔ NAND 设备页 32*N`，目录项就写在 `blk[0]` 所指扇区的页 0 的 +0x10。
之前以为"不线性"，是因为把 inode 表所在的扇区 0x2 误当成了普通数据区。
