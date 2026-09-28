<#  Removes the scratch directories produced while investigating the NC3001
    build (CRLF staging copy, PDF copies, assembler experiments).
    Pass -All to also delete the object tree / generated Makefile / .lik.  #>
[CmdletBinding()]
param(
    [switch]$All
)

$RepoRoot  = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildRoot = Join-Path $RepoRoot 'out\nc3001_build'

$scratch = @('tree', 'pdf', 'scratch1', 'enc_test', 'probe232',
             'dircount0', 'dircount100', 'dircount500',
             'split', 'tc232', 'tc304')
if ($All) { $scratch += @('DEV', 'CP119', 'stage', 'log') }

foreach ($name in $scratch) {
    $p = Join-Path $BuildRoot $name
    if (-not (Test-Path -LiteralPath $p)) { continue }
    $resolved = (Resolve-Path -LiteralPath $p).Path
    if (-not $resolved.StartsWith($BuildRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "refusing to delete outside $BuildRoot : $resolved"
    }
    Write-Host "removing $resolved"
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

if ($All) {
    foreach ($f in @('Makefile.DEV', 'Makefile.CP119', 'DEV.lik', 'CP119.lik')) {
        $p = Join-Path $BuildRoot $f
        if (Test-Path -LiteralPath $p) { Write-Host "removing $p"; Remove-Item -LiteralPath $p -Force }
    }
}
Write-Host 'done'
