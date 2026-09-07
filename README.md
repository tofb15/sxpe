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

GitHub Actions (`.github/workflows/ci.yml`) runs Linux CLI/MCP without Qt, and a Windows job that installs Qt and runs `gui_smoke`. Optional local FullBuild/CC checks are not CI.

On Windows with Visual Studio 2022/2026 (MSVC):

```text
build.bat
```

(`scripts/build.ps1` is the same command.) Double-click `SXPE.bat` to run the GUI from `build/`.

Portable folder (GUI + CLI + MCP, Qt plugins, MSVC runtime) in `dist/sxpe`, plus a zip:

```text
package.bat
```

(`scripts/package.ps1` is the same command.) Use `-SkipBuild` to package the current `build/` output. The zip includes `SXPE.bat`, `sxpe-cli.bat`, and `sxpe-mcp.bat`. Keep the DLL and plugin subfolders next to the exes when you share the folder.

Requires CMake 3.28+ and a C++23 compiler. The GUI needs Qt 6 Widgets (`find_package(Qt6)`). If Qt lives next to the repo as `../qt/6.8.2/msvc2022_64`, CMake picks it up. Without Qt, CLI and MCP still build.

```text
sxpe_gui path\to\file.package
sxpe --version
sxpe --help
sxpe help resource
sxpe resource rename --help
sxpe resource list --package path\to\file.package
sxpe resource rename --package path\to\file.package --type 0x0333406C --group 0 --instance 0x1 --name NRaas.NoCD --force
sxpe sims3pack list --path mod.sims3pack
sxpe sims3pack extract --path mod.sims3pack --out-dir out --index 0 --force
```

Commands are `noun verb` (`resource list`, `package info`). `--package PATH` is one-shot: open, run, save if it writes, close. Results are JSON `{ok, data|error}`; `--format text` or `table` prints a human view. `sxpe help` is the command catalog (same list MCP uses). `sxpe <noun> <verb> --help` shows that command.

Resource **Name** is the package name map (NMAP). `nmap.set` / `resource.rename` write it and **create an NMAP if the package has none**.

## Layout

- `include/sxpe/games` — game-profile interfaces
- `include/sxpe/games/sims3` — Sims 3 profile (v1)
- `include/sxpe/core` — registry, mmap, caps
- `include/sxpe/commands` — command bus
- `src/cli` / `src/mcp` — Qt-free agent binaries
- `src/` — implementations
- `tests/` — unit tests (synthetic fixtures only)
- `fixtures/synthetic/` — invented DBPF/RefPack bytes (not game files)
- `docs/testing.md` — optional local FullBuild/CC round-trip (`scripts/roundtrip.ps1`); gitignored paths only
- `docs/neighborhood-layout.md` — `.nhd`/`.world`/`.dbc` layout lock (safe in-place replace vs refused)

Do not commit game packages, custom-content, or other copyrighted binaries.

Drop several `.package` files on the GUI to merge them; SXPE writes an `SXMM` manifest so **Tools → Un-merge package** can recreate the sources. Only SXPE-manifest merges are reversible.
