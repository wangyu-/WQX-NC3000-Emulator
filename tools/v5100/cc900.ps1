<#
    V5100 / TLCS-900 (Toshiba T900) C 编译器 —— 驱动替代脚本

    背景：
      原厂 CC900.EXE 是“驱动”，它用  "-z@" + 环境块 的方式把参数传给
      THC1/THC2/ASM900。Windows 10/11 的 CreateProcess 会校验环境块
      （每个条目必须是 NAME=VALUE），因此 CC900 在本机必然失败：
        cc900-Fatal-116: cannot execute "...\bin\thc1"
      （实测 CreateProcessA 返回 0，GetLastError = 87 ERROR_INVALID_PARAMETER）

    本脚本按 2006 年原厂 TIDE 工程 (v6100.TBP / demo.asm 头部注释) 的实际
    参数，直接依次调用三个阶段：
        THC1   解析 C 源码        -> 中间文件 (.i)
        THC2   代码生成           -> 汇编源码 (.asm)
        ASM900 汇编               -> 可重定位目标 (.rel)

    用法：
      .\cc900.ps1 -Source ..\..\Gui_smulator\Application\aptest\demo.c -Out demo.rel `
                 -Include .\include,..\Gui_smulator\mwingui `
                 -Define PIC_PID=1
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]   $Source,
    [string]                                 $Out,
    [string[]]                               $Include = @(),
    [string[]]                               $Define  = @(),
    [string]                                 $Codegen = '-O0',
    [switch]                                 $NoDebug,
    [string]                                 $ToolRoot = (Join-Path $PSScriptRoot '..\ngpc\ngpc\T900'),
    [switch]                                 $KeepIntermediate
)

$ErrorActionPreference = 'Stop'
$ToolRoot = (Resolve-Path $ToolRoot).Path
$bin      = Join-Path $ToolRoot 'BIN'
$sysInc   = Join-Path $ToolRoot 'INCLUDE'

foreach ($tool in 'THC1.EXE', 'THC2.EXE', 'ASM900.EXE') {
    if (-not (Test-Path (Join-Path $bin $tool))) { throw "找不到 $tool（$bin）" }
}

$env:THOME = $ToolRoot
$env:PATH  = "$bin;$env:PATH"

$src = (Resolve-Path $Source).Path
if (-not $Out) { $Out = [IO.Path]::ChangeExtension($src, '.rel') }
$outDir = Split-Path -Parent $Out
if (-not $outDir) { $outDir = (Get-Location).Path }
$outDir = (Resolve-Path $outDir).Path
$outAbs = Join-Path $outDir ([IO.Path]::GetFileName([IO.Path]::ChangeExtension($Out, '.rel')))

# 原厂工具只认 DOS 换行；LF-only 的源码会被 ASM900 判为 "Illegal source file format"
$work = Join-Path ([IO.Path]::GetTempPath()) ("v5100cc_" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $work | Out-Null
$srcCrlf = Join-Path $work ([IO.Path]::GetFileName($src))
[IO.File]::WriteAllText($srcCrlf, ([IO.File]::ReadAllText($src) -replace "`r`n", "`n" -replace "`n", "`r`n"))

$iFile   = Join-Path $work 'stage1.i'
$asmFile = Join-Path $work 'stage2.asm'

$incPaths = @()
foreach ($p in $Include) {
    $incPaths += if (Test-Path $p) { (Resolve-Path $p).Path } else { $p }
}
# 源码被复制到临时目录（统一换成 CRLF），因此要把原目录加入搜索路径，
# 否则 #include "xxx.h" 这类同目录引用会失败
$srcDir = Split-Path $src -Parent
$incArgs = @()
foreach ($p in ($incPaths + $srcDir + $sysInc)) { $incArgs += @('-I', $p) }

# 注意：ASM900 内部只支持 4 个 -I，传第 5 个会直接崩（0xC0000005）。
# 生成的 .asm 里没有 $INCLUDE 指令，-I 只用于解析调试信息里的绝对路径，
# 因此这里按原厂工程的做法只给「用户 include + T900 include」。
$asmIncArgs = @()
foreach ($p in (($incPaths + $sysInc) | Select-Object -First 4)) { $asmIncArgs += @('-I', $p) }
$defArgs = @(); foreach ($d in $Define) { $defArgs += "-D$d" }
$dbg     = if ($NoDebug) { @() } else { @('-g') }

# ---- 阶段 1：解析（THC1）--------------------------------------------------
$parserOpts = @('-w2') + $dbg + @('-Xc', '-Xp2', '-XS4', '-Nb1', '-XO', '-XM') + $defArgs + $incArgs
$r1 = & (Join-Path $bin 'THC1.EXE') @parserOpts $srcCrlf $iFile 2>&1
if ($LASTEXITCODE -ne 0) { $r1 | ForEach-Object { Write-Host $_ }; throw "THC1 失败 (exit=$LASTEXITCODE)" }

# ---- 阶段 2：代码生成（THC2）---------------------------------------------
$codeOpts = @('-w1') + ($Codegen -split '\s+') + $dbg + @('-XS4', '-Nb1', '-XO', '-XM')
$r2 = & (Join-Path $bin 'THC2.EXE') @codeOpts $iFile $asmFile 2>&1
if ($LASTEXITCODE -ne 0) { $r2 | ForEach-Object { Write-Host $_ }; throw "THC2 失败 (exit=$LASTEXITCODE)" }

# ---- 阶段 3：汇编（ASM900）-----------------------------------------------
# ASM900 把 .rel 写在“当前目录”，因此切到输出目录执行
Push-Location $outDir
try {
    $asmOpts = @('-w1') + $dbg + $asmIncArgs + @('-Nb3', '-XE', $asmFile)
    $r3 = & (Join-Path $bin 'ASM900.EXE') @asmOpts 2>&1
    if ($LASTEXITCODE -ne 0) { $r3 | ForEach-Object { Write-Host $_ }; throw "ASM900 失败 (exit=$LASTEXITCODE)" }
}
finally { Pop-Location }

$produced = Join-Path $outDir ([IO.Path]::GetFileNameWithoutExtension($asmFile) + '.rel')
if (-not (Test-Path $produced)) { throw "汇编未生成目标文件" }
if ($produced -ne $outAbs) { Move-Item -LiteralPath $produced -Destination $outAbs -Force }

Get-ChildItem -LiteralPath $work -Recurse -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

$warn = ($r1 | Where-Object { $_ -match 'Warning' }).Count + ($r3 | Where-Object { $_ -match 'Warning' }).Count
Write-Host ("[OK] {0} -> {1}  ({2} bytes, {3} warnings)" -f (Split-Path $src -Leaf), (Split-Path $outAbs -Leaf), (Get-Item $outAbs).Length, $warn)
