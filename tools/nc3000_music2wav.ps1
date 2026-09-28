# nc3000_music2wav.ps1 - play one of the NC3000's original /midi/音乐N.mid
# scores through the *firmware's* SACM_MS01 engine (not the SDK example) and
# write a listenable WAV.
#
#   .\tools\nc3000_music2wav.ps1 1 out\音乐1.wav            # whole song
#   .\tools\nc3000_music2wav.ps1 2 -Seconds 8               # first 8 seconds
#   .\tools\nc3000_music2wav.ps1 1 -Firmware 061            # use 061.dat
#
# How it works (see docs\NC3000_MS01音乐复现_20260926.md):
#   0xBB 0x0A link -> 0x44 volume -> 0x55 speed -> 0x99 0x00 (MS01)
#   -> 0x33 + 15-byte score blocks, tail as 0x22 + len + payload.
#   That is exactly the sequence NOR bank 0 uses ($FDE0/$FAEE/$FB01/$FE2A +
#   the $FC5F pump).  The DSP's 0x99 handler calls its "start decode" only
#   after the stream is established, which the first data packet does - no
#   extra command is needed (measured: dropping the duplicate 0x99 gives a
#   byte-identical WAV).
#   Audio = DAC2 (0x7016) minus 0x8000, at the firmware's own 63833 Hz.
#   Scores shorter than ~560 bytes must be padded: the DSP only sets its
#   "stream established" flag once the pair counter [0x0729] (init 0x118 =
#   560 bytes) counts down to zero, and the real host sends a whole RAM buffer.
#   The probe does that automatically (pads to 640 bytes with 0x00).
#
# The score is ~28.2 s long; the engine stops writing samples at the end
# (no loop), and the probe stops on its own after 60M instructions of silence.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][ValidateRange(1, 20)][int]$Song,
    [Parameter(Position = 1)][string]$Out = "",
    [double]$Seconds = 0,          # 0 = whole song (renders until the engine stops)
    [ValidateSet("nand", "061")][string]$Firmware = "nand",
    [int]$Volume = 8,
    [ValidateRange(0, 4)][int]$Speed = 2
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$probe = Join-Path $root "spce061a\build\ms01play.exe"
$score = Join-Path $root ("out\midi_orig\NC3000原机的midi\音乐{0}.mid" -f $Song)
if (-not (Test-Path $score)) {
    throw "score not found: $score`n(extract info\NC3000原机的midi.rar into out\midi_orig\ first)"
}
if (-not $probe) { throw "probe path not set" }
if (-not (Test-Path $probe)) {
    throw "player not found: $probe`nbuild it with:`n  tcc -O2 -I emu -I host -o build\ms01play.exe emu\main_linux_ms01.c host\nc3000_dsp.c emu\spce061a.c emu\unsp.c emu\firmware_061.c host\firmware_061_nand.c"
}
if (-not $Out) {
    $Out = Join-Path $root ("out\NC3000_音乐{0}.wav" -f $Song)
}
if (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $root $Out }
$Out = [IO.Path]::GetFullPath($Out)
if ($Out -eq $score) { throw "refusing to overwrite the score" }

# ms01play.exe works out the instruction budget itself: -t <sec> for a partial
# render, otherwise a 50 s budget.  Either way the render also ends by itself
# once the firmware stops writing DAC2 samples (end of score).

$fwArg = if ($Firmware -eq "nand") { @("-fw", "nand") } else { @() }
$tArg = if ($Seconds -gt 0) { @("-t", "$Seconds") } else { @() }
Write-Host ("rendering 音乐{0} with {1} firmware" -f $Song, $Firmware)
& $probe $score $Out -v $Volume -s $Speed @fwArg @tArg |
    Where-Object { $_ -match 'fed |pcm:|wrote |\[end\]|padded' } |
    ForEach-Object { Write-Host $_ }

if (Test-Path $Out) {
    $len = (Get-Item $Out).Length
    Write-Host ("`nwrote {0} ({1:N0} bytes)" -f $Out, $len)
} else {
    throw "the probe did not write $Out"
}
