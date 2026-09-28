# Invert the colours of a 24 bit BMP produced by the headless LCD dumper.
# usage: powershell -File tools/bmp_invert.ps1 <in.bmp> <out.bmp>
param([string]$In, [string]$Out)
$b = [System.IO.File]::ReadAllBytes($In)
for ($i = 54; $i -lt $b.Length; $i++) { $b[$i] = 255 - $b[$i] }
[System.IO.File]::WriteAllBytes($Out, $b)
Write-Host "wrote $Out"
