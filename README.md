# SXPE

**SXPE** is an unofficial editor for *The Sims 3* package files. It opens, inspects, and edits `.package` files (and related `.world` / `.dbc` / `.nhd`) so the game can still load them.

It is a **new C++23 program**, not a fork of Peter L Jones’s s3pe / sims3tools, and not Electronic Arts software. GPL-3.0-or-later. The Sims 3 is a trademark of Electronic Arts.

[![CI](https://github.com/tofb15/sxpe/actions/workflows/ci.yml/badge.svg)](https://github.com/tofb15/sxpe/actions/workflows/ci.yml)

Current release: **[v0.7.0](https://github.com/tofb15/sxpe/releases/tag/v0.7.0)**.

---

## What SXPE is

Three surfaces, **one command bus** (same operations, same JSON envelopes):

| Surface | Binary | Who it is for |
| --- | --- | --- |
| Desktop GUI | `sxpe_gui` | Everyday mod work on **Windows and Linux** |
| CLI | `sxpe` | Scripts and terminals (Qt-free) |
| MCP | `sxpe_mcp` | Agents over stdio (Qt-free) |

![SXPE main window: resource grid and XML inspector](docs/images/sxpe-gui.png)

Typical work: open a package, browse resources by TGI and name, edit STBL / NMAP / XML / catalog / CAS / CLIP metadata, merge or un-merge packages, validate, compare, scan a Downloads folder, inspect Sims3Packs, import/export S3SA DLLs (never loaded in-process).

## Why it exists

s3pe is a 2010s Windows-only WinForms .NET editor. It is still useful, but it is a poor fit for 2026:

- Windows GUI only — no first-class Linux app
- No JSON CLI or agent protocol
- Large custom-content merges often ran out of memory or left huge temps
- Optional “Handlers” loaded arbitrary DLLs into the process

SXPE reimplements the **daily-driver featureset** on a modern stack (C++23, mmap I/O, virtualized lists, Qt 6 Widgets when present) so humans and agents share the same tools, and so large jobs **fail with a clear cap** instead of a mystery crash.

## SXPE vs s3pe

SXPE is **not** “s3pe 2.0” and does **not** contain s3pe/s3pi source. Familiar jobs have equivalents; the [user guide](docs/user-guide.md#if-you-used-s3pe-before) maps menus.

| | s3pe | SXPE |
| --- | --- | --- |
| Code | C# / WinForms | New C++23 (not a paste-fork) |
| GUI | Windows | Windows **and** Linux |
| Automation | Limited | JSON CLI + MCP, same bus as the GUI |
| Large merges | Often OOM | Caps, progress, cancel + rollback |
| Un-merge | No | SXPE merges only, via **SXMM** manifest |
| Plugins | DLL Handlers | **Permanently unsupported** (no `LoadLibrary` of random DLLs) |
| Neighborhood files | Easy to compact-corrupt | **Layout-locked** `.nhd` / `.world` / `.dbc` (in-place replace only) |

**Prefer SXPE** when you want Linux, scripts/agents, safer merges, or an editor that refuses dangerous neighborhood rebuilds. **s3pe** may still match a workflow that depends on a third-party Handler DLL — SXPE will not load those.

## Vision

Stay the editor you reach for on Sims 3 packages: game-loadable output, honest limits, and one catalog for GUI, CLI, and agents. Extra *Sims*-family games can plug in later as `GameProfile`s; **this version does not implement The Sims 4**. SXPE will not become a plugin host.

## Honest limits

- Sims 3 DBPF only. Unknown / other-game files are refused.
- No full 3D mesh or CLIP playback (inspector shows summaries and images, not a viewport).
- No Store/DRM Sims3Pack unpacking.
- `.nhd` / `.world` / `.dbc` are layout-locked — see [neighborhood layout](docs/neighborhood-layout.md).
- Third-party GUI plugins / DLL Handlers will not return ([#60](https://github.com/tofb15/sxpe/issues/60)).
- Help → Check for update **never downloads** a zip; it only compares versions. On a **private** GitHub repo the public API returns 404 unless you set a token — see the [user guide](docs/user-guide.md#check-for-update).

Keep backups. Compact/save rewrites packages; test in a copy first.

---

## Get SXPE

You do **not** need to compile if a Release asset matches your OS.

| You want | Do this |
| --- | --- |
| **Windows GUI + CLI + MCP** | Download `sxpe-0.7.0-windows-x64.zip` from **[v0.7.0](https://github.com/tofb15/sxpe/releases/tag/v0.7.0)**. Unzip somewhere writable. Keep DLLs and `platforms/` next to the exes. Double-click **`SXPE.bat`**. Same folder: `sxpe-cli.bat`, `sxpe-mcp.bat`. |
| **Linux CLI + MCP** | Download `sxpe-0.7.0-linux-x64-cli.tar.gz` from the same Release, extract, run `./sxpe` / `./sxpe_mcp`. |
| **Linux GUI** | Build with Qt 6.5+ Widgets ([building.md](docs/building.md)). A tarball from `scripts/package-linux.sh` can include `sxpe_gui` but **does not vendor Qt** — the machine that runs it still needs Qt. |

Drop one `.package` on the GUI to open it. Drop several to merge (SXPE writes an SXMM manifest so **un-merge works only for SXPE merges**).

## First steps

1. Open a **copy** of a package (never the file the game currently has open).
2. Merge a folder of CC: **Tools → Merge packages…** — [workflows](docs/workflows.md#merge-custom-content-into-one-package).
3. **Tools → Validate** before you share.

More recipes: [docs/workflows.md](docs/workflows.md). GUI reference: [docs/user-guide.md](docs/user-guide.md).

---

## Documentation

Index: **[docs/README.md](docs/README.md)**.

| Doc | Read it when |
| --- | --- |
| [User guide](docs/user-guide.md) | Using the GUI (menus, editors, shortcuts) |
| [Workflows](docs/workflows.md) | A specific mod task (merge, S3SA, scan, …) |
| [CLI & MCP](docs/cli-mcp.md) | Scripting or agents |
| [Building](docs/building.md) | Compiling, packaging, CI |
| [Format specs](docs/spec/README.md) | Codecs and the command catalog |
| [DESIGN.md](DESIGN.md) | Architecture |
| [CONTRIBUTING.md](CONTRIBUTING.md) | DCO, scope, what not to commit |

Do not commit game packages, custom content, or other copyrighted binaries.

---

## Build from source

Only if you are compiling. Releases already contain binaries.

These three CMake lines are **sequential steps**, not three ways to do the same thing:

1. **Configure** (once, or after `CMakeLists.txt` / dependency changes) — generates the build tree.
2. **Build** — compiles.
3. **Test** — optional. You can run `sxpe` / `sxpe_gui` without it.

**Windows (Visual Studio 2022/2026):** run `build.bat`. It finds MSVC, Ninja, and Qt if the kit is at `../qt/6.8.2/msvc2022_64`, then configure + build + test. `package.bat` writes a portable zip. Do not start with the CMake preset until `cl.exe` and Ninja are on `PATH` (Developer Command Prompt).

**Linux** (CMake 3.28+, Ninja, C++23 compiler):

```text
cmake --preset default                 # 1. configure → build/
cmake --build --preset default         # 2. compile
ctest --preset default --output-on-failure   # 3. optional tests
```

GUI needs Qt 6.5+ Widgets. Without Qt, CLI and MCP still build. Wayland, packaging, CI: **[docs/building.md](docs/building.md)**.

---

## Version

Project version is **0.7.0** (`CMakeLists.txt` `PROJECT_VERSION` and `vcpkg.json`). Notes: [docs/releases/v0.7.0.md](docs/releases/v0.7.0.md). Template for the next tag: [docs/releases/TEMPLATE.md](docs/releases/TEMPLATE.md).

---

## Licence

[GPL-3.0-or-later](LICENSE). Unofficial fan project. Electronic Arts owns The Sims 3. SXPE ships no EA game files. Use at your own risk; keep backups of packages you care about.
