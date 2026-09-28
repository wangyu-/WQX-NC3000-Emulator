<#  Removes build intermediates that ended up in info\NC3001\src\Debug when the
    IDE-regenerated Makefile (OUTDIR=.\Debug) was used.  CVS\ and anything older
    than -Since are kept.  Default: everything written on 2026-09-25 after 12:57. #>
[CmdletBinding()]
param(
    [datetime]$Since = (Get-Date '2026-09-25 12:57:00'),
    [switch]$WhatIf
)

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$dir = Join-Path $RepoRoot 'info\NC3001\src\Debug'
if (-not (Test-Path -LiteralPath $dir)) { Write-Host "no $dir"; exit 0 }

$dir = (Resolve-Path -LiteralPath $dir).Path
$victims = Get-ChildItem -LiteralPath $dir -File -Recurse |
           Where-Object { $_.LastWriteTime -ge $Since -and $_.FullName -notmatch '\\CVS\\' }

Write-Host ("{0} files to remove from {1}" -f $victims.Count, $dir)
$total = ($victims | Measure-Object Length -Sum).Sum
Write-Host ("total {0:N1} MB" -f ($total / 1MB))
if ($WhatIf) { $victims | Select-Object -First 10 FullName,LastWriteTime | Format-Table -AutoSize; exit 0 }

foreach ($f in $victims) { Remove-Item -LiteralPath $f.FullName -Force }

# drop now-empty subdirectories (never Debug\CVS itself)
Get-ChildItem -LiteralPath $dir -Directory -Recurse |
    Sort-Object { $_.FullName.Length } -Descending |
    Where-Object { $_.Name -ne 'CVS' } |
    ForEach-Object {
        if (-not (Get-ChildItem -LiteralPath $_.FullName -Force)) { Remove-Item -LiteralPath $_.FullName -Force }
    }
Write-Host ("remaining: " + (Get-ChildItem -LiteralPath $dir -Recurse -Force | ForEach-Object Name) -join ', ')

# xasm16 drops a debug dump named DUPTMP.txt into its working directory when it
# runs out of macro temp files (i.e. whenever one of the 18 heavy files fails)
foreach ($stray in @('DUPTMP.txt')) {
    $p = Join-Path (Split-Path $dir -Parent) $stray
    if (Test-Path -LiteralPath $p) { Write-Host "removing $p"; Remove-Item -LiteralPath $p -Force }
}
