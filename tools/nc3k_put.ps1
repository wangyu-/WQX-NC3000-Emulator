<#
给 NC3000 模拟器的虚拟磁盘加一个文件（下载到文曲星）。

用法:
  powershell -File tools\nc3k_put.ps1 -Rom roms\workF -File "info\NC3000原机的midi\音乐1.mid" -Name "/midi/音乐1.mid"

-Rom   模拟器 rom 目录（里面要有 nc3000.nor / nc3000.nand / nc3000.nand0）
-Name  文曲星里的目标路径；可以带目录（例如 /midi/xxx.mid），也可以只写文件名（进根目录）
运行完会把修改后的 nor/nand 直接写回 -Rom 目录（原文件先备份成 *.bak）。
#>
param(
    [Parameter(Mandatory = $true)][string]$Rom,
    [Parameter(Mandatory = $true)][string]$File,
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$WaitMs = 45000
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$py = "C:\Users\YYGSM\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if (!(Test-Path $py)) { $py = "python" }

$romBase = Join-Path $root $Rom
if (!(Test-Path $romBase)) { throw "rom dir not found: $romBase" }
$exe = Join-Path $root "src_nc3000\nc3000_headless.exe"
$romPath = Join-Path $romBase "nc3000"

$hostFile = $File
if (![System.IO.Path]::IsPathRooted($hostFile)) { $hostFile = Join-Path $root $File }
if (!(Test-Path $hostFile)) { throw "file not found: $hostFile" }

$work = Join-Path $root "out\nc3000\put"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$cmdFile = Join-Path $work "cmd.bin"
$staged  = Join-Path $work "src.bin"

# 宿主文件先复制成纯 ASCII 路径：模拟器用 fopen(ANSI) 打开，中文路径会失败
Copy-Item -LiteralPath $hostFile -Destination $staged -Force
# 命令文件用 GBK 原始字节写，文件名里可以有汉字
$mk = Join-Path $root "tools\mk_cmdfile.py"
Write-Host "python : $py"
Write-Host "mkmake : $mk"
& $py $mk $cmdFile "put ..\out\nc3000\put\src.bin $Name"
if ($LASTEXITCODE -ne 0) { throw "mk_cmdfile failed (exit $LASTEXITCODE)" }
if (!(Test-Path $cmdFile)) { throw "cmd file not created: $cmdFile" }

foreach ($ext in @("nor", "nand")) {
    $p = "$romPath.$ext"
    if (Test-Path $p) { Copy-Item $p "$p.bak" -Force }
}

Push-Location (Join-Path $root "src_nc3000")
try {
    & $exe $romPath --ms $WaitMs --hold 60 `
        --press 4000 5 6 --press 14000 5 1 `
        --cmd-file 18000 $cmdFile `
        --save-flash $romPath 2>&1 |
        Select-String -Pattern "put\]|flash saved|unexpected|assert"
} finally { Pop-Location }

Write-Host "done: $Name -> $romBase"
