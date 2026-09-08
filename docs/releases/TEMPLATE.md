# SXPE X.Y.Z - release notes

**Tag:** `vX.Y.Z`

Pick `X.Y.Z` with [docs/versioning.md](../versioning.md) (one bump per Release; highest match since the last tag). `PROJECT_VERSION` and this tag must be the same number.

## Published assets

List every artifact that ships on this tag (or say honestly when one is local-only):

| Asset | Status |
| --- | --- |
| `sxpe-X.Y.Z-windows-x64.zip` | Windows portable (GUI + CLI + MCP + Qt plugins) - `package.bat` / `scripts/package.ps1` |
| `sxpe-X.Y.Z-windows-x64-cli.zip` | Windows CLI + MCP only - `scripts/package.ps1 -NoGui` |
| `sxpe-X.Y.Z-linux-x64-cli.tar.gz` | Linux CLI + MCP (no Qt) - `scripts/package-linux.sh --no-gui` |
| `sxpe-X.Y.Z-linux-x64.tar.gz` | Linux CLI + MCP + `sxpe_gui` - `scripts/package-linux.sh` (does **not** vendor Qt libs) |

All four are attached by `.github/workflows/release.yml` on `v*` tags (and `workflow_dispatch`).

## Highlights

- …

## Install

### Windows portable

1. Download / unzip `sxpe-X.Y.Z-windows-x64.zip` (or build locally with `package.bat`).
2. Run `SXPE.bat`. CLI: `sxpe-cli.bat`. MCP: `sxpe-mcp.bat`.

### Windows CLI + MCP

1. Download / unzip `sxpe-X.Y.Z-windows-x64-cli.zip` (or `scripts/package.ps1 -NoGui`).
2. Run `sxpe-cli.bat` / `sxpe-mcp.bat`.

### Linux CLI + MCP

1. Download / extract `sxpe-*-linux-x64-cli.tar.gz` (or run `./scripts/package-linux.sh --no-gui`).
2. Run `./sxpe` / `./sxpe_mcp` (or the `*-cli.sh` / `*-mcp.sh` launchers).

### Linux GUI

1. Download / extract `sxpe-*-linux-x64.tar.gz` (or `./scripts/package-linux.sh` when `sxpe_gui` exists).
2. Run `./SXPE.sh`. System/official Qt 6.5+ must resolve at runtime (not vendored). See [docs/building.md](../building.md).

## Building from source

See [docs/building.md](../building.md).

## Limits (honesty)

- Sims 3 only; no Sims 4 profile.
- No full 3D mesh / CLIP playback.
- No Store/DRM Sims3Pack.
- Layout-locked `.nhd` / `.world` / `.dbc`.
- Third-party GUI plugins permanently unsupported (#60).
