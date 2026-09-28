<#  Removes leftovers my probes dropped into the (moved) NC3001 tree:
    xasm16 writes DUPTMP.txt into its working directory whenever a file fails
    with "No more tmp file can be used in macro processing".  #>
[CmdletBinding()]
param([string]$Tree = 'C:\Users\YYGSM\Documents\work\NC3001')

$victims = Get-ChildItem -LiteralPath $Tree -Recurse -File -Force -ErrorAction SilentlyContinue |
           Where-Object { ($_.Name -eq 'DUPTMP.txt' -or $_.Name -like '_probe*.obj') -and $_.FullName -notmatch '\\CVS\\' }
foreach ($v in $victims) {
    Write-Host ("removing {0} ({1:N0} bytes, {2})" -f $v.FullName, $v.Length, $v.LastWriteTime)
    Remove-Item -LiteralPath $v.FullName -Force
}
if (-not $victims) { Write-Host 'nothing to clean' }
