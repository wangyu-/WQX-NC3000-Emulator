# nc3000_word2wav.ps1 - decode a frame range of the NC3000's sysdir/celp_data
# (the S600 word-pronunciation stream) into a listenable WAV.
#
#   .\tools\nc3000_word2wav.ps1 <startFrame> <frameCount> [out.wav] [-Inode 26]
#                               [-Gain 1.0] [-Trim] [-Tail 1.0]
#
# 24 ms per frame at the S600 playback rate (36,790 Hz), ~828 DAC samples per
# frame.  Needs the ready-line fix in nc3000_word.c
# (docs/archive/交接文档_2026-09-24_09-26.md -> 交接文档_20260925.md §3.11).
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][int]$StartFrame,
    [Parameter(Mandatory = $true, Position = 1)][int]$FrameCount,
    [Parameter(Position = 2)][string]$Out = "",
    [int]$Inode = 26,
    [double]$Gain = 1.0,
    [ValidateRange(0, 4)][int]$Speed = 2,
    [switch]$Trim,
    [double]$Tail = 1.0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $root "spce061a\build\word.exe"
if (-not (Test-Path $probe)) {
    throw "probe not found: $probe`nbuild it with:`n  tcc -O2 -I emu -o build\word.exe host\nc3000_dsp.c host\nc3000_word.c emu\spce061a.c emu\unsp.c emu\firmware_061.c"
}
if (-not $Out) {
    $Out = Join-Path $root ("out\NC3000_单词_帧{0}_{1}帧.wav" -f $StartFrame, $FrameCount)
}
$Out = [IO.Path]::GetFullPath($Out)

$tmp = Join-Path $env:TEMP ("nc3000w_{0}" -f $PID)
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$slice = Join-Path $tmp "slice.bin"
$raw = Join-Path $tmp "raw.wav"

& node (Join-Path $PSScriptRoot "celp_frames.js") $StartFrame $FrameCount $slice $Inode | Write-Host

# 0x55 speed level: the host table at $FB16 is {06,09,0C,0F,12}; level 2
# (=0x0C) is the chip's own default (set by the stop/idle routine 0xC41E).
$env:N3_SPEED = "$Speed"

Push-Location (Join-Path $root "spce061a")
try {
    & $probe "build\word_rec.bin" $raw 27 $slice 0x21 $FrameCount 0 |
        Select-String -Pattern "mode 27:|wrote|done" | ForEach-Object { Write-Host $_.Line }
} finally { Pop-Location }
if (-not (Test-Path $raw)) { throw "probe produced no wav" }

# --- post-process: optional trim to content, fade, gain, write mono 16-bit ---
$bytes = [IO.File]::ReadAllBytes($raw)
$rate = [BitConverter]::ToInt32($bytes, 24)
$n = ([BitConverter]::ToInt32($bytes, 40)) / 2
$s = New-Object 'double[]' $n
for ($i = 0; $i -lt $n; $i++) { $s[$i] = [BitConverter]::ToInt16($bytes, 44 + 2 * $i) }

$a = 0; $b = $n - 1
if ($Trim) {
    # frame-level RMS, so the decode's leading/trailing transient is what gets cut
    $win = [int]($rate * 0.02)
    $nf = [int]($n / $win)
    $e = New-Object 'double[]' $nf
    for ($k = 0; $k -lt $nf; $k++) {
        $sum = 0.0
        for ($i = $k * $win; $i -lt ($k + 1) * $win; $i++) { $sum += $s[$i] * $s[$i] }
        $e[$k] = [Math]::Sqrt($sum / $win)
    }
    $mx = ($e | Measure-Object -Maximum).Maximum
    $thr = $mx * 0.08
    $k0 = 0; while ($k0 -lt $nf -and $e[$k0] -lt $thr) { $k0++ }
    $k1 = $nf - 1; while ($k1 -gt $k0 -and $e[$k1] -lt $thr) { $k1-- }
    $a = $k0 * $win; $b = [Math]::Min($n - 1, ($k1 + 1) * $win - 1)
    $pad = [int]($rate * 0.03)
    $a = [Math]::Max(0, $a - $pad); $b = [Math]::Min($n - 1, $b + $pad)
}
$len = $b - $a + 1
$fadeIn = [int]($rate * 0.006); $fadeOut = [int]($rate * [Math]::Min(0.02, $Tail * 0.05))
$outBuf = New-Object byte[] (44 + 2 * $len)
[Text.Encoding]::ASCII.GetBytes("RIFF").CopyTo($outBuf, 0)
[BitConverter]::GetBytes(36 + 2 * $len).CopyTo($outBuf, 4)
[Text.Encoding]::ASCII.GetBytes("WAVEfmt ").CopyTo($outBuf, 8)
[BitConverter]::GetBytes(16).CopyTo($outBuf, 16)
[BitConverter]::GetBytes([int16]1).CopyTo($outBuf, 20)
[BitConverter]::GetBytes([int16]1).CopyTo($outBuf, 22)
[BitConverter]::GetBytes($rate).CopyTo($outBuf, 24)
[BitConverter]::GetBytes($rate * 2).CopyTo($outBuf, 28)
[BitConverter]::GetBytes([int16]2).CopyTo($outBuf, 32)
[BitConverter]::GetBytes([int16]16).CopyTo($outBuf, 34)
[Text.Encoding]::ASCII.GetBytes("data").CopyTo($outBuf, 36)
[BitConverter]::GetBytes(2 * $len).CopyTo($outBuf, 40)
for ($i = 0; $i -lt $len; $i++) {
    $v = $s[$a + $i] * $Gain
    if ($i -lt $fadeIn) { $v *= $i / $fadeIn }
    if ($i -ge $len - $fadeOut) { $v *= ($len - $i) / $fadeOut }
    if ($v -gt 32767) { $v = 32767 } elseif ($v -lt -32768) { $v = -32768 }
    [BitConverter]::GetBytes([int16][Math]::Round($v)).CopyTo($outBuf, 44 + 2 * $i)
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
[IO.File]::WriteAllBytes($Out, $outBuf)
Write-Host ("wrote {0}: {1} samples @ {2} Hz ({3:N2} s)" -f $Out, $len, $rate, ($len / $rate))
Remove-Item -Recurse -Force $tmp
