# Stage a shareable Windows x64 folder: GUI + CLI + MCP, Qt plugins, MSVC CRT
# (default), or CLI + MCP only with -NoGui / -CliOnly (no Qt / no sxpe_gui).
# Does not copy EA packages, PDBs (unless -IncludePdb), or test binaries.
#
#   powershell -ExecutionPolicy Bypass -File scripts/package.ps1
#   powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -SkipBuild
#   powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -SkipBuild -NoGui
#
# Output:
#   dist/sxpe/  and  dist/sxpe-<version>-windows-x64.zip       (default / GUI)
#   dist/sxpe/  and  dist/sxpe-<version>-windows-x64-cli.zip   (-NoGui)
param(
    [string]$BuildDir = "",
    [string]$OutDir = "",
    [switch]$SkipBuild,
    [switch]$NoZip,
    [switch]$IncludePdb,
    [Alias("CliOnly")]
    [switch]$NoGui
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build" }
if (-not $OutDir) { $OutDir = Join-Path $Root "dist\sxpe" }

function Find-VsPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio with C++ tools." }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No MSVC installation found." }
    return $vs
}

function Read-SxpeVersion {
    $cmake = Get-Content (Join-Path $Root "CMakeLists.txt") -Raw
    if ($cmake -match 'project\(\s*sxpe\s+VERSION\s+([0-9.]+)') {
        return $Matches[1]
    }
    return "0.0.0"
}

function Find-QtPrefix {
    $hint = Join-Path (Split-Path $Root -Parent) "qt\6.8.2\msvc2022_64"
    if (Test-Path (Join-Path $hint "bin\windeployqt.exe")) { return $hint }
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cache) {
        foreach ($line in Get-Content $cache) {
            if ($line -match '^Qt6_DIR:PATH=(.+)$') {
                $qt6dir = $Matches[1].Trim()
                $prefix = (Resolve-Path (Join-Path $qt6dir "..\..\..")).Path
                if (Test-Path (Join-Path $prefix "bin\windeployqt.exe")) { return $prefix }
            }
        }
    }
    if ($env:CMAKE_PREFIX_PATH) {
        foreach ($p in $env:CMAKE_PREFIX_PATH -split ';') {
            if (Test-Path (Join-Path $p "bin\windeployqt.exe")) { return $p }
        }
    }
    return $null
}

function Copy-VcRuntime([string]$Dest, [string]$Vs) {
    $redist = Join-Path $Vs "VC\Redist\MSVC"
    if (-not (Test-Path $redist)) { return }
    $crt = Get-ChildItem $redist -Recurse -Directory -Filter "Microsoft.VC*.CRT" |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $crt) { return }
    Copy-Item (Join-Path $crt.FullName "*.dll") $Dest -Force
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Resolve-StagedExeDir([string]$Dir, [string[]]$Required) {
    foreach ($sub in @("", "RelWithDebInfo", "Release")) {
        $d = if ($sub) { Join-Path $Dir $sub } else { $Dir }
        $ok = $true
        foreach ($n in $Required) {
            if (-not (Test-Path (Join-Path $d $n))) { $ok = $false; break }
        }
        if ($ok) { return $d }
    }
    return $Dir
}
$need = if ($NoGui) { @("sxpe.exe", "sxpe_mcp.exe") } else { @("sxpe.exe", "sxpe_mcp.exe", "sxpe_gui.exe") }
$ExeDir = Resolve-StagedExeDir $BuildDir $need
foreach ($n in $need) {
    $p = Join-Path $ExeDir $n
    if (-not (Test-Path $p)) { throw "Missing $p. Build first or omit -SkipBuild." }
}

$qt = $null
$windeployqt = $null
if (-not $NoGui) {
    $qt = Find-QtPrefix
    $windeployqt = if ($qt) { Join-Path $qt "bin\windeployqt.exe" } else { $null }
    if (-not $windeployqt -or -not (Test-Path $windeployqt)) {
        throw "windeployqt.exe not found. Point CMAKE_PREFIX_PATH at Qt, or keep Qt at ../qt/6.8.2/msvc2022_64."
    }
}

function Clear-OutDir([string]$Path) {
    if (-not (Test-Path $Path)) { return }
    for ($i = 0; $i -lt 8; $i++) {
        try {
            Remove-Item $Path -Recurse -Force -ErrorAction Stop
            if (-not (Test-Path $Path)) { return }
        } catch {
            Start-Sleep -Milliseconds (250 * ($i + 1))
        }
    }
    $park = "$Path.old.$PID"
    try {
        Rename-Item $Path $park -ErrorAction Stop
    } catch {
        throw "Cannot replace $Path (in use). Close SXPE or any Explorer window in that folder, then retry."
    }
    Remove-Item $park -Recurse -Force -ErrorAction SilentlyContinue
}

Clear-OutDir $OutDir
New-Item -ItemType Directory -Path $OutDir | Out-Null

foreach ($n in $need) {
    Copy-Item (Join-Path $ExeDir $n) (Join-Path $OutDir $n) -Force
    if ($IncludePdb) {
        $pdb = [IO.Path]::ChangeExtension((Join-Path $ExeDir $n), ".pdb")
        if (Test-Path $pdb) { Copy-Item $pdb $OutDir -Force }
    }
}

Copy-Item (Join-Path $Root "LICENSE") (Join-Path $OutDir "LICENSE") -Force
Copy-Item (Join-Path $Root "NOTICE") (Join-Path $OutDir "NOTICE") -Force
$launchers = Join-Path $PSScriptRoot "portable"
if ($NoGui) {
    foreach ($bat in @("sxpe-cli.bat", "sxpe-mcp.bat")) {
        $bp = Join-Path $launchers $bat
        if (-not (Test-Path $bp)) { throw "Missing $bp" }
        Copy-Item $bp $OutDir -Force
    }
} else {
    if (-not (Test-Path (Join-Path $launchers "SXPE.bat"))) {
        throw "Missing $launchers\SXPE.bat"
    }
    Copy-Item (Join-Path $launchers "*.bat") $OutDir -Force
}

$vs = $null
try {
    $vs = Find-VsPath
} catch {
    if (-not $NoGui) { throw }
    Write-Warning $_.Exception.Message
}

if (-not $NoGui) {
    $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
    $gui = Join-Path $OutDir "sxpe_gui.exe"
    $wdCmd = 'call "' + $vcvars + '" && "' + $windeployqt + '" --release --compiler-runtime --no-translations --no-opengl-sw "' + $gui + '"'
    cmd.exe /c $wdCmd
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)." }
}

if ($vs) {
    Copy-VcRuntime $OutDir $vs
}
# CRT DLLs sit next to the exes; the redist installer is not needed in a portable folder.
$redistExe = Join-Path $OutDir "vc_redist.x64.exe"
if (Test-Path $redistExe) { Remove-Item $redistExe -Force }

# RelWithDebInfo is a release binary; drop *d.dll debug companions if both exist.
Get-ChildItem $OutDir -Recurse -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.BaseName -match 'd$') {
        $rel = Join-Path $_.DirectoryName ($_.BaseName.Substring(0, $_.BaseName.Length - 1) + $_.Extension)
        if (Test-Path $rel) { Remove-Item $_.FullName -Force }
    }
}

if (-not $NoGui) {
    $platforms = Join-Path $OutDir "platforms\qwindows.dll"
    if (-not (Test-Path $platforms)) {
        throw "Packaging did not produce platforms\qwindows.dll (Qt platform plugin)."
    }
}

$ver = Read-SxpeVersion
if ($NoGui) {
    $readme = @"
SXPE $ver (Windows x64 CLI)
===========================

This folder is CLI + MCP only (no GUI, no Qt). Keep any CRT DLLs next to the exes.

  sxpe-cli.bat     CLI (double-click for help, or pass arguments)
  sxpe-mcp.bat     MCP stdio
  sxpe.exe         CLI (Qt-free)
  sxpe_mcp.exe     MCP stdio (Qt-free)

CLI:
  sxpe-cli.bat --help
  sxpe-cli.bat --version
  sxpe-cli.bat help
  sxpe-cli.bat help resource
  sxpe-cli.bat resource rename --help

How the CLI works:
  Commands are noun + verb (resource list, package info).
  --package PATH is one-shot: open, run, save if it writes, close.
  Every command returns JSON {ok, data} or {ok, error}.
  --format text or table prints a human table; json is compact JSON.
  --force / --dry-run gate writes. --type/--group/--instance accept hex 0x...

  sxpe-cli.bat package info --package path\to\file.package --format json

MCP:
  sxpe-mcp.bat

For the Windows GUI portable zip (Qt plugins + SXPE.bat), use scripts/package.ps1
without -NoGui, or download sxpe-<ver>-windows-x64.zip from GitHub Releases.

License: GPL-3.0-or-later (LICENSE and NOTICE).
Unofficial The Sims 3 package editor. Not affiliated with Electronic Arts.
"@
} else {
    $readme = @"
SXPE $ver (Windows x64)
=======================

This folder is standalone. Keep the DLLs and Qt plugin subfolders (platforms/, etc.) next to the exes.

  SXPE.bat         Windows GUI (double-click; working directory is this folder)
  sxpe-cli.bat     CLI (double-click for help, or pass arguments)
  sxpe-mcp.bat     MCP stdio
  sxpe_gui.exe     Windows GUI
  sxpe.exe         CLI (Qt-free)
  sxpe_mcp.exe     MCP stdio (Qt-free)

GUI:
  SXPE.bat
  SXPE.bat path\to\file.package

CLI:
  sxpe-cli.bat --help
  sxpe-cli.bat --version
  sxpe-cli.bat help
  sxpe-cli.bat help resource
  sxpe-cli.bat resource rename --help

How the CLI works:
  Commands are noun + verb (resource list, package info).
  --package PATH is one-shot: open, run, save if it writes, close.
  Every command returns JSON {ok, data} or {ok, error}.
  --format text or table prints a human table; json is compact JSON.
  --force / --dry-run gate writes. --type/--group/--instance accept hex 0x...

Rename a door (NMAP name), one-shot:
  sxpe-cli.bat resource rename --package door.package --type 0x0333406C --group 0 --instance 0x1 --name NRaas.NoCD --force

  sxpe-cli.bat package info --package path\to\file.package --format json

Platform limits:
  - Third-party GUI plugins / DLL Handlers are permanently unsupported (#60).
  - External programs use {path} substitution (Settings → External programs).
  - Windows portable zip on GitHub Releases is tracked by #54; this local package is the supported path today.

License: GPL-3.0-or-later (LICENSE and NOTICE).
Unofficial The Sims 3 package editor. Not affiliated with Electronic Arts.
"@
}
[System.IO.File]::WriteAllText((Join-Path $OutDir "README.txt"), $readme)

$files = Get-ChildItem $OutDir -Recurse -File
Write-Host "Staged $($files.Count) files in $OutDir"

if (-not $NoZip) {
    $distRoot = Split-Path $OutDir -Parent
    if (-not (Test-Path $distRoot)) { New-Item -ItemType Directory -Path $distRoot | Out-Null }
    $zipName = if ($NoGui) { "sxpe-$ver-windows-x64-cli.zip" } else { "sxpe-$ver-windows-x64.zip" }
    $zip = Join-Path $distRoot $zipName
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path $OutDir -DestinationPath $zip -CompressionLevel Optimal
    Write-Host "Zip $zip"
}

Write-Host "Done."
