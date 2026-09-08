# Building SXPE

Download a [Release](https://github.com/tofb15/sxpe/releases) if you only want to run SXPE. This page is **compile, package, and CI**.

Prerequisites: **CMake 3.28+**, a **C++23** compiler. GUI also needs **Qt 6.5+ Widgets**. Without Qt, CLI (`sxpe`) and MCP (`sxpe_mcp`) still build.

Windows and Linux both support GUI + CLI + MCP. Menus match; packaging helpers and a few lock/path details differ.

## The three CMake steps

These are **sequential**, not three spellings of the same command:

| Step | Command | When |
| --- | --- | --- |
| 1. Configure | `cmake --preset default` | Once per clone, and after `CMakeLists.txt` or toolchain changes. Writes `build/`. |
| 2. Build | `cmake --build --preset default` | Every time you want new binaries. |
| 3. Test | `ctest --preset default --output-on-failure` | Optional. You can run the binaries after step 2 without testing. |

The `default` preset is **Ninja** + **RelWithDebInfo** (`CMakePresets.json`). You need `ninja` and a compiler on `PATH`.

## Windows (MSVC)

Use **Visual Studio 2022 or 2026** with C++ tools. Do **not** start with the three lines above unless you already have a Developer Command Prompt (so `cl.exe` and Ninja are on `PATH`).

```text
build.bat
```

That is `scripts/build.ps1`: vswhere → `vcvars64` → configure → build → `ctest`. If Qt lives at `../qt/6.8.2/msvc2022_64` next to the repo, CMake picks it up; otherwise set `CMAKE_PREFIX_PATH` to your kit.

Portable zip (GUI + CLI + MCP, Qt plugins, MSVC CRT):

```text
package.bat
```

Same as `scripts/package.ps1`. Output: `dist/sxpe/` and `dist/sxpe-<version>-windows-x64.zip` (`SXPE.bat`, `sxpe-cli.bat`, `sxpe-mcp.bat`). `-SkipBuild` packages the current `build/` tree (including VS `RelWithDebInfo` / `Release` output dirs).

CLI-only zip (no Qt / no `sxpe_gui`):

```text
powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -NoGui
```

Alias: `-CliOnly`. Output: `dist/sxpe-<version>-windows-x64-cli.zip` (`sxpe-cli.bat`, `sxpe-mcp.bat`, MSVC CRT when available).

## Linux (Ubuntu 24.04 / Debian)

Install a C++ toolchain, Ninja, CMake 3.28+, then the three steps above.

GUI: Qt 6.5+ Widgets.

1. **Official Qt** (matches CI): desktop `gcc_64`, then `export CMAKE_PREFIX_PATH=/path/to/Qt/6.8.2/gcc_64` and configure/build/test.
2. **Distro packages** (when they are ≥ 6.5): `qt6-base-dev`, `qt6-base-dev-tools`. Ubuntu 24.04’s archive Qt can be older than 6.5; `find_package` then skips the GUI.

`gui_smoke` (`ctest`) runs `sxpe_gui --smoke fixtures/synthetic/single-blob.bin` with `QT_QPA_PLATFORM=offscreen`. No display server required.

### Wayland / X11

- Headless / CI: `QT_QPA_PLATFORM=offscreen`.
- If the interactive window fails under Wayland: `QT_QPA_PLATFORM=xcb ./build/sxpe_gui path/to/file.package`
- Rare fallback: `xvfb-run -a ./build/sxpe_gui --smoke fixtures/synthetic/single-blob.bin`
- Plugin search: official kits `<prefix>/plugins`; Debian multiarch often `<libdir>/qt6/plugins`. `gui_smoke` sets `QT_PLUGIN_PATH` when either exists.

## Linux tarball

```text
./scripts/package-linux.sh              # build unless --skip-build; stage dist/sxpe/
./scripts/package-linux.sh --skip-build
./scripts/package-linux.sh --no-gui     # CLI+MCP only
```

| Output | When |
| --- | --- |
| `dist/sxpe-<ver>-linux-x64-cli.tar.gz` | CLI + MCP (`--no-gui` or no `sxpe_gui`) |
| `dist/sxpe-<ver>-linux-x64.tar.gz` | CLI + MCP + `sxpe_gui` when that binary was built |

The GUI tarball **does not vendor Qt**. Launchers: `sxpe-cli.sh`, `sxpe-mcp.sh`, and `SXPE.sh` when GUI is included. Smoke: `./scripts/test-package-linux.sh` (skips if `build/sxpe` is missing).

Not AppImage/Flatpak.

## CI

Live `.github/workflows/ci.yml`:

| Job | What |
| --- | --- |
| `linux-cli` | Linux CLI/MCP (no Qt) on pushes and PRs to `dev` / `master` |
| `windows-gui` | Windows GUI + `gui_smoke` on pushes and PRs to `dev` / `master` |

Unique-topic branches do not run CI until they are a PR into `dev`/`master` or merged. Tag pushes use `release.yml` only (not a second `ci` run). A newer push on the same branch cancels an in-progress `ci` run.

**Linux GUI CI is not in `ci.yml` yet.** Paste-ready job: [`docs/ci/linux-gui.yml`](ci/linux-gui.yml) (canonical YAML - do not copy it into this page). Promoting it needs a GitHub token with **`workflow` scope**; OAuth apps without that scope cannot push `.github/workflows/*`.

Tag `v*` runs [`.github/workflows/release.yml`](../.github/workflows/release.yml). The four-artifact matrix (Windows GUI zip, Windows CLI zip, Linux CLI tarball, Linux GUI tarball — Qt not vendored; plus `workflow_dispatch`) lives in [`docs/ci/release.yml`](ci/release.yml) until a `workflow`-scoped push or UI paste updates the live file. See [`docs/ci/README.md`](ci/README.md).

## Releases

Version source: `CMakeLists.txt` `PROJECT_VERSION` (keep `vcpkg.json` in sync). Notes live under [`docs/releases/`](releases/TEMPLATE.md). Published assets: [GitHub Releases](https://github.com/tofb15/sxpe/releases).

| Platform | Local packaging | Artifact |
| --- | --- | --- |
| Windows GUI | `package.bat` / `scripts/package.ps1` | `sxpe-<ver>-windows-x64.zip` |
| Windows CLI | `scripts/package.ps1 -NoGui` | `sxpe-<ver>-windows-x64-cli.zip` |
| Linux CLI | `scripts/package-linux.sh --no-gui` | `sxpe-<ver>-linux-x64-cli.tar.gz` |
| Linux GUI | `scripts/package-linux.sh` (Qt present; **not** vendored) | `sxpe-<ver>-linux-x64.tar.gz` |
