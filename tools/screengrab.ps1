<#
屏幕区域截图（比 PrintWindow 更可靠）。
用法: powershell -File tools\screengrab.ps1 <x> <y> <w> <h> <out.png>
不传参数则截主屏全屏。
#>
param(
    [Parameter(Position = 0)][int]$X = -1,
    [Parameter(Position = 1)][int]$Y = -1,
    [Parameter(Position = 2)][int]$W = -1,
    [Parameter(Position = 3)][int]$H = -1,
    [Parameter(Position = 4)][string]$Out = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
if ($X -lt 0) { $X = $b.X; $Y = $b.Y; $W = $b.Width; $H = $b.Height }
if (-not $Out) { $Out = "out\nc3000\screen_full.png" }
$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$bmp = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($X, $Y, 0, 0, (New-Object System.Drawing.Size($W, $H)))
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "SAVED $Out  region=($X,$Y,$W,$H) screen=$($b.Width)x$($b.Height)"
