<#
在无头模拟器里穷举 keypadmatrix[y][x]，看固件把 $C7 解成什么。
用法:
  powershell -File tools\sweep_keys.ps1 [-Y0 0] [-Y1 7] [-X0 0] [-X1 15]
输出每个坐标被写入 $C7 的键值（过滤掉常见的 86/06/00 噪声）。
#>
param(
    [int]$Y0 = 0, [int]$Y1 = 7, [int]$X0 = 0, [int]$X1 = 15,
    [int]$PressMs = 13500, [int]$Ms = 19000
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = Join-Path $root "src_nc3000\nc3000_headless.exe"
$rom = Join-Path $root "roms\lee2\nc3000"

Push-Location (Join-Path $root "src_nc3000")
try {
    for ($y = $Y0; $y -le $Y1; $y++) {
        for ($x = $X0; $x -le $X1; $x++) {
            $out = & $exe $rom --ms $Ms --hold 2500 `
                --press 4000 5 6 --press $PressMs $y $x `
                --watch-write 00c7 2>&1
            $vals = @()
            foreach ($line in $out) {
                $v = $null; $pc = "----"
                if ($line -match 'write \$00C7 = ([0-9A-F]{2})') { $v = $matches[1] }
                if ($line -match 'by PC=\$([0-9A-F]{4})') { $pc = $matches[1] }
                if ($v) { $vals += ("{0}@{1}" -f $v, $pc) }
            }
            $s = if ($vals.Count) { $vals -join " " } else { "(none)" }
            Write-Output ("({0},{1})  {2}" -f $y, $x, $s)
        }
    }
} finally { Pop-Location }
