# SXPE

**SXPE** is an unofficial *The Sims 3* DBPF package editor. It opens, inspects, and edits `.package` (and related `.world` / `.dbc` / `.nhd`) files used by the game.

**Who it is for**

- Modders who want a modern Windows (and optional Linux) GUI for everyday package work
- Script and agent authors who prefer a Qt-free JSON CLI (`sxpe`) or MCP stdio server (`sxpe_mcp`)

**Not** affiliated with Electronic Arts. **Not** Peter L Jones’s s3pe. The Sims 3 is a trademark of Electronic Arts. [GPL-3.0-or-later](LICENSE).

[![CI](https://github.com/tofb15/sxpe/actions/workflows/ci.yml/badge.svg)](https://github.com/tofb15/sxpe/actions/workflows/ci.yml)

---

## Quick start (non-technical)

**What is on [GitHub Releases](https://github.com/tofb15/sxpe/releases) today (v0.6.0):** a **Linux CLI + MCP** tarball (`sxpe-0.6.0-linux-x64-cli.tar.gz`). There is **no** Windows portable zip on that release yet.

| You want | Do this |
| --- | --- |
| Linux CLI / MCP | Download the tarball from [Releases](https://github.com/tofb15/sxpe/releases/tag/v0.6.0), extract, run `sxpe` / `sxpe_mcp` |
| Windows GUI / CLI / MCP | Build and package locally: `build.bat`, then `package.bat` (or `scripts/package.ps1`). Details: [docs/building.md](docs/building.md). Trackers: [#54](https://github.com/tofb15/sxpe/issues/54) (ship zip on Releases), [#52](https://github.com/tofb15/sxpe/issues/52) (Linux ↔ Windows parity) |
| Linux GUI | Build from source with Qt 6 Widgets present ([docs/building.md](docs/building.md)) |

In the GUI, **Help → Check for update** queries GitHub’s Releases API and never downloads anything without your consent.

---

## Quick start (builders)

**Prerequisites:** CMake 3.28+, a C++23 compiler. GUI needs Qt 6.5+ Widgets (and Network for update check). Without Qt, CLI and MCP still build.

```text
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
```

Windows (MSVC Visual Studio 2022/2026):

```text
build.bat
```

Then double-click `SXPE.bat` to run from `build/`. Portable zip (GUI + CLI + MCP + Qt plugins + MSVC runtime) for local use or manual upload:

```text
package.bat
```

Full Windows / Linux / Wayland notes, packaging, and paste-ready CI jobs: **[docs/building.md](docs/building.md)**.

---

## Features

| Surface | What it does | Docs |
| --- | --- | --- |
| **GUI** (`sxpe_gui`) | Open/save packages, virtualized resource grid, STBL / NMAP / XML editors, merge & un-merge, validate, compare, folder scan, Sims3Pack inspect, S3SA DLL import/export/view | [User guide](docs/user-guide.md) |
| **CLI** (`sxpe`) | `noun verb` commands, JSON envelopes by default | [CLI & MCP](docs/cli-mcp.md) |
| **MCP** (`sxpe_mcp`) | Same command bus over stdio for agents | [CLI & MCP](docs/cli-mcp.md) |

Format reference for codecs and agents: **[docs/spec/](docs/spec/README.md)**. Architecture: [DESIGN.md](DESIGN.md).

**Honest limits (M6):** no Sims 4 profile; no full 3D mesh / CLIP playback; no Store/DRM Sims3Pack; neighborhood / world / DBC files are **layout-locked** (safe in-place replace only — see [neighborhood layout](docs/neighborhood-layout.md)); third-party GUI plugins / DLL Handlers are **permanently unsupported** (no plugin SDK, no LoadLibrary of random DLLs — see CONTRIBUTING).

---

## Common mod workflows

Short recipes; step-by-step: **[docs/workflows.md](docs/workflows.md)**.

| Task | GUI | CLI sketch |
| --- | --- | --- |
| Open a package | File → Open | `sxpe resource list --package file.package` |
| Remove THUM / junk thumbs | Select resources → Delete (or filter + delete) | Delete by TGI after `resource list` |
| Merge packages | **Tools → Merge packages…** (Merge assistant: folder → preview → merge) or drop several files | Same bus steps as assistant (`resource.importPackage` + SXMM) |
| Un-merge | Tools → Un-merge package… (needs SXPE `SXMM` manifest) | `package.unmerge` |
| Import / export S3SA DLL | Resource → Editors → Import DLL / Export S3SA / View S3SA | `s3sa.importDll` / `s3sa.exportDll` / `s3sa.view` |
| Validate before share | Tools → Validate | `sxpe package validate --package …` (via bus id) |
| Compare two packages | Tools → Compare packages… | `package.diff` |
| Folder hygiene scan | Tools → Scan folder… | `sxpe folder scan --path Downloads` |
| Inspect Sims3Pack | File → Open Sims3Pack… / Tools → Inspect Sims3Pack… | `sxpe sims3pack list --path mod.sims3pack` |

---

## If you used s3pe before

SXPE is a separate project (not a fork of s3pe). Familiar jobs map roughly like this:

| You did in s3pe | In SXPE |
| --- | --- |
| Open / Save / Save As | **File** menu (same idea) |
| Drop several packages to combine | **Tools → Merge packages…** (Merge assistant) or drop files → **Merge into new package** |
| Resource → Import → as DBC / from package | **Resource → Import → As DBC…** / **From package(s) into this package…** |
| Helpers (STBL, NMAP, …) | **Resource → Editors** |
| Auto Preview pane | **Inspector** (honest limits: no full 3D / CLIP playback) |
| External hex/text programs | **Settings → External programs** |
| “Just merge my Downloads folder” folklore | Prefer the Merge assistant preview + caps; see [workflows.md](docs/workflows.md) |

Un-merge only works for packages SXPE itself merged (**SXMM** manifest). Mega-packs from other tools are not reversible that way.

## Documentation map

| Doc | Audience |
| --- | --- |
| [docs/user-guide.md](docs/user-guide.md) | Humans — menus, editors, update check, layout lock |
| [docs/cli-mcp.md](docs/cli-mcp.md) | Agents — bus, envelopes, examples |
| [docs/workflows.md](docs/workflows.md) | Task-oriented recipes |
| [docs/building.md](docs/building.md) | Build, package, CI |
| [docs/testing.md](docs/testing.md) | Optional local FullBuild/CC round-trip |
| [docs/spec/](docs/spec/README.md) | Format / command catalog |
| [docs/releases/v0.6.0.md](docs/releases/v0.6.0.md) | M6 release notes |
| [CONTRIBUTING.md](CONTRIBUTING.md) | DCO, scope, hygiene |

Do not commit game packages, custom content, or other copyrighted binaries.

---

## Version & changelog (0.6.0)

Current project version is **0.6.0** (`CMakeLists.txt` `PROJECT_VERSION` / `vcpkg.json`). Highlights for this cut:

- M6 daily-driver GUI work (validate report, compare, XML/NMAP editors, find refs, folder scan, neighborhood honesty, S3SA viewer, Sims3Pack inspect, Linux Qt GUI smoke)
- **Help → Check for update** via GitHub Releases API (no auto-download)
- Docs overhaul for humans and agents

**Published asset:** Linux CLI+MCP tarball on [v0.6.0](https://github.com/tofb15/sxpe/releases/tag/v0.6.0). Windows portable zip is built locally (`package.bat`) until [#54](https://github.com/tofb15/sxpe/issues/54) ships it on Releases.

Release notes: [docs/releases/v0.6.0.md](docs/releases/v0.6.0.md).

**Open roadmap (not closed M6 tracker):** [#52](https://github.com/tofb15/sxpe/issues/52) (Linux ↔ Windows parity), [#53](https://github.com/tofb15/sxpe/issues/53) (this docs wave), [#54](https://github.com/tofb15/sxpe/issues/54) (Windows zip on Releases), [#62](https://github.com/tofb15/sxpe/issues/62) (M7), [#69](https://github.com/tofb15/sxpe/issues/69) (community research).

---

## Licence / disclaimer

SXPE is **GPL-3.0-or-later**. It is an unofficial fan project. Electronic Arts owns The Sims 3. SXPE ships no EA game files. Use at your own risk; keep backups of packages you care about.
