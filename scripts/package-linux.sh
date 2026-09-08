#!/usr/bin/env bash
# Stage a shareable Linux x64 tarball: CLI + MCP, and optionally GUI when built with Qt.
# Does not vendor Qt shared libraries — the GUI binary needs system or official Qt at runtime.
# Windows keeps scripts/package.ps1 / package.bat.
#
#   ./scripts/package-linux.sh
#   ./scripts/package-linux.sh --skip-build
#   ./scripts/package-linux.sh --no-gui          # CLI+MCP only even if sxpe_gui exists
#   ./scripts/package-linux.sh --build-dir build --out-dir dist/sxpe
#
# Output:
#   dist/sxpe/                                  staged folder
#   dist/sxpe-<ver>-linux-x64-cli.tar.gz        when GUI omitted
#   dist/sxpe-<ver>-linux-x64.tar.gz            when GUI included
set -euo pipefail

Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BuildDir="${Root}/build"
OutDir="${Root}/dist/sxpe"
SkipBuild=0
ForceNoGui=0
NoTar=0

usage() {
  sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) SkipBuild=1; shift ;;
    --no-gui) ForceNoGui=1; shift ;;
    --no-tar) NoTar=1; shift ;;
    --build-dir) BuildDir="$2"; shift 2 ;;
    --out-dir) OutDir="$2"; shift 2 ;;
    -h|--help) usage 0 ;;
    *) echo "Unknown option: $1" >&2; usage 1 ;;
  esac
done

read_version() {
  local cmake="${Root}/CMakeLists.txt"
  if [[ -f "$cmake" ]] && grep -Eq 'project\(\s*sxpe\s+VERSION\s+[0-9.]+' "$cmake"; then
    sed -nE 's/.*project\(\s*sxpe\s+VERSION\s+([0-9.]+).*/\1/p' "$cmake" | head -1
  else
    echo "0.0.0"
  fi
}

if [[ "$SkipBuild" -eq 0 ]]; then
  if [[ ! -f "${Root}/CMakePresets.json" ]]; then
    echo "Missing CMakePresets.json" >&2
    exit 1
  fi
  cmake --preset default
  cmake --build --preset default --parallel
fi

need=(sxpe sxpe_mcp)
for n in "${need[@]}"; do
  if [[ ! -x "${BuildDir}/${n}" ]]; then
    echo "Missing ${BuildDir}/${n}. Build first or omit --skip-build." >&2
    exit 1
  fi
done

include_gui=0
if [[ "$ForceNoGui" -eq 0 && -x "${BuildDir}/sxpe_gui" ]]; then
  include_gui=1
fi

rm -rf "$OutDir"
mkdir -p "$OutDir"

cp -a "${BuildDir}/sxpe" "${BuildDir}/sxpe_mcp" "$OutDir/"
cp -a "${Root}/LICENSE" "${Root}/NOTICE" "$OutDir/"
if [[ "$include_gui" -eq 1 ]]; then
  cp -a "${BuildDir}/sxpe_gui" "$OutDir/"
fi

# Thin launchers (cwd = package dir) so PATH tricks are unnecessary for non-builders.
cat > "${OutDir}/sxpe-cli.sh" <<'LAUNCH'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/sxpe" "$@"
LAUNCH
cat > "${OutDir}/sxpe-mcp.sh" <<'LAUNCH'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$DIR/sxpe_mcp" "$@"
LAUNCH
chmod +x "${OutDir}/sxpe" "${OutDir}/sxpe_mcp" "${OutDir}/sxpe-cli.sh" "${OutDir}/sxpe-mcp.sh"
if [[ "$include_gui" -eq 1 ]]; then
  cat > "${OutDir}/SXPE.sh" <<'LAUNCH'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Prefer XCB if Wayland decorations/input misbehave: QT_QPA_PLATFORM=xcb ./SXPE.sh
exec "$DIR/sxpe_gui" "$@"
LAUNCH
  chmod +x "${OutDir}/sxpe_gui" "${OutDir}/SXPE.sh"
fi

ver="$(read_version)"
{
  echo "SXPE ${ver} (Linux x64)"
  echo "======================="
  echo
  echo "This folder is standalone for CLI/MCP. Keep binaries next to the launchers."
  echo
  echo "  sxpe / sxpe-cli.sh     CLI (Qt-free)"
  echo "  sxpe_mcp / sxpe-mcp.sh MCP stdio (Qt-free)"
  if [[ "$include_gui" -eq 1 ]]; then
    echo "  sxpe_gui / SXPE.sh     Qt GUI (requires Qt 6.5+ Widgets+Network on this machine)"
  else
    echo
    echo "GUI was not included (sxpe_gui missing or --no-gui). Build with Qt present and"
    echo "re-run without --no-gui, or run from a build tree: see docs/building.md."
  fi
  echo
  echo "CLI:"
  echo "  ./sxpe-cli.sh --help"
  echo "  ./sxpe-cli.sh resource list --package path/to/file.package"
  echo
  echo "MCP:"
  echo "  ./sxpe-mcp.sh"
  echo
  if [[ "$include_gui" -eq 1 ]]; then
    echo "GUI:"
    echo "  ./SXPE.sh"
    echo "  ./SXPE.sh path/to/file.package"
    echo "  QT_QPA_PLATFORM=offscreen ./SXPE.sh --smoke path/to/file.package   # headless smoke"
    echo
    echo "Honest limit: this tarball does NOT vendor Qt shared libraries. Install Qt 6.5+"
    echo "(distro qt6-base packages or official gcc_64 kit) so libQt6Widgets/Network resolve."
  fi
  echo
  echo "Platform limits:"
  echo "  - Third-party GUI plugins / DLL Handlers are permanently unsupported (#60)."
  echo "  - External programs use {path} substitution, e.g.:"
  echo "      hex:  hexdump -C {path}     or   /usr/bin/ghex {path}"
  echo "      text: xdg-open {path}      or   nano {path}"
  echo "      S3SA: ilspycmd {path}      (or any PE/.NET viewer you install)"
  echo
  echo "License: GPL-3.0-or-later (LICENSE and NOTICE)."
  echo "Unofficial The Sims 3 package editor. Not affiliated with Electronic Arts."
} > "${OutDir}/README.txt"

file_count="$(find "$OutDir" -type f | wc -l)"
echo "Staged ${file_count} files in ${OutDir}"

if [[ "$NoTar" -eq 0 ]]; then
  dist_root="$(dirname "$OutDir")"
  mkdir -p "$dist_root"
  if [[ "$include_gui" -eq 1 ]]; then
    archive="${dist_root}/sxpe-${ver}-linux-x64.tar.gz"
  else
    archive="${dist_root}/sxpe-${ver}-linux-x64-cli.tar.gz"
  fi
  rm -f "$archive"
  # Archive the folder as top-level "sxpe/" (matches Windows zip layout).
  tar -C "$(dirname "$OutDir")" -czf "$archive" "$(basename "$OutDir")"
  echo "Tar ${archive}"
fi

echo "Done."
