# Builds the emulator's rom set for NC3000 out of the raw dumps in info/.
#
#   info/NC3KSYSNOR.nor   557,056 B = 17 x 32 KB NOR banks, in "CPU window" order
#                         (bank N >= 1 has its two 16 KB halves swapped relative
#                          to the chip's linear order - see
#                          docs/NC3KSYSNOR_结构分析.md section 3.2)
#   info/NC3KSYSNAND.nand 53,779,970 B = raw NAND image, **528 B pages**
#                         (512 B data + 16 B spare).  The spare area carries
#                         real content - NGFFS keeps page/block state there -
#                         so this file must NOT be re-paginated.
#
# The emulator wants
#   <rom>.nor    1 MB (0x100000), banks back to back *in chip linear order*
#   <rom>.nand0  first 64 pages of the NAND, 528 B each (the emulator keeps
#                that small "boot area" in its own file)
#   <rom>.nand   everything after that, still 528 B per page
#
# usage:
#   powershell -File tools/nc3000_rom_prep.ps1                 # chip linear NOR (default)
#   powershell -File tools/nc3000_rom_prep.ps1 -NorOrder raw   # keep the dump's order
param(
    [string]$Info = 'info',
    [string]$OutDir = 'roms',
    [ValidateSet('chips', 'raw')][string]$NorOrder = 'chips'
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$info = Join-Path $root $Info
$out  = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null

$BANK   = 0x8000
$HALF   = 0x4000
$NORPHY = 0x100000        # 1 MB, 32 banks
$PAGE   = 528
$PAGES  = 131072          # 64 MB / 512 B

# ---------------------------------------------------------------- NOR
$src = Join-Path $info 'NC3KSYSNOR.nor'
$norRaw = [System.IO.File]::ReadAllBytes($src)
Write-Host ("nor dump: {0} bytes ({1} banks)" -f $norRaw.Length, ($norRaw.Length / $BANK))

$nor = [byte[]](,0xFF * $NORPHY)

$banksInDump = [int]($norRaw.Length / $BANK)
for ($b = 0; $b -lt $banksInDump; $b++) {
    $s = $b * $BANK
    if ($NorOrder -eq 'raw' -or $b -eq 0) {
        # bank 0 is sequential on the chip, so raw and chip order agree
        [Array]::Copy($norRaw, $s, $nor, $s, $BANK)
    } else {
        # swap the two 16 KB halves
        [Array]::Copy($norRaw, $s,        $nor, $s + $HALF, $HALF)
        [Array]::Copy($norRaw, $s + $HALF, $nor, $s,        $HALF)
    }
}
# in chip order the last 4 KB of bank 0 (0x7000..0x7fff) is the BIOS/vector block

# ---------------------------------------------------------------- 产品信息块
#
# 固件在"按输入进主菜单"之前会去读 NOR **0xED000**（bank 0x1D）的这块"产品信息"，
# 格式（与模拟器 nor.cpp 里的 nor_info_block0 同一套）：
#
#     金远见(6B) 'N' 'C' 机型(2B,小端) 'J' 01 00 00 00 4F 05 09 03 01 …
#
# 机型字段就是**型号的十进制值**：NC2000=0x07D0、NC2600=0x0A28、NC3000=0x0BB8。
# 老 dump（info/NC3KSYSNOR.nor）只有 bank 0x00~0x10 = 0x88000 字节，这一块没被导出，
# 于是固件读到全 0xFF，会先弹一句 **"产品信息有误 请接洽代理商"** 才进主菜单
# （用户 2026-09-27 反馈；实测把这块补上提示就消失）。
#
# 注意：这里补出来的是**合成值**（机型/语言/版本按 NC3000 3.4 填，序列号沿用参照 ROM 的），
# 真机那块里的序列号只有完整 dump 才能拿到。脚本会在补齐时打印提示。

$infoAddr = 0xED000
$infoLen  = 0x100
$prodInfoList = @(
    0xBD,0xF0,0xD4,0xB6,0xBC,0xFB    # 金远见
    0x4E,0x43                        # NC
    0xB8,0x0B                        # 机型 = 0x0BB8 = 3000
    0x4A                             # 'J' = 简体
    0x01,0x00,0x00,0x00,0x4F,0x05,0x09,0x03,0x01
)
$prodInfo = [byte[]]$prodInfoList
$prodInfoSrc = 'synthesised'

if ($norRaw.Length -gt $infoAddr + 8 -and
    -not ($norRaw[$infoAddr] -eq 0xFF -and $norRaw[$infoAddr + 1] -eq 0xFF)) {
    $prodInfoSrc = 'from dump（原样保留）'
} else {
    for ($i = 0; $i -lt $infoLen; $i++) { $nor[$infoAddr + $i] = 0xFF }
    for ($i = 0; $i -lt $prodInfo.Length; $i++) { $nor[$infoAddr + $i] = $prodInfo[$i] }
    Write-Host ("  [产品信息] dump 没覆盖 0x{0:X}（只有 {1} 字节）→ 已补一块合成值（机型 NC3000）" -f $infoAddr, $norRaw.Length)
    Write-Host "             真机那块里的序列号需要完整 dump 才能拿到；要还原就重新 dump bank 0x1D。"
}
Write-Host ("  product info block: {0}" -f $prodInfoSrc)

$norOut = Join-Path $out 'nc3000.nor'
[System.IO.File]::WriteAllBytes($norOut, $nor)
Write-Host ("wrote {0} ({1} bytes, order={2})" -f $norOut, $nor.Length, $NorOrder)

Write-Host "  check: reset vector (bank0+0x7ffc) = " -NoNewline
Write-Host ("{0:X2}{1:X2}" -f $nor[0x7ffd], $nor[0x7ffc])

# ---------------------------------------------------------------- NAND
$nandSrc = Join-Path $info 'NC3KSYSNAND.nand'
$raw = [System.IO.File]::ReadAllBytes($nandSrc)
$pages = [int]($raw.Length / $PAGE)
$tail = $raw.Length - $pages * $PAGE
Write-Host ("nand dump: {0} bytes = {1} pages of {2} B (+{3} trailing bytes ignored)" -f `
            $raw.Length, $pages, $PAGE, $tail)
Write-Host ("  covers {0:N1} MB of the 64 MB device; the rest stays erased" -f `
            ($pages * 512 / 1MB))

$nand0Out = Join-Path $out 'nc3000.nand0'
$nandOut  = Join-Path $out 'nc3000.nand'
$split = 64 * $PAGE

<#
  ★ 关键：dump 到底含不含前 64 页？

  NC3000 的 NGFFS 把第一张 inode 表放在**设备第 64 页**（= 模拟器 .nand 文件的第 0 页），
  所以：
    * 若 dump 的第 0 字节就是 inode 1 的记录（01 00 xx xx ... 30/31 字节处 a8 a8），
      说明这份 dump **本身就是 **".nand"（从设备第 64 页开始 dump 的），
      那就不能再砍掉 64 页——否则整份镜像会错位 64 页（= 33,792 字节），
      固件在"闪存中有数据 清除吗？"选 N 之后会因为找不到文件系统而掉进它自己的
      调试监视器（屏幕只剩一个 ">"）。2026-09-27 用户就是踩到这个。
    * 否则按"含前 64 页"处理，切成 .nand0 + .nand。
#>
$looksLikeNandBody = ($raw[0] -eq 0x01) -and ($raw[1] -eq 0x00) -and `
                     ($raw[0x1E] -eq 0xA8) -and ($raw[0x1F] -eq 0xA8)
if ($looksLikeNandBody) {
    Write-Host "  dump starts at device page 64 (inode table at offset 0) -> use it as .nand as-is"
    [System.IO.File]::WriteAllBytes($nandOut,  $raw[0..($pages * $PAGE - 1)])
    # .nand0 让模拟器自己合成（nand.cpp 会写入 "ggv nc3000" 签名），这里给一页全 FF 即可
    [System.IO.File]::WriteAllBytes($nand0Out, [byte[]](,0xFF * $split))
    Write-Host ("wrote {0} (64 pages of FF) and {1} ({2} pages)" -f $nand0Out, $nandOut, $pages)
} else {
    Write-Host "  dump includes device pages 0.. -> split off .nand0"
    [System.IO.File]::WriteAllBytes($nand0Out, $raw[0..($split - 1)])
    [System.IO.File]::WriteAllBytes($nandOut,  $raw[$split..($pages * $PAGE - 1)])
    Write-Host ("wrote {0} (64 pages) and {1} ({2} pages)" -f $nand0Out, $nandOut, ($pages - 64))
    Write-Host ("  check: page 64 data begins with {0:X2} {1:X2} (expect 01 00 = inode #1)" -f `
                $raw[$split], $raw[$split + 1])
}
Write-Host 'done'
