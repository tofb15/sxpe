#!/usr/bin/env bash
# Smoke-test scripts/package-linux.sh against an existing build/ tree.
# Skips (exit 0) when CLI binaries are missing so bare checkouts stay green.
set -euo pipefail
Root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BuildDir="${Root}/build"
if [[ ! -x "${BuildDir}/sxpe" || ! -x "${BuildDir}/sxpe_mcp" ]]; then
  echo "SKIP: ${BuildDir}/sxpe and/or sxpe_mcp missing (build first)"
  exit 0
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
out="${tmpdir}/sxpe"
"${Root}/scripts/package-linux.sh" --skip-build --build-dir "$BuildDir" --out-dir "$out"

[[ -x "${out}/sxpe" ]]
[[ -x "${out}/sxpe_mcp" ]]
[[ -f "${out}/LICENSE" && -f "${out}/NOTICE" && -f "${out}/README.txt" ]]
[[ -x "${out}/sxpe-cli.sh" && -x "${out}/sxpe-mcp.sh" ]]
"${out}/sxpe" --help >/dev/null

if [[ -x "${BuildDir}/sxpe_gui" ]]; then
  [[ -x "${out}/sxpe_gui" && -x "${out}/SXPE.sh" ]]
fi

parent="$(dirname "$out")"
shopt -s nullglob
tars=("${parent}"/sxpe-*-linux-x64*.tar.gz)
shopt -u nullglob
[[ ${#tars[@]} -ge 1 ]]
listing="$(tar -tzf "${tars[0]}")"
printf '%s\n' "$listing" | grep -qx 'sxpe/sxpe'
printf '%s\n' "$listing" | grep -qx 'sxpe/sxpe_mcp'
echo "OK: package-linux smoke (${tars[0]})"
