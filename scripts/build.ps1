# Configure and build SXPE (Windows). Does not copy EA packages.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio with C++ tools." }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "No MSVC installation found." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$ninja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if (-not (Test-Path $ninja)) { throw "Ninja not found under Visual Studio CMake tools." }

cmd.exe /c "call `"$vcvars`" && cd /d `"$Root`" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `"-DCMAKE_MAKE_PROGRAM=$ninja`" && cmake --build build && ctest --test-dir build --output-on-failure"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
