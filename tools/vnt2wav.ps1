# vnt2wav.ps1 - decode a 一般品质 (S200) .vnt / .bin into a 8 kHz mono WAV.
#
# Uses the *direct-drive* S200 path in spce061a\host\s200_map.c, which is the
# one that has been verified against the official S200.exe reference:
#   frame energy envelope correlation 0.9911 (500 frames)
#   waveform correlation 0.95~0.98 at a fixed -9 sample offset
# The firmware's own ROM-protocol path (s200_probe) mis-feeds the stream and
# produces railing DAC bursts, so do NOT use it for extraction.
#
#   .\tools\vnt2wav.ps1 info\视听教材\...\一般品质\中级读物-2.vnt
#   .\tools\vnt2wav.ps1 in.vnt out.wav -MaxFrames 500
#
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$In,
    [Parameter(Position = 1)][string]$Out = "",
    [int]$MaxFrames = 0,
    [string]$Probe = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $Probe) { $Probe = Join-Path $root "spce061a\build\s200_mapr.exe" }
if (-not (Test-Path $Probe)) {
    throw "probe not found: $Probe`nbuild it with:`n  tcc -O2 -I emu -o build\s200_mapr.exe host\s200_map.c emu\spce061a.c emu\unsp.c emu\firmware_061.c"
}

$inFull = (Resolve-Path -LiteralPath $In).Path
$bytes  = [IO.File]::ReadAllBytes($inFull)
if ($bytes.Length -lt 8) { throw "file too small: $inFull" }

$len = [BitConverter]::ToUInt32($bytes, 0)
$cfg = [BitConverter]::ToUInt16($bytes, 4)
# 0x0008 = the 视听教材 一般品质 .vnt; 0x0001 = the official .s20 (TC2000/NC3001
# demo resources, e.g. lrc_800.s20); 0x0004 = the NC3001 dictionary word audio
# (TV_DATA.BIN, see NC3001/EZSrc/cedict/YHLib.c).  All carry the same S200
# 5-byte/frame stream, so the container config only picks the rate/quality.
if ($cfg -ne 0x0008 -and $cfg -ne 0x0001 -and $cfg -ne 0x0004) {
    throw ("container config = 0x{0:x4}; this build decodes 0x0008, 0x0004 and 0x0001 (S200). " -f $cfg) +
          "A .tnt file is text, not audio; an A1600 stream needs a different decoder."
}

$frames = [int](($bytes.Length - 6) / 5)
if ($MaxFrames -gt 0 -and $MaxFrames -lt $frames) { $frames = $MaxFrames }
if ($frames -le 0) { throw "no frames in $inFull" }
if (-not $Out) { $Out = [IO.Path]::ChangeExtension($inFull, ".wav") }

$pcmSrc = Join-Path $root "out\s200_emu_021F.pcm"
Write-Host ("decoding {0} frames ({1:N1} s) ..." -f $frames, ($frames * 160 / 8000))
Remove-Item -LiteralPath $pcmSrc -ErrorAction SilentlyContinue
# the probe writes its pcm to "..\out\" relative to its working directory
Push-Location (Join-Path $root "spce061a")
try { & $Probe $inFull 0 $frames 0 0 0 | Out-Null } finally { Pop-Location }
if (-not (Test-Path $pcmSrc)) { throw "probe did not produce $pcmSrc" }

$pcm  = [IO.File]::ReadAllBytes($pcmSrc)
$want = $frames * 160 * 2
if ($pcm.Length -lt $want) {
    Write-Warning ("probe produced only {0} bytes, expected {1} - stream may be truncated or invalid" -f $pcm.Length, $want)
}
$rate = 8000
$h    = New-Object byte[] 44
[Text.Encoding]::ASCII.GetBytes("RIFF").CopyTo($h, 0)
[BitConverter]::GetBytes([int](36 + $pcm.Length)).CopyTo($h, 4)
[Text.Encoding]::ASCII.GetBytes("WAVEfmt ").CopyTo($h, 8)
[BitConverter]::GetBytes([int]16).CopyTo($h, 16)
[BitConverter]::GetBytes([int16]1).CopyTo($h, 20)
[BitConverter]::GetBytes([int16]1).CopyTo($h, 22)
[BitConverter]::GetBytes([int]$rate).CopyTo($h, 24)
[BitConverter]::GetBytes([int]$rate).CopyTo($h, 28)
[BitConverter]::GetBytes([int16]2).CopyTo($h, 32)
[BitConverter]::GetBytes([int16]16).CopyTo($h, 34)
[Text.Encoding]::ASCII.GetBytes("data").CopyTo($h, 36)
[BitConverter]::GetBytes([int]$pcm.Length).CopyTo($h, 40)

$fs = [IO.File]::Create($Out)
$fs.Write($h, 0, 44)
$fs.Write($pcm, 0, $pcm.Length)
$fs.Close()

$n = [int]($pcm.Length / 2)
$sum = 0.0; $peak = 0
for ($i = 0; $i -lt $n; $i++) {
    $v = ([int]$pcm[2 * $i]) -bor (([int]$pcm[2 * $i + 1]) -shl 8)
    if ($v -ge 32768) { $v -= 65536 }
    $sum += [double]$v * $v
    if ([Math]::Abs($v) -gt $peak) { $peak = [Math]::Abs($v) }
}
Write-Host ("wrote {0}`n  {1} samples, {2:N1} s @ {3} Hz, RMS {4:N1}, peak {5}" -f `
    $Out, $n, ($n / $rate), $rate, [Math]::Sqrt($sum / $n), $peak)
