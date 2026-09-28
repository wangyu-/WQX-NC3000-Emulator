<#
    build_nc3001.ps1 -- rebuild the NC3001 board firmware (SPL162001) with the
    official GeneralPlus u'nSP tool chain.

    The IDE project directory information\NC3001\src contains an IDE-generated
    Makefile (13:57:58 12/08/06) that belongs to the "CP119" project (simulator
    profile: Debug=0, SPL162001NandDrv_Fake.lib, CP119.S37) and references the
    original developer machine layout (D:\work\NC3001\...,
    C:\PROGRA~1\Sunplus\UNSPID~2.2).  This script re-targets that Makefile onto
    the local tree + the installed tool chain and, for the DEV_NC3000+ project,
    adds the 5 objects that project has on top of CP119
    (Driver\LCD\lcd_c.c + 4 "theme" .RAW resources).

    Nothing inside info\ is modified: the patched Makefile / .lik live in
    out\nc3001_build\, the objects land there too.
#>
[CmdletBinding()]
param(
    [ValidateSet('DEV_NC3000+', 'CP119')]
    [string]$Project = 'DEV_NC3000+',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [int]$Jobs = 8,

    [switch]$NoCompile,
    [switch]$NoLink,
    [switch]$Rebuild,
    [switch]$Quiet,

    # Root of the IDE installation (the folder that holds ToolChain\ and Library\).
    [string]$ToolchainRoot,

    # NC3001 tree (contains src\ and EZSrc\).  Default: <repo>\..\NC3001 if it
    # exists (the tree was moved there on 2026-09-25), else <repo>\info\NC3001.
    [string]$NcTree,

    # xasm16's macro "temp files" go through the old CRT tmpfile(), i.e. into the
    # ROOT OF THE CURRENT DRIVE.  C:\ is not writable for a normal user, which is
    # why the 18 macro-heavy files only built when the IDE ran elevated.  Instead
    # of asking for admin we map the tree onto a free drive letter with subst and
    # run everything from there.  -NoSubst disables that.
    [switch]$NoSubst
)

$ErrorActionPreference = 'Stop'

# --------------------------------------------------------------------------- paths
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
$EzDir    = $NcDir                      # "$(SOURCE=..\EZSrc\...)" sibling of src
$BuildRoot = Join-Path $RepoRoot 'out\nc3001_build'
$BuildTag = if ($Project -eq 'DEV_NC3000+') { 'DEV' } else { 'CP119' }
$OutDir   = Join-Path $BuildRoot $BuildTag
$MakeFilePath = Join-Path $BuildRoot ("Makefile." + $BuildTag)
$LikPathBase  = Join-Path $BuildRoot $BuildTag      # xlink16 appends ".lik"
$LogDir   = Join-Path $BuildRoot 'log'

if (-not (Test-Path -LiteralPath $SrcDir)) { throw "source tree not found: $SrcDir" }

# --------------------------------------------------------------------------- toolchain
function Resolve-ToolchainRoot {
    param([string]$Explicit)

    $ok = {
        param($dir)
        (Test-Path (Join-Path $dir 'gcc.exe')) -and
        (Test-Path (Join-Path $dir 'xasm16.exe')) -and
        (Test-Path (Join-Path $dir 'xlink16.exe'))
    }

    if ($Explicit) {
        foreach ($probe in @($Explicit, (Join-Path $Explicit 'ToolChain'))) {
            if (& $ok $probe) {
                if ((Split-Path $probe -Leaf) -eq 'ToolChain') { return (Split-Path $probe -Parent) }
                return $probe
            }
        }
        throw "-ToolchainRoot '$Explicit' does not contain gcc.exe/xasm16.exe/xlink16.exe"
    }

    $candidates = @('C:\Generalplus\unSPIDE 3.0.4')
    if (Test-Path 'C:\Generalplus') {
        $candidates += (Get-ChildItem 'C:\Generalplus' -Directory -ErrorAction SilentlyContinue |
                        ForEach-Object { $_.FullName })
    }
    foreach ($p in @('C:\Program Files\Sunplus', 'C:\Program Files (x86)\Sunplus',
                     'C:\Program Files\Generalplus', 'C:\Program Files (x86)\Generalplus')) {
        if (Test-Path $p) {
            $candidates += (Get-ChildItem $p -Directory -ErrorAction SilentlyContinue |
                            ForEach-Object { $_.FullName })
        }
    }

    $found = @()
    foreach ($c in ($candidates | Select-Object -Unique)) {
        foreach ($probe in @($c, (Join-Path $c 'ToolChain'))) {
            if (& $ok $probe) {
                # normalise "<root>\ToolChain" -> "<root>"
                $root = if ((Split-Path $probe -Leaf) -eq 'ToolChain') { Split-Path $probe -Parent } else { $probe }
                if ($found -notcontains $root) { $found += $root }
            }
        }
    }
    if (-not $found) {
        throw "u'nSP tool chain not found. Pass -ToolchainRoot <dir containing ToolChain\>."
    }

    # Prefer the 2006-era IDE (unSP IDE 2.x): its xasm16 can still assemble the
    # macro-heavy GUI/Editor files, the 3.0.4 one cannot (64-expansion limit,
    # see docs\NC3001固件编译说明.md).
    $legacy = $found | Where-Object { $_ -match 'unSP IDE 2' } | Select-Object -First 1
    if ($legacy) { return $legacy }
    return $found[0]
}

function Get-ShortPath {
    param([string]$Path)
    try {
        $fso = New-Object -ComObject Scripting.FileSystemObject
        return $fso.GetFolder($Path).ShortPath
    } catch {
        return $Path
    }
}

$TcRoot      = Resolve-ToolchainRoot $ToolchainRoot
$TcBin       = if (Test-Path (Join-Path $TcRoot 'ToolChain')) { Join-Path $TcRoot 'ToolChain' } else { $TcRoot }
$TcRootShort = Get-ShortPath $TcRoot         # keeps "-B<APPDIR>\" free of spaces
$TcBinShort  = Get-ShortPath $TcBin
$LibInc      = Join-Path $TcRootShort 'Library\Include'
$LibIncSys   = Join-Path $TcRootShort 'Library\Include\SYS'
$LibCMacro   = Join-Path $TcRootShort 'Library\CMacro'
$MakeExe     = Join-Path $TcRoot 'Make.exe'
if (-not (Test-Path $MakeExe)) { $MakeExe = (Get-Command make.exe -ErrorAction SilentlyContinue).Source }

# --------------------------------------------------------------------------- helpers
function Write-Bytes {
    param([string]$Path, [byte[]]$Bytes)
    [System.IO.File]::WriteAllBytes($Path, $Bytes)
}

function Get-RelativePath {
    param([string]$From, [string]$To)
    $f = [System.IO.Path]::GetFullPath($From)
    $t = [System.IO.Path]::GetFullPath($To)
    if (-not $f.EndsWith('\')) { $f += '\' }
    $fUri = New-Object System.Uri($f)
    $tUri = New-Object System.Uri($t)
    [System.Uri]::UnescapeDataString($fUri.MakeRelativeUri($tUri).ToString()).Replace('/', '\')
}

# The u'nSP tools are ANSI (CP936) programs and the IDE Makefile carries GBK
# bytes in its Chinese resource paths.  PowerShell 7 runs the console in UTF-8
# (CP65001), and a CP65001 console makes cmd.exe / make.exe mangle those bytes
# into "?" -- so force the legacy code page around every tool invocation.
if (-not ('Wqx.ConsoleCP' -as [type])) {
    Add-Type -Namespace Wqx -Name ConsoleCP -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetConsoleCP(uint cp);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetConsoleOutputCP(uint cp);
[DllImport("kernel32.dll")] public static extern uint GetConsoleCP();
[DllImport("kernel32.dll")] public static extern uint GetConsoleOutputCP();
'@
}
function Use-LegacyCodePage {
    param([scriptblock]$Body)
    $prevIn  = [Wqx.ConsoleCP]::GetConsoleCP()
    $prevOut = [Wqx.ConsoleCP]::GetConsoleOutputCP()
    [void][Wqx.ConsoleCP]::SetConsoleCP(936)
    [void][Wqx.ConsoleCP]::SetConsoleOutputCP(936)
    try { & $Body } finally {
        [void][Wqx.ConsoleCP]::SetConsoleCP($prevIn)
        [void][Wqx.ConsoleCP]::SetConsoleOutputCP($prevOut)
    }
}

# --------------------------------------------------------------------------- prepare dirs
if ($Rebuild -and (Test-Path -LiteralPath $OutDir)) {
    $resolved = (Resolve-Path -LiteralPath $OutDir).Path
    if (-not $resolved.StartsWith($BuildRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "refusing to delete outside $BuildRoot : $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
foreach ($d in @($BuildRoot, $OutDir, $LogDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

$OutDirRel = Get-RelativePath $SrcDir $OutDir      # e.g. ..\..\..\out\nc3001_build\DEV

# safety net: never let make write its 1000+ intermediate files into info\
$outAbs = [System.IO.Path]::GetFullPath((Join-Path $SrcDir $OutDirRel))
if ($outAbs.StartsWith($NcDir, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "refusing to build inside info\ (OUTDIR would be $outAbs)"
}

# --------------------------------------------------------------------------- run dir
# elevated -> C:\ root is writable, nothing to do.  otherwise map the tree onto a
# free drive letter so that tmpfile() lands in a writable drive root.
$IsElevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
                [Security.Principal.WindowsBuiltInRole]::Administrator)
$RunDir     = $SrcDir
$SubstDrive = $null
$MakeOutDir = $OutDirRel          # OUTDIR as written into the Makefile (must stay colon-free)
if (-not $NoSubst -and -not $IsElevated) {
    # map the common parent of the tree and this repo onto a free drive letter:
    # tmpfile() then lands in <L>:\ which is writable, and both the source tree
    # and our out\ directory stay reachable with colon-free relative paths.
    $common = Split-Path $NcDir -Parent
    while ($common -and -not ($RepoRoot.StartsWith($common + '\', [StringComparison]::OrdinalIgnoreCase))) {
        $parent = Split-Path $common -Parent
        if (-not $parent -or $parent -eq $common) { break }
        $common = $parent
    }
    foreach ($L in @('Z', 'Y', 'X', 'W', 'V', 'U', 'T', 'S', 'R', 'Q', 'P')) {
        if (Test-Path ($L + ':\')) { continue }
        $out = cmd /c ('subst ' + $L + ': "' + $common + '" 2>&1')
        if ($out -or -not (Test-Path ($L + ':\'))) { continue }
        $relTree = $NcDir.Substring($common.Length).TrimStart('\')
        $candRun = $L + ':\' + $relTree + '\src'
        if (-not (Test-Path $candRun)) { continue }
        $SubstDrive = $L
        $RunDir     = $candRun
        # OUTDIR must be colon-free for make, and a relative path cannot cross
        # drives -> express both ends inside the subst domain
        $relRun     = $L + ':\' + $relTree + '\src'
        $relOut     = $L + ':\' + $OutDir.Substring($common.Length).TrimStart('\')
        $MakeOutDir = Get-RelativePath $relRun $relOut
        break
    }
}
$RunMode = if ($SubstDrive) { 'subst ' + $SubstDrive + ': -> ' + $common + '  (run dir ' + $RunDir + ')' }
           elseif ($IsElevated) { 'elevated (no subst needed)' }
           else { 'plain (C: root writable? else 18 files will fail)' }

# --------------------------------------------------------------------------- generate Makefile
# The IDE files are GBK.  All byte surgery is done in a Latin-1 "byte string"
# (1 byte == 1 char) so that the Chinese paths survive untouched and the
# replacements still use fast String.Replace instead of byte-by-byte scans.
$gbk   = [System.Text.Encoding]::GetEncoding(936)
$latin = [System.Text.Encoding]::GetEncoding(28591)

# Unicode string -> byte string (encoded as GBK, i.e. what the tools expect)
function To-ByteString { param([string]$Text) return $latin.GetString($gbk.GetBytes($Text)) }
# byte string -> Unicode string
function From-ByteString { param([string]$Text) return $gbk.GetString($latin.GetBytes($Text)) }

$mk = $latin.GetString([System.IO.File]::ReadAllBytes((Join-Path $SrcDir 'Makefile')))

# The IDE regenerates src\Makefile every time the project is saved/built, so it
# comes in two flavours: the original CP119 one (DebugSim / CP119.S37 / DEV=0)
# and a DEV_NC3000+ one written by unSP IDE 2.x (.\Debug / DEV_NC3000+.TSK / DEV=1).
$baseIsDev = $mk -match 'DEV_NC3000\+'

$repl = [ordered]@{
    # IDE installation -> local tool chain (forward and backward slash flavours)
    'C:/Program Files/Sunplus/unSP IDE 2.3.2/library/include' = $LibInc
    'C:\Program Files\Sunplus\unSP IDE 2.3.2\library'         = (Join-Path $TcRootShort 'Library')
    'C:/Program Files (x86)/Sunplus/unSP IDE 2.3.2/library/include' = $LibInc
    'C:\Program Files (x86)\Sunplus\unSP IDE 2.3.2\library'         = (Join-Path $TcRootShort 'Library')
    'C:\PROGRA~1\Sunplus\UNSPID~2.2'                          = $TcBinShort
    'C:\PROGRA~2\Sunplus\UNSPID~1.2'                          = $TcBinShort
    # original source tree -> this checkout
    'D:/work/NC3001/'                                        = ($EzDir.Replace('\', '/') + '/')
    'D:\work\NC3001\'                                        = ($EzDir + '\')
}
if (-not $baseIsDev) {
    # CP119-base Makefile: retarget it onto the requested project
    $repl['= .\DebugSim'] = ('= ' + $OutDirRel)
    $repl['CP119.S37']    = ($Project + '.S37')
    $repl['CP119.ary']    = ($Project + '.ary')
    $repl['CP119.sbm']    = ($Project + '.sbm')
    $repl['CP119.bdy']    = ($Project + '.bdy')
    if ($Project -ne 'CP119') { $repl['-D DEV=0'] = '-D DEV=1' }
}

foreach ($k in $repl.Keys) { $mk = $mk.Replace($k, $repl[$k]) }   # ASCII <-> (local paths are ASCII too)

# APPDIR / OUTDIR must always point at *our* tool chain and *our* output dir,
# whatever the IDE wrote into src\Makefile (its own OUTDIR is .\Debug = src\Debug,
# which would drop 1000+ build files into the (read-only by policy) source tree).
$mk = [regex]::Replace($mk, '(?m)^APPDIR[ \t]*=.*$', ('APPDIR' + "`t" + '= ' + $TcBinShort))
# absolute OUTDIR: the working directory may be on the subst drive, where a
# relative "..\..\..\out\..." would resolve somewhere else entirely
$mk = [regex]::Replace($mk, '(?m)^OUTDIR[ \t]*=.*$', ('OUTDIR' + "`t" + '= ' + $MakeOutDir))

# ---- project-specific extra objects (DEV_NC3000+ has 5 more than CP119)
if ($Project -eq 'DEV_NC3000+' -and $mk -notmatch 'lcd_c\.asm') {
    $extra = @()
    $extra += ''
    $extra += '# --- added by build_nc3001.ps1: objects present in DEV_NC3000+ but not in CP119'
    $extra += '"$(OUTDIR)\lcd_c.asm": "' + $SrcDir + '\Driver\LCD\lcd_c.c"'
    $extra += "`t`$(CC) `$(CFLAGS) -o `"`$(OUTDIR)/lcd_c.asm`" `"" + ($SrcDir.Replace('\', '/')) + '/Driver/LCD/lcd_c.c"'
    $extra += ''
    $extra += '"$(OUTDIR)\lcd_c.obj": "$(OUTDIR)\lcd_c.asm"'
    $extra += "`t`$(AS) `$(CASFLAGS) `$(INCLUDES) -l `"`$(OUTDIR)\lcd_c.lst`" -o `"`$(OUTDIR)\lcd_c.obj`" `"`$(OUTDIR)\lcd_c.asm`""
    foreach ($pic in @('dict_campic', 'ecdictpic', 'xinhuapic', 'cedictpic')) {
        $srcFile = $SrcDir + '\Resource\Bmp\主题\' + $pic + '.RAW'
        $id      = 'RES_' + $pic.ToUpper() + '_RAW'
        $extra += ''
        $extra += '"$(OUTDIR)\' + $pic + '_RAW.res": "' + $srcFile + '"'
        $extra += "`t`$(RESC) `"$srcFile`" `"`$(OUTDIR)\$pic`_RAW.res`" $id "
    }
    $extra += ''
    $mk = $mk + (To-ByteString (($extra -join "`r`n") + "`r`n"))

    # ... and they must be linked, so append them to OBJFILES as well.
    # the anchor starts with a TAB so it cannot hit the "$(RM)" line of the clean rule
    $objAnchor = "`t" + '"$(OUTDIR)\communication.obj"'
    $objAdd    = " \`r`n" +
                 "`t`"`$(OUTDIR)\lcd_c.obj`" \`r`n" +
                 "`t`"`$(OUTDIR)\dict_campic_RAW.res`" \`r`n" +
                 "`t`"`$(OUTDIR)\ecdictpic_RAW.res`" \`r`n" +
                 "`t`"`$(OUTDIR)\xinhuapic_RAW.res`" \`r`n" +
                 "`t`"`$(OUTDIR)\cedictpic_RAW.res`""
    $mk = $mk.Replace($objAnchor, $objAnchor + $objAdd)
}

# ---- source patches required by the shipped assemblers ----------------------
# KeyDecode.asm ends its code region with "jmp ?L_KeyScanReturn" placed right in
# front of the key tables.  xasm16 (1.14.11 and 1.14.13 both) rejects a jmp that
# is not followed by an effective instruction - "error A0174: After two
# effective words instructions should to be inserted in the branch instruction"
# - and a nop does not satisfy it.  Emitting the very same jump as the 2-word
# `goto` (PC = label) is accepted, which is also what the rest of the project
# uses.  info\ is never modified: the patched copy is built under
# out\nc3001_build\stage\patched\.
$patchedDir = Join-Path $BuildRoot 'stage\patched'
$sourcePatches = [ordered]@{}
$patchLog = @()
foreach ($rel in $sourcePatches.Keys) {
    $origPath = Join-Path $EzDir $rel
    if (-not (Test-Path -LiteralPath $origPath)) { throw "patch target missing: $origPath" }
    $res = & $sourcePatches[$rel] $gbk.GetString([System.IO.File]::ReadAllBytes($origPath))
    if ($res.Hits -lt 1) {
        # already fixed inside info\ - nothing to do, keep using the original file
        $patchLog += ($rel + ' : already patched in tree (skipped)')
        continue
    }
    $dst = Join-Path $patchedDir $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
    Write-Bytes -Path $dst -Bytes $gbk.GetBytes($res.Text)
    $mk = $mk.Replace($origPath, $dst)
    $patchLog += ($rel + ' -> ' + $dst)
}

# ---- stage every input whose path contains non-ASCII characters under an
#      ASCII name.  make.exe hands recipe lines to cmd.exe through the ANSI
#      code page; a GBK path inside a *recipe* survives only while the console
#      code page happens to be 936 and breaks as soon as several jobs run at
#      once.  All source/object names are ASCII, the affected inputs are the
#      ~110 .RAW/.bin/.lrc resources that live in Chinese-named folders.
$StageDir = Join-Path $BuildRoot 'stage'
$stagedCount = 0
if (-not (Test-Path $StageDir)) { New-Item -ItemType Directory -Path $StageDir -Force | Out-Null }

foreach ($m in [regex]::Matches($mk, '"([^"]+)"')) {
    $p = $m.Groups[1].Value                                  # byte string
    if ($p -notmatch '[^\x00-\x7F]') { continue }            # ASCII path -> leave alone
    $uni = From-ByteString $p
    if (-not (Test-Path -LiteralPath $uni)) { continue }

    $stagedCount++
    $safe = ([System.IO.Path]::GetFileName($uni) -replace '[^\x20-\x7E]', '_')
    $dest = Join-Path $StageDir ("r{0:d4}_{1}" -f $stagedCount, $safe)
    Copy-Item -LiteralPath $uni -Destination $dest -Force
    $mk = $mk.Replace($p, $dest)
}

Write-Bytes -Path $MakeFilePath -Bytes ($latin.GetBytes($mk))

# --------------------------------------------------------------------------- generate .lik
$likSrc = Join-Path $SrcDir ($Project + '.lik')
$lik = $latin.GetString([System.IO.File]::ReadAllBytes($likSrc))
$likRepl = [ordered]@{
    # original developer machine layout
    'D:\work\NC3001\src\Debug\'                       = ($OutDir + '\')
    'D:\work\NC3001\src\DebugSim\'                    = ($OutDir + '\')
    'D:\work\NC3001\'                                 = ($EzDir + '\')
    # the IDE rewrites <project>.lik with local paths whenever the project is
    # saved/built (seen 2026-09-25 15:13), so handle those as well
    ($EzDir + '\src\Debug\')                          = ($OutDir + '\')
    ($EzDir + '\src\DebugSim\')                       = ($OutDir + '\')
    # IDE library path -> the tool chain actually used
    'C:\Program Files (x86)\Sunplus\unSP IDE 2.3.2\library' = (Join-Path $TcRootShort 'Library')
    'C:\Program Files\Sunplus\unSP IDE 2.3.2\library'       = (Join-Path $TcRootShort 'Library')
    'C:\PROGRA~2\Sunplus\UNSPID~1.2\library'                = (Join-Path $TcRootShort 'Library')
}
foreach ($k in $likRepl.Keys) { $lik = $lik.Replace($k, $likRepl[$k]) }
Write-Bytes -Path ($LikPathBase + '.lik') -Bytes ($latin.GetBytes($lik))

# ---- the IDE's Makefile does NOT link through the .lik; its link rule is
#      "$(LD) -at <project>.ary <project>.TSK ..." and the .ary is the object +
#      library list the IDE writes for the linker.  Rewrite the project's .ary
#      (or synthesise it from the .lik when the IDE never wrote one).
$arySrc = @((Join-Path $SrcDir ('Debug\' + $Project + '.ary')),
            (Join-Path $SrcDir ('DebugSim\' + $Project + '.ary')),
            (Join-Path $SrcDir ($Project + '.ary'))) |
          Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($arySrc -and (Test-Path -LiteralPath $arySrc)) {
    $ary = $latin.GetString([System.IO.File]::ReadAllBytes($arySrc))
    foreach ($k in $likRepl.Keys) { $ary = $ary.Replace($k, $likRepl[$k]) }
} else {
    $ary = (($lik -split "`n" | Where-Object { $_ -match '^(Obj|Lib|LibPath|PrjPath|IDE_Version|IDE_User):' }) -join "`n") + "`n"
}
Write-Bytes -Path (Join-Path $OutDir ($Project + '.ary')) -Bytes ($latin.GetBytes($ary))

# --------------------------------------------------------------------------- compile
$log = Join-Path $LogDir ("make_$BuildTag.log")
$summary = [ordered]@{
    Project   = $Project
    Config    = $Config
    Toolchain = $TcRoot
    RunMode   = $RunMode
    SrcDir    = $SrcDir
    OutDir    = $OutDir
    Makefile  = $MakeFilePath
    Lik       = ($LikPathBase + '.lik')
    Log       = $log
}
if ($patchLog.Count) { $summary['SourcePatch'] = ($patchLog -join '; ') }
$toolProbe = [ordered]@{ 'xasm16.exe' = @(); 'xlink16.exe' = @('__probe__'); 'gcc.exe' = @() }
foreach ($tool in $toolProbe.Keys) {
    try {
        $v = (& (Join-Path $TcBin $tool) @($toolProbe[$tool]) 2>&1 | Select-Object -First 1)
        if ($v) { $summary[$tool] = ("$v").Trim() }
    } catch { }
}

if (-not $NoCompile) {
    $makeArgs = @('-f', $MakeFilePath, '-k')
    if ($Jobs -gt 1) { $makeArgs += "-j$Jobs" }
    $makeArgs += 'compile'

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $cmdline = '"' + $MakeExe + '" ' + (($makeArgs | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' ')
    if (-not $Quiet) { Write-Host "make: $cmdline" }
    Push-Location $RunDir
    try {
        $text = Use-LegacyCodePage { (cmd /c ($cmdline + ' 2>&1')) -join "`r`n" }
    } finally { Pop-Location }
    $sw.Stop()
    [System.IO.File]::WriteAllText($log, $text, $gbk)

    $makeExit = $LASTEXITCODE
    $summary['MakeExitCode'] = $makeExit
    $summary['Seconds']      = [int]$sw.Elapsed.TotalSeconds
}

# --------------------------------------------------------------------------- verify + link
$expected = Get-Content -LiteralPath ($LikPathBase + '.lik') -Encoding Default |
            Where-Object { $_ -match '^Obj:' } |
            ForEach-Object { Split-Path (($_ -replace '^Obj:\s*"?', '' -replace '"?\s*$', '')) -Leaf }
$present = @()
if (Test-Path $OutDir) {
    $present = Get-ChildItem -LiteralPath $OutDir -File | ForEach-Object { $_.Name }
}
$missing = $expected | Where-Object { $present -notcontains $_ }
$summary['ExpectedObjects'] = $expected.Count
$summary['BuiltObjects']    = ($expected.Count - @($missing).Count)
$summary['MissingObjects']  = @($missing).Count
if (@($missing).Count -and -not $Quiet) {
    Write-Host ("missing objects: " + (@($missing) -join ', '))
}
if (@($missing).Count -and (Test-Path $log)) {
    $tmpHit = Select-String -LiteralPath $log -Pattern 'No more tmp file can be used in macro processing' -Encoding Default
    if ($tmpHit) {
        $files = $tmpHit | ForEach-Object { ([regex]::Match($_.Line, '([^\\]+)\.asm\(', 'IgnoreCase')).Groups[1].Value } |
                 Where-Object { $_ } | Sort-Object -Unique
        $hint = "xasm16 macro temp-file pool exhausted in " + ($files -join ', ') +
                ' - see docs\NC3001固件编译说明.md section 4.1'
        $summary['Hint'] = $hint
    }
}

if (-not $NoLink -and @($missing).Count -eq 0) {
    # link exactly the way the IDE's Makefile does it:
    #   $(LD) -as <ary> <tsk> -initdata -body SPL162001_Flash -nobdy -bfile <bdy>
    # (calling "xlink16 <project>" instead reads <project>.lik, which is what the
    #  IDE's LikEditor keeps - but the enc linker then trips over decrypt.tmp)
    $linkArgs = @('-f', $MakeFilePath)
    if ($Jobs -gt 1) { $linkArgs += "-j$Jobs" }
    $linkArgs += 'all'
    $linkCmd = '"' + $MakeExe + '" ' + (($linkArgs | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' ')
    Push-Location $RunDir
    try {
        $linkText = Use-LegacyCodePage { (cmd /c ($linkCmd + ' 2>&1')) -join "`r`n" }
        $linkExit = $LASTEXITCODE
    } finally { Pop-Location }
    [System.IO.File]::WriteAllText((Join-Path $LogDir ("link_$BuildTag.log")), $linkText, $gbk)
    $summary['LinkExitCode'] = $linkExit
    if (-not $Quiet) { Write-Host "link: exit $linkExit" }
    $summary['Outputs'] = @(Get-ChildItem -LiteralPath $OutDir -File -ErrorAction SilentlyContinue |
                            Where-Object { $_.Extension -in '.tsk', '.S37', '.s37', '.sbm', '.ary', '.map' } |
                            ForEach-Object { $_.Name + '(' + $_.Length + ')' })
}

if (-not $Quiet) {
    $summary.GetEnumerator() | ForEach-Object { "{0,-16} {1}" -f $_.Key, $_.Value }
}
if ($SubstDrive) {
    [void](cmd /c ('subst ' + $SubstDrive + ': /d'))
    if (-not $Quiet) { Write-Host ("subst {0}: removed" -f $SubstDrive) }
}
[pscustomobject]$summary
