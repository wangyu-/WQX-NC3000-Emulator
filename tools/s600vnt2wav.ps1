# s600vnt2wav.ps1 - decode an S600 .vnt (18-byte frames, e.g. 试听教材ks100) into a WAV.
#
# The ROM path only decodes reliably when the host waits for the chip's ready
# line for *real* (see docs\NC3000_S600规范.md §4.28 - the old 400-iteration
# timeout fed ~46x faster than the content rate and produced railed noise).
# It also works best on bounded chunks that start at a "padding run" (the
# repeated identical frames between sentences), so this script splits there.
#
#   .\tools\s600vnt2wav.ps1 <in.vnt> [out.wav] [-MaxFrames 200] [-MaxChunks 0]
#
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$In,
    [Parameter(Position = 1)][string]$Out = "",
    [int]$MaxFrames = 200,     # frames per chunk
    [int]$MaxChunks = 0,       # 0 = all
    [string]$Probe = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Probe) { $Probe = Join-Path $root "spce061a\build\word.exe" }
if (-not (Test-Path $Probe)) {
    throw "probe not found: $Probe`nbuild it with:`n  tcc -O2 -I emu -o build\word.exe host\nc3000_dsp.c host\nc3000_word.c emu\spce061a.c emu\unsp.c emu\firmware_061.c"
}

$inFull = (Resolve-Path -LiteralPath $In).Path
$bytes = [IO.File]::ReadAllBytes($inFull)
$frames = [int]($bytes.Length / 18)
if ($frames -le 0) { throw "no frames in $inFull" }
if (-not $Out) { $Out = [IO.Path]::ChangeExtension($inFull, ".wav") }

$RATE = 36790                 # playback DAC rate (TimerA 0xFD64)
$SAMPLES_PER_FRAME = [int]($RATE * 0.024)   # 882.96 -> 882

# ---- 1. find the padding runs (>=3 identical consecutive frames) -------------
function Get-Frame([int]$i) {
    $o = $i * 18
    return [BitConverter]::ToString($bytes, $o, 18)
}
$runs = New-Object System.Collections.ArrayList
$s = 0
for ($i = 1; $i -le $frames; $i++) {
    if ($i -eq $frames -or (Get-Frame $i) -ne (Get-Frame ($i - 1))) {
        if (($i - $s) -ge 3) { $null = $runs.Add($s) }
        $s = $i
    }
}
$bounds = New-Object System.Collections.Generic.List[int]
$bounds.Add(0)
foreach ($r in $runs) {
    if ([int]$r -gt ([int]$bounds[$bounds.Count - 1] + 8)) { $bounds.Add([int]$r) }
}
$bounds.Add($frames)

# cut the segments into chunks of at most MaxFrames frames
$chunkFrom = New-Object System.Collections.Generic.List[int]
$chunkLen = New-Object System.Collections.Generic.List[int]
for ($b = 0; $b -lt ($bounds.Count - 1); $b++) {
    $from = [int]$bounds[$b]; $to = [int]$bounds[$b + 1]
    while (($to - $from) -gt $MaxFrames) {
        $chunkFrom.Add($from); $chunkLen.Add($MaxFrames); $from += $MaxFrames
    }
    if (($to - $from) -gt 0) { $chunkFrom.Add($from); $chunkLen.Add($to - $from) }
}
if ($MaxChunks -gt 0 -and $chunkFrom.Count -gt $MaxChunks) {
    $chunkFrom = $chunkFrom.GetRange(0, $MaxChunks)
    $chunkLen = $chunkLen.GetRange(0, $MaxChunks)
}
$nChunks = $chunkFrom.Count
Write-Host ("decoding {0} frames in {1} chunks (<= {2} frames each)" -f $frames, $nChunks, $MaxFrames)

$tmp = Join-Path $env:TEMP ("s600_{0}" -f $PID)
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$ms = New-Object System.IO.MemoryStream
$wrote = 0
$ci = 0
for ($k = 0; $k -lt $nChunks; $k++) {
    $from = [int]$chunkFrom[$k]; $nf = [int]$chunkLen[$k]; $ci++
    $slice = Join-Path $tmp "c$ci.vnt"
    $wav = Join-Path $tmp "c$ci.wav"
    $buf = New-Object byte[] ($nf * 18)
    [Array]::Copy($bytes, $from * 18, $buf, 0, $nf * 18)
    [IO.File]::WriteAllBytes($slice, $buf)
    Push-Location (Join-Path $root "spce061a")
    try {
        & $Probe "build\word_rec.bin" $wav 27 $slice 0x21 $nf 0 0 0 0 0 0 0 0 0 0 x 0 0 |
            Out-Null
    } finally { Pop-Location }
    if (-not (Test-Path $wav)) { Write-Warning "chunk $ci produced no wav"; continue }
    $pcm = [IO.File]::ReadAllBytes($wav)
    $n = [int](($pcm.Length - 44) / 2)
    $want = $nf * $SAMPLES_PER_FRAME
    if ($n -gt $want) { $n = $want }
    # Kill the seam click: a chunk boundary is a hard step (the sentence tail
    # jumps to the next chunk), so ramp in 1 ms at the head and out 3.5 ms at
    # the tail of every chunk.
    $fi = [int]($RATE * 0.001); $fo = [int]($RATE * 0.0035)
    for ($i = 0; $i -lt $fi -and $i -lt $n; $i++) {
        $o = 44 + 2 * $i
        $s = [int]$pcm[$o] -bor ([int]$pcm[$o + 1] -shl 8)
        if ($s -ge 32768) { $s -= 65536 }
        $s = [int]($s * $i / $fi)
        if ($s -lt 0) { $s += 65536 }
        $pcm[$o] = [byte]($s -band 0xFF); $pcm[$o + 1] = [byte](($s -shr 8) -band 0xFF)
    }
    for ($i = 0; $i -lt $fo -and $i -lt $n; $i++) {
        $o = 44 + 2 * ($n - 1 - $i)
        $s = [int]$pcm[$o] -bor ([int]$pcm[$o + 1] -shl 8)
        if ($s -ge 32768) { $s -= 65536 }
        $s = [int]($s * $i / $fo)
        if ($s -lt 0) { $s += 65536 }
        $pcm[$o] = [byte]($s -band 0xFF); $pcm[$o + 1] = [byte](($s -shr 8) -band 0xFF)
    }
    $ms.Write($pcm, 44, $n * 2)
    if ($n -lt $want) {                       # pad a short chunk with silence
        $pad = New-Object byte[] (($want - $n) * 2)
        $ms.Write($pad, 0, $pad.Length)
    }
    $wrote += $want
    Write-Host ("  chunk {0}/{1}: frames {2}..{3} -> {4} samples" -f $ci, $nChunks, $from, ($from + $nf - 1), $n)
}
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

$data = $ms.ToArray()
$h = New-Object byte[] 44
[Text.Encoding]::ASCII.GetBytes("RIFF").CopyTo($h, 0)
[BitConverter]::GetBytes([int](36 + $data.Length)).CopyTo($h, 4)
[Text.Encoding]::ASCII.GetBytes("WAVEfmt ").CopyTo($h, 8)
[BitConverter]::GetBytes([int]16).CopyTo($h, 16)
[BitConverter]::GetBytes([int16]1).CopyTo($h, 20)
[BitConverter]::GetBytes([int16]1).CopyTo($h, 22)
[BitConverter]::GetBytes([int]$RATE).CopyTo($h, 24)
[BitConverter]::GetBytes([int]$RATE).CopyTo($h, 28)
[BitConverter]::GetBytes([int16]2).CopyTo($h, 32)
[BitConverter]::GetBytes([int16]16).CopyTo($h, 34)
[Text.Encoding]::ASCII.GetBytes("data").CopyTo($h, 36)
[BitConverter]::GetBytes([int]$data.Length).CopyTo($h, 40)

$fs = [IO.File]::Create($Out)
$fs.Write($h, 0, 44)
$fs.Write($data, 0, $data.Length)
$fs.Close()

$sum = 0.0; $peak = 0
for ($i = 0; $i -lt $data.Length / 2; $i++) {
    $v = ([int]$data[2 * $i]) -bor (([int]$data[2 * $i + 1]) -shl 8)
    if ($v -ge 32768) { $v -= 65536 }
    $sum += [double]$v * $v
    if ([Math]::Abs($v) -gt $peak) { $peak = [Math]::Abs($v) }
}
$n2 = [int]($data.Length / 2)
Write-Host ("wrote {0}`n  {1} samples, {2:N1} s @ {3} Hz, RMS {4:N1}, peak {5}" -f `
    $Out, $n2, ($n2 / $RATE), $RATE, [Math]::Sqrt($sum / $n2), $peak)
