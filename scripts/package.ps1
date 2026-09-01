# Stage a shareable Windows x64 folder: GUI + CLI + MCP, Qt plugins, MSVC CRT.
# Does not copy EA packages, PDBs (unless -IncludePdb), or test binaries.
#
#   powershell -ExecutionPolicy Bypass -File scripts/package.ps1
#   powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -SkipBuild
#
# Output: dist/sxpe/  and  dist/sxpe-<version>-windows-x64.zip
param(
    [string]$BuildDir = "",
    [string]$OutDir = "",
    [switch]$SkipBuild,
    [switch]$NoZip,
    [switch]$IncludePdb
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

$need = @("sxpe.exe", "sxpe_mcp.exe", "sxpe_gui.exe")
foreach ($n in $need) {
    $p = Join-Path $BuildDir $n
    if (-not (Test-Path $p)) { throw "Missing $p. Build first or omit -SkipBuild." }
}

$qt = Find-QtPrefix
$windeployqt = if ($qt) { Join-Path $qt "bin\windeployqt.exe" } else { $null }
if (-not $windeployqt -or -not (Test-Path $windeployqt)) {
    throw "windeployqt.exe not found. Point CMAKE_PREFIX_PATH at Qt, or keep Qt at ../qt/6.8.2/msvc2022_64."
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
    Copy-Item (Join-Path $BuildDir $n) (Join-Path $OutDir $n) -Force
    if ($IncludePdb) {
        $pdb = [IO.Path]::ChangeExtension((Join-Path $BuildDir $n), ".pdb")
        if (Test-Path $pdb) { Copy-Item $pdb $OutDir -Force }
    }
}

Copy-Item (Join-Path $Root "LICENSE") (Join-Path $OutDir "LICENSE") -Force
Copy-Item (Join-Path $Root "NOTICE") (Join-Path $OutDir "NOTICE") -Force
$launchers = Join-Path $PSScriptRoot "portable"
if (-not (Test-Path (Join-Path $launchers "SXPE.bat"))) {
    throw "Missing $launchers\SXPE.bat"
}
Copy-Item (Join-Path $launchers "*.bat") $OutDir -Force

$vs = Find-VsPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$gui = Join-Path $OutDir "sxpe_gui.exe"
$wdCmd = 'call "' + $vcvars + '" && "' + $windeployqt + '" --release --compiler-runtime --no-translations --no-opengl-sw "' + $gui + '"'
cmd.exe /c $wdCmd
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)." }

Copy-VcRuntime $OutDir $vs
# CRT DLLs sit next to the exes; the redist installer is not needed in a portable folder.
$redistExe = Join-Path $OutDir "vc_redist.x64.exe"
if (Test-Path $redistExe) { Remove-Item $redistExe -Force }

# RelWithDebInfo is a release binary; drop *d.dll debug companions if both exist.
Get-ChildItem $OutDir -Recurse -Filter "*.dll" | ForEach-Object {
    if ($_.BaseName -match 'd$') {
        $rel = Join-Path $_.DirectoryName ($_.BaseName.Substring(0, $_.BaseName.Length - 1) + $_.Extension)
        if (Test-Path $rel) { Remove-Item $_.FullName -Force }
    }
}

$platforms = Join-Path $OutDir "platforms\qwindows.dll"
if (-not (Test-Path $platforms)) {
    throw "Packaging did not produce platforms\qwindows.dll (Qt platform plugin)."
}

$ver = Read-SxpeVersion
$readme = @"
SXPE $ver (Windows x64)
=======================

This folder is standalone. Keep the DLLs and plugin subfolders next to the exes.

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
  sxpe-cli.bat package info --package path\to\file.package --format json

License: GPL-3.0-or-later (LICENSE and NOTICE).
Unofficial The Sims 3 package editor. Not affiliated with Electronic Arts.
"@
[System.IO.File]::WriteAllText((Join-Path $OutDir "README.txt"), $readme)

$files = Get-ChildItem $OutDir -Recurse -File
Write-Host "Staged $($files.Count) files in $OutDir"

if (-not $NoZip) {
    $distRoot = Split-Path $OutDir -Parent
    if (-not (Test-Path $distRoot)) { New-Item -ItemType Directory -Path $distRoot | Out-Null }
    $zip = Join-Path $distRoot "sxpe-$ver-windows-x64.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path $OutDir -DestinationPath $zip -CompressionLevel Optimal
    Write-Host "Zip $zip"
}

Write-Host "Done."
