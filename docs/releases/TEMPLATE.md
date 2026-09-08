# SXPE X.Y.Z — release notes

**Tag:** `vX.Y.Z`

## Published assets

List every artifact that ships on this tag (or say honestly when one is local-only):

| Asset | Status |
| --- | --- |
| `sxpe-X.Y.Z-windows-x64.zip` | Windows portable (GUI + CLI + MCP + Qt plugins) — `package.bat` / `scripts/package.ps1`; Releases upload tracked by [#54](https://github.com/tofb15/sxpe/issues/54) |
| `sxpe-X.Y.Z-linux-x64-cli.tar.gz` | Linux CLI + MCP (no Qt) — `scripts/package-linux.sh --no-gui` |
| `sxpe-X.Y.Z-linux-x64.tar.gz` | Optional Linux tarball including `sxpe_gui` when Qt was present at build — `scripts/package-linux.sh` (does **not** vendor Qt libs) |

## Highlights

- …

## Install

### Windows portable

1. Download / unzip `sxpe-X.Y.Z-windows-x64.zip` (or build locally with `package.bat`).
2. Run `SXPE.bat`. CLI: `sxpe-cli.bat`. MCP: `sxpe-mcp.bat`.

### Linux CLI + MCP

1. Download / extract `sxpe-*-linux-x64-cli.tar.gz` (or run `./scripts/package-linux.sh --no-gui`).
2. Run `./sxpe` / `./sxpe_mcp` (or the `*-cli.sh` / `*-mcp.sh` launchers).

### Linux GUI

Build with Qt 6.5+ present ([docs/building.md](../building.md)), or package with `./scripts/package-linux.sh` when `sxpe_gui` exists. System/official Qt must resolve at runtime.

## Building from source

See [docs/building.md](../building.md).

## Limits (honesty)

- Sims 3 only; no Sims 4 profile.
- No full 3D mesh / CLIP playback.
- No Store/DRM Sims3Pack.
- Layout-locked `.nhd` / `.world` / `.dbc`.
- Third-party GUI plugins permanently unsupported (#60).
