<#
    split_asm.ps1 -- split one macro-heavy .asm into several assembly units.

    Why: every xasm16 we have (1.14.11 in unSP IDE 2.3.2 and 1.14.13 in 3.0.4)
    keeps a fixed per-run pool of "macro expansion temp files" (~640 lines of
    expanded text).  Once a file expands more than that the assembler reports
    "No more tmp file can be used in macro processing!" and the expansion
    breaks.  18 files of this firmware exceed it (up to 986 macro calls in
    Component\Editor\RichEdit.asm).

    This script cuts such a file into N stand-alone .asm units (each gets its
    own assembler run, hence its own pool) at boundaries where execution cannot
    fall through, copies the file's header/.MACRO/.VDEF definitions into every
    unit and then repairs the cross-unit symbols (.PUBLIC/.EXTERNAL, renaming
    ?-locals) until every unit assembles.  The original file in info\ is never
    touched; the units live under out\nc3001_build\split\<Base>\.

    Usage:
      .\split_asm.ps1 -Source src\Component\Editor\RichEdit.asm [-ToolchainRoot ...]
    Writes <OutRoot>\<Base>\<Base>_pNN.asm plus <OutRoot>\<Base>\manifest.json
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Source,
    [string]$ToolchainRoot,
    [string]$OutRoot,
    [string]$NcTree,
    [int]$MaxRounds = 80,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($NcTree) {
    $NcDir = (Resolve-Path -LiteralPath $NcTree).Path
} else {
    $movedTree = Join-Path (Split-Path $RepoRoot -Parent) 'NC3001'
    $oldTree   = Join-Path $RepoRoot 'info\NC3001'
    if (Test-Path -LiteralPath $movedTree) { $NcDir = (Resolve-Path -LiteralPath $movedTree).Path }
    else { $NcDir = $oldTree }
}
$SrcDir   = Join-Path $NcDir 'src'
if (-not $OutRoot) { $OutRoot = Join-Path $RepoRoot 'out\nc3001_build\split' }

$gbk   = [System.Text.Encoding]::GetEncoding(936)
$latin = [System.Text.Encoding]::GetEncoding(28591)

# ------------------------------------------------------------------ tool chain
function Get-ShortPath { param([string]$Path)
    try { (New-Object -ComObject Scripting.FileSystemObject).GetFolder($Path).ShortPath } catch { $Path } }

$candidates = @()
if ($ToolchainRoot) { $candidates += $ToolchainRoot }
foreach ($p in @('C:\Program Files (x86)\Sunplus', 'C:\Program Files\Sunplus', 'C:\Generalplus')) {
    if (Test-Path $p) { $candidates += (Get-ChildItem $p -Directory -EA SilentlyContinue | ForEach-Object FullName) }
}
$TcRoot = $null
foreach ($c in $candidates) {
    foreach ($probe in @($c, (Join-Path $c 'ToolChain'))) {
        if ((Test-Path (Join-Path $probe 'xasm16.exe')) -and (Test-Path (Join-Path $probe 'gcc.exe'))) {
            $TcRoot = if ((Split-Path $probe -Leaf) -eq 'ToolChain') { Split-Path $probe -Parent } else { $probe }
            break
        }
    }
    if ($TcRoot) { break }
}
if (-not $TcRoot) { throw 'u''nSP tool chain not found (pass -ToolchainRoot)' }
$TcBin  = if (Test-Path (Join-Path $TcRoot 'ToolChain')) { Join-Path $TcRoot 'ToolChain' } else { $TcRoot }
$TcShort = Get-ShortPath $TcBin
$AsmExe = Join-Path $TcBin 'xasm16.exe'

$ASFLAGS  = '-t4 -d -be'
$INCLUDES = '-I"' + $SrcDir + '" -I"./System/include" -I"./Driver/include" ' +
            '-I"./Component/Include" -I"./System/include/clib"'

# ------------------------------------------------------------------ helpers
function Invoke-Asm {
    param([string]$ChunkPath, [string]$ObjPath, [string]$LstPath)
    $cmd = '"' + $AsmExe + '" ' + $ASFLAGS + ' ' + $INCLUDES +
           ' -l "' + $LstPath + '" -o "' + $ObjPath + '" "' + $ChunkPath + '" 2>&1'
    $prevIn = [Wqx.LegacyCP]::GetConsoleCP(); $prevOut = [Wqx.LegacyCP]::GetConsoleOutputCP()
    [void][Wqx.LegacyCP]::SetConsoleCP(936); [void][Wqx.LegacyCP]::SetConsoleOutputCP(936)
    try {
        # run from the chunk's own directory: every .INCLUDE was rewritten to an
        # absolute path, and xasm16 writes its DUPTMP.txt dump into the CWD when a
        # file fails - we do not want that inside info\
        Push-Location $work
        try { $out = cmd /c $cmd } finally { Pop-Location }
    } finally {
        [void][Wqx.LegacyCP]::SetConsoleCP($prevIn); [void][Wqx.LegacyCP]::SetConsoleOutputCP($prevOut)
    }
    $errs = @(); $tmpLine = 0
    foreach ($line in $out) {
        if ($line -match 'No more tmp file can be used in macro processing') {
            $m = [regex]::Match($line, '\((\d+)\)')
            if ($m.Success) { $tmpLine = [int]$m.Groups[1].Value }
        }
        if ($line -match 'error A\d+') { $errs += $line.Trim() }
    }
    [pscustomobject]@{ Output = $out; Errors = $errs; TmpLine = $tmpLine }
}

if (-not ('Wqx.LegacyCP' -as [type])) {
    Add-Type -Namespace Wqx -Name LegacyCP -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetConsoleCP(uint cp);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetConsoleOutputCP(uint cp);
[DllImport("kernel32.dll")] public static extern uint GetConsoleCP();
[DllImport("kernel32.dll")] public static extern uint GetConsoleOutputCP();
'@
}

function Write-Chunk {
    param([string]$Path, [string[]]$Lines)
    [System.IO.File]::WriteAllBytes($Path, $gbk.GetBytes(($Lines -join "`n") + "`n"))
}

# section in effect at every body line: these files rely on the assembler's
# default section (.CODE) and switch to .ORAM/.text/... with plain directives or
# "name: .SECTION .XXX"; a chunk that starts in the middle must re-enter the
# same section, otherwise its code/data lands somewhere else.
function Get-SectionMap {
    $map = New-Object string[] $body.Count
    $cur = '.CODE'
    for ($i = 0; $i -lt $body.Count; $i++) {
        $t = $body[$i]
        if ($t -match '^\s*\.(code|text|nb_data|data|oram|iram|ram|sram|osram|isram)\s*$') {
            $cur = '.' + $Matches[1].ToUpper()
        }
        elseif ($t -match '^\S+\s*:\s*\.SECTION\s+\.(\w+)') {
            $cur = '.' + $Matches[1].ToUpper()
        }
        $map[$i] = $cur
    }
    return ,$map
}

# ------------------------------------------------------------------ read source
$srcPath = Join-Path $NcDir $Source
if (-not (Test-Path -LiteralPath $srcPath)) { throw "source not found: $srcPath" }
$Base    = [System.IO.Path]::GetFileNameWithoutExtension($srcPath)
$work    = Join-Path $OutRoot $Base
New-Item -ItemType Directory -Force -Path $work | Out-Null

$all = [System.IO.File]::ReadAllText($srcPath, $gbk) -split "`n"
if ($all[-1] -eq '') { $all = $all[0..($all.Count-2)] }        # drop trailing empty
$srcFileDir = Split-Path $srcPath -Parent

# .INCLUDE paths are resolved relative to the *including file's* directory by the
# assembler (e.g. Editor\RichEdit.asm uses '.\include\Editor_Self.inc').  Our
# chunks live under out\, so rewrite every include to an absolute path.
function Fix-Include {
    param([string]$Line)
    if ($Line -notmatch '^(\s*\.include\s+)(\S+)(.*)$') { return $Line }
    $rel = $Matches[2]
    $abs = Join-Path $srcFileDir $rel
    if (-not (Test-Path -LiteralPath $abs)) { $abs = Join-Path $SrcDir $rel }
    if (-not (Test-Path -LiteralPath $abs)) { return $Line }
    return $Matches[1] + [System.IO.Path]::GetFullPath($abs) + $Matches[3]
}

# body starts at the first .CODE / .TEXT section directive
$codeStart = -1
for ($i = 0; $i -lt $all.Count; $i++) {
    if ($all[$i] -match '^\s*\.(code|text)\s*$') { $codeStart = $i; break }
}
if ($codeStart -lt 0) { $codeStart = 0 }

$header = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -le $codeStart; $i++) { $header.Add($all[$i]) }
$body = New-Object System.Collections.Generic.List[string]
for ($i = $codeStart + 1; $i -lt $all.Count; $i++) { $body.Add($all[$i]) }

# lift .MACRO blocks, compile-time constants (name: .VDEF/.EQU) and the
# .EXTERNAL/.PUBLIC declarations into the header (they are position independent)
$lifted = New-Object System.Collections.Generic.List[string]
$publicList = New-Object System.Collections.Generic.List[string]
$keep   = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $body.Count; $i++) {
    $t = $body[$i]
    if ($t -match '^\S+\s*:\s*\.macro\b') {
        $j = $i
        while ($j -lt $body.Count -and $body[$j] -notmatch '^\s*\.endm\b') { $j++ }
        for ($k = $i; $k -le [Math]::Min($j, $body.Count - 1); $k++) { $lifted.Add($body[$k]) }
        $i = $j
        continue
    }
    if ($t -match '^\S+\s*:\s*\.(vdef|equ)\b') { $lifted.Add($t); continue }
    if ($t -match '^\s*\.include\b') {
        # every unit needs the macro/constant definitions of the headers, so the
        # .INCLUDE lines are lifted as well (removed from the body)
        if ($lifted -notcontains $t) { $lifted.Add($t) }
        continue
    }
    if ($t -match '^\s*\.(external|public)\b') {
        # .EXTERNAL is replicated everywhere; .PUBLIC is re-emitted per chunk for
        # the symbols that chunk actually defines
        foreach ($name in ($t -replace '^\s*\.\w+\s*','' -split '[\s,]+')) {
            if ($name) {
                if ($t -match '^\s*\.public\b') { if ($publicList -notcontains $name) { $publicList.Add($name) } }
                else { $lifted.Add('.EXTERNAL' + "`t" + $name) }
            }
        }
        continue
    }
    $keep.Add($t)
}
$body = $keep
if ($lifted.Count) { $header.Add(''); $lifted | ForEach-Object { $header.Add($_) } }

if (-not $Quiet) {
    Write-Host ("{0}: {1} header lines, {2} body lines, {3} lifted defs" -f $Base, $header.Count, $body.Count, $lifted.Count)
}

# ------------------------------------------------------- candidate cut points
# a new chunk may start at a label that is only reachable by a jump/call (the
# previous effective line must be an unconditional transfer), or right after .ENDP
$bounds = New-Object System.Collections.Generic.List[int]
$prevEff = ''
for ($i = 0; $i -lt $body.Count; $i++) {
    $t = $body[$i]
    if ($t -match '(?i)^\s*\.endp\b') { $bounds.Add($i + 1) }
    elseif ($t -match '^\S+:' -and $t -notmatch '^\s' -and $t -notmatch '^//' -and
            $t -notmatch '(?i)^\S+\s*:\s*\.' -and
            ($prevEff -match '(?i)^\s*(retf|ret|goto|jmp)\b' -or $prevEff -match '(?i)^\s*PC\s*=')) {
        $bounds.Add($i)
    }
    if ($t -match '\S' -and $t -notmatch '^\s*//') { $prevEff = $t }
}
$bounds = @($bounds | Where-Object { $_ -gt 0 -and $_ -lt $body.Count } | Sort-Object -Unique)

$chunks = New-Object System.Collections.Generic.List[object]
$chunks.Add([pscustomobject]@{ Start = 0; End = $body.Count - 1; Public = @(); External = @() })

function Write-AllChunks {
    param([switch]$Asm)
    $i = 0
    $res = @()
    $sect = Get-SectionMap
    foreach ($c in $chunks) {
        $i++
        $lines = New-Object System.Collections.Generic.List[string]
        $header | ForEach-Object { $lines.Add($_) }
        # .PUBLIC only for symbols this chunk really defines (a chunk that does not
        # define it must import it instead)
        foreach ($p in $publicList) {
            $pat = '^\s*' + [regex]::Escape($p) + '\s*:'
            for ($k = $c.Start; $k -le $c.End; $k++) {
                if ($body[$k] -match $pat) { $lines.Add('.PUBLIC' + "`t" + $p); break }
            }
        }
        foreach ($x in $c.External) { $lines.Add('.EXTERNAL' + "`t" + $x) }
        foreach ($x in $c.Public)   { $lines.Add('.PUBLIC'   + "`t" + $x) }
        $lines.Add($sect[$c.Start])                      # re-enter the section this chunk starts in
        for ($k = $c.Start; $k -le $c.End; $k++) { $lines.Add($body[$k]) }
        for ($k = 0; $k -lt $lines.Count; $k++) { $lines[$k] = Fix-Include $lines[$k] }
        $file = Join-Path $work ('{0}_p{1:d2}.asm' -f $Base, $i)
        Write-Chunk -Path $file -Lines $lines
        $obj  = Join-Path $work ('{0}_p{1:d2}.obj' -f $Base, $i)
        $lst  = Join-Path $work ('{0}_p{1:d2}.lst' -f $Base, $i)
        $r = $null
        if ($Asm) { $r = Invoke-Asm -ChunkPath $file -ObjPath $obj -LstPath $lst }
        $res += [pscustomobject]@{ N = $i; File = $file; Obj = $obj; Lst = $lst
                                   Start = $c.Start; End = $c.End
                                   HeaderLines = $lines.Count - ($c.End - $c.Start + 1)
                                   Result = $r }
    }
    return $res
}

# ------------------------------------------------------------- split until OK
$final = $null
for ($round = 1; $round -le $MaxRounds; $round++) {
    $res = Write-AllChunks -Asm
    $bad = @($res | Where-Object { $_.Result.TmpLine -gt 0 })
    if (-not $bad) { $final = $res; break }

    $splitHappened = $false
    foreach ($c in ($bad | Sort-Object N -Descending)) {
        $idx = $chunks[$c.N - 1]
        # chunk file line -> index inside the shared $body array
        $bodyIdx = $idx.Start + ($c.Result.TmpLine - 1 - $c.HeaderLines)
        if ($bodyIdx -ge $idx.End) { $bodyIdx = $idx.End - 1 }
        $cut = ($bounds | Where-Object { $_ -gt $idx.Start -and $_ -le $bodyIdx } | Select-Object -Last 1)
        if (-not $cut) {
            # no safe boundary: fall back to the last label or the last blank line before the failure
            $cut = ($idx.Start + 1..([Math]::Max($idx.Start + 1, $bodyIdx + 1)) |
                    Where-Object { $body[$_] -match '^\S.*:' -or $body[$_] -eq '' } |
                    Select-Object -Last 1)
        }
        if (-not $cut -or $cut -le $idx.Start -or $cut -gt $idx.End) { continue }

        $chunks.RemoveAt($c.N - 1)
        $chunks.Insert($c.N - 1, [pscustomobject]@{ Start = $cut; End = $idx.End; Public = @($idx.Public); External = @($idx.External) })
        $chunks.Insert($c.N - 1, [pscustomobject]@{ Start = $idx.Start; End = $cut - 1; Public = @($idx.Public); External = @($idx.External) })
        $splitHappened = $true
    }
    if (-not $splitHappened) { $final = $res; break }
    if (-not $Quiet) { Write-Host ("  round {0}: {1} chunks" -f $round, $chunks.Count) }
}
if (-not $final) { $final = Write-AllChunks -Asm }
if (-not $Quiet) { Write-Host ("chunks after splitting: {0}" -f $final.Count) }

# --------------------------------------------------- repair cross-chunk symbols
for ($round = 1; $round -le 12; $round++) {
    $res  = Write-AllChunks -Asm
    $need = @()
    foreach ($c in $res) {
        foreach ($e in $c.Result.Errors) {
            $m = [regex]::Match($e, "error A0007: '([^']+)' undefined")
            if ($m.Success) { $need += [pscustomobject]@{ N = $c.N; Sym = $m.Groups[1].Value } }
        }
    }
    if (-not $need) { $final = $res; break }

    # ?-prefixed symbols cannot be exported -> give them a global name (body+header
    # so macro definitions stay in sync)
    $ren = @{}
    foreach ($n in ($need | Where-Object { $_.Sym.StartsWith('?') })) {
        if ($ren.ContainsKey($n.Sym)) { continue }
        $ren[$n.Sym] = 'ZQ' + ($n.Sym -replace '^\?','') + '_u' + ($ren.Count + 1)
    }
    foreach ($k in $ren.Keys) {
        $pat = [regex]::Escape($k) + '(?![\w])'
        for ($i = 0; $i -lt $body.Count;   $i++) { $body[$i]   = [regex]::Replace($body[$i],   $pat, $ren[$k]) }
        for ($i = 0; $i -lt $header.Count; $i++) { $header[$i] = [regex]::Replace($header[$i], $pat, $ren[$k]) }
    }
    if ($ren.Count -and -not $Quiet) { Write-Host ("  round {0}: renamed {1} local symbol(s)" -f $round, $ren.Count) }

    foreach ($n in $need) {
        if ($ren.ContainsKey($n.Sym)) { continue }
        $sym = $n.Sym
        $defIdx = -1
        for ($ci = 0; $ci -lt $chunks.Count -and $defIdx -lt 0; $ci++) {
            for ($k = $chunks[$ci].Start; $k -le $chunks[$ci].End; $k++) {
                if ($body[$k] -match ('^\s*' + [regex]::Escape($sym) + '\s*:')) { $defIdx = $ci; break }
            }
        }
        $u = $chunks[$n.N - 1]
        if ($u.External -notcontains $sym) { $u.External = @($u.External) + $sym }
        if ($defIdx -ge 0) {
            $d = $chunks[$defIdx]
            if ($d.Public -notcontains $sym) { $d.Public = @($d.Public) + $sym }
        }
    }
    if ($round -eq 12) { $final = Write-AllChunks -Asm }
}

# a symbol fix-up can lengthen a header and push a chunk over the temp-file pool
for ($round = 1; $round -le 6; $round++) {
    $bad = @($final | Where-Object { $_.Result.TmpLine -gt 0 })
    if (-not $bad) { break }
    foreach ($c in ($bad | Sort-Object N -Descending)) {
        $idx = $chunks[$c.N - 1]
        $bodyIdx = $idx.Start + ($c.Result.TmpLine - 1 - $c.HeaderLines)
        if ($bodyIdx -ge $idx.End) { $bodyIdx = $idx.End - 1 }
        $cut = ($bounds | Where-Object { $_ -gt $idx.Start -and $_ -le $bodyIdx } | Select-Object -Last 1)
        if (-not $cut) { continue }
        $chunks.RemoveAt($c.N - 1)
        $chunks.Insert($c.N - 1, [pscustomobject]@{ Start = $cut; End = $idx.End; Public = @($idx.Public); External = @($idx.External) })
        $chunks.Insert($c.N - 1, [pscustomobject]@{ Start = $idx.Start; End = $cut - 1; Public = @($idx.Public); External = @($idx.External) })
    }
    $final = Write-AllChunks -Asm
}

# ---------------------------------------------- cross-unit jumps -> tail calls
# xasm16 refuses "goto/jmp <label defined in another object>" (error A0145
# "Can't jump to external label").  Rewriting the jump as "call L" + "retf" is
# the classic tail-call idiom and behaves identically: L runs, its final retf
# returns to the inserted retf, which returns to our caller.
for ($round = 1; $round -le 10; $round++) {
    $res = Write-AllChunks -Asm
    $jumps = @()
    foreach ($c in $res) {
        foreach ($e in $c.Result.Errors) {
            if ($e -match 'A0145') {
                $m = [regex]::Match($e, '\((\d+)\)')
                if ($m.Success) { $jumps += [pscustomobject]@{ N = $c.N; Line = [int]$m.Groups[1].Value; Hdr = $c.HeaderLines } }
            }
        }
    }
    if (-not $jumps) { $final = $res; break }

    # collect first (so the body line numbers stay valid), then patch from the
    # bottom of the file upwards
    $todo = @()
    $tramp = 0
    foreach ($j in $jumps) {
        $bi = $chunks[$j.N - 1].Start + ($j.Line - 1 - $j.Hdr)
        if ($bi -lt 0 -or $bi -ge $body.Count) {
            if (-not $Quiet) { Write-Host ("    ! p{0:d2} line {1}: body index {2} out of range" -f $j.N,$j.Line,$bi) }
            continue
        }
        if ($body[$bi] -match '^(\s*)(goto|jmp)(\s+)([^\s,]+)(.*)$') {
            $todo += [pscustomobject]@{ Bi = $bi; Kind = 'uncond'; Indent = $Matches[1]
                                        Sep = $Matches[3]; Sym = $Matches[4]; Rest = $Matches[5]
                                        Mnemonic = $Matches[2] }
        }
        elseif ($body[$bi] -match '^(\s*)(jz|jnz|je|jne|ja|jae|jb|jbe|jg|jge|jl|jle|js|jns|jc|jnc)(\s+)([^\s,]+)(.*)$') {
            $tramp++
            $todo += [pscustomobject]@{ Bi = $bi; Kind = 'cond'; Indent = $Matches[1]
                                        Sep = $Matches[3]; Sym = $Matches[4]; Rest = $Matches[5]
                                        Mnemonic = $Matches[2]; Label = ('ZQT_{0}' -f $tramp) }
        }
        elseif (-not $Quiet) {
            Write-Host ("    ! p{0:d2} line {1} (hdr {2}) -> body {3}: not a plain jump: {4}" -f $j.N,$j.Line,$j.Hdr,$bi,$body[$bi].Trim())
        }
    }
    foreach ($t in ($todo | Sort-Object Bi -Descending)) {
        $bi = $t.Bi
        $ownerIdx = -1
        for ($ci = 0; $ci -lt $chunks.Count; $ci++) {
            if ($bi -ge $chunks[$ci].Start -and $bi -le $chunks[$ci].End) { $ownerIdx = $ci; break }
        }
        if ($ownerIdx -lt 0) { continue }

        if ($t.Kind -eq 'uncond') {
            $body[$bi] = $t.Indent + 'call' + $t.Sep + $t.Sym + $t.Rest
            $body.Insert($bi + 1, "`tretf")
            $added = 1; $keepInOwner = 1
        } else {
            # jCC L  ->  jCC ZQT_n / call L / retf / ZQT_n:
            $body[$bi] = $t.Indent + $t.Mnemonic + $t.Sep + $t.Label + $t.Rest
            $body.Insert($bi + 1, $t.Indent + 'call' + $t.Sep + $t.Sym)
            $body.Insert($bi + 2, "`tretf")
            $body.Insert($bi + 3, $t.Label + ':')
            $added = 3
        }
        if ($chunks[$ownerIdx].End -ge $bi) { $chunks[$ownerIdx].End = $chunks[$ownerIdx].End + $added }
        for ($ci = 0; $ci -lt $chunks.Count; $ci++) {
            if ($ci -eq $ownerIdx) { continue }
            if ($chunks[$ci].Start -gt $bi) { $chunks[$ci].Start = $chunks[$ci].Start + $added }
            if ($chunks[$ci].End   -gt $bi) { $chunks[$ci].End   = $chunks[$ci].End   + $added }
        }
    }
    if ($round -eq 10) { $final = Write-AllChunks -Asm }
}

# ------------------------------------------------------------------- manifest
$manifest = [pscustomobject]@{
    Source = $Source
    Base   = $Base
    Chunks = @($final | ForEach-Object { [pscustomobject]@{ N = $_.N; File = $_.File; Obj = $_.Obj } })
}
[System.IO.File]::WriteAllText((Join-Path $work 'manifest.json'),
    ($manifest | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding($false)))

$left = @($final | Where-Object { $_.Result.Errors.Count -gt 0 })
if (-not $Quiet) {
    Write-Host ("{0}: {1} chunks, {2} with errors" -f $Base, $final.Count, $left.Count)
    foreach ($c in $left) {
        Write-Host ("  p{0:d2} ({1} lines): {2}" -f $c.N, ($c.End - $c.Start + 1), (($c.Result.Errors | Select-Object -First 3) -join ' | '))
    }
}
[pscustomobject]@{ Base = $Base; Chunks = $final.Count; ChunkDir = $work; Manifest = (Join-Path $work 'manifest.json')
                   Failed = $left.Count }
