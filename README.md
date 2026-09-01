# SXPE

**SXPE** is a 2026 reimplementation of a Sims 3 package editor: inspect and edit DBPF `.package` files used by *The Sims 3*.

- **Language:** C++23 (CMake)
- **Humans:** Windows desktop app (Qt 6 Widgets, later milestone)
- **Agents / scripts:** JSON/JSONL CLI and MCP stdio (Qt-free, later milestone)
- **License:** [GPL-3.0-or-later](LICENSE)
- **v1 target:** The Sims 3 only

The core is built so additional *game profiles* can be added later (The Sims 4 is the likely next profile). **v1 does not implement other games.** Unknown formats are refused.

This project is unofficial. The Sims 3 is a trademark of Electronic Arts. SXPE is not affiliated with EA and is not Peter L Jones’s s3pe.

## Status

M5: Windows Qt 6 GUI (`sxpe_gui`) plus CLI (`sxpe`) and MCP (`sxpe_mcp`). Open/save packages, virtualized resource grid, filter, preview/hex/graph, dialogs. CLI/MCP stay Qt-free.

```text
cmake --preset default
cmake --build --preset default
ctest --preset default
```

On Windows with Visual Studio 2022/2026 (MSVC):

```text
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
```

Portable folder (GUI + CLI + MCP, Qt plugins, MSVC runtime) in `dist/sxpe`, plus a zip:

```text
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```

Use `-SkipBuild` to package the current `build/` output. Keep the DLL and plugin subfolders next to the exes when you share the folder.

Requires CMake 3.28+ and a C++23 compiler. The GUI needs Qt 6 Widgets (`find_package(Qt6)`). If Qt lives next to the repo as `../qt/6.8.2/msvc2022_64`, CMake picks it up. Without Qt, CLI and MCP still build.

```text
sxpe_gui path\to\file.package
```

## Layout

- `include/sxpe/games` — game-profile interfaces
- `include/sxpe/games/sims3` — Sims 3 profile (v1)
- `include/sxpe/core` — registry, mmap, caps
- `include/sxpe/commands` — command bus
- `src/cli` / `src/mcp` — Qt-free agent binaries
- `src/` — implementations
- `tests/` — unit tests (synthetic fixtures only)
- `fixtures/synthetic/` — invented DBPF/RefPack bytes (not game files)

Do not commit game packages, custom-content, or other copyrighted binaries.
