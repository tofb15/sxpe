# Building SXPE

Requires CMake 3.28+, a C++23 compiler, and (for the GUI) Qt 6.5+ Widgets.

**Supported targets:** Windows and Linux for **GUI + CLI + MCP**. Menus and workflows are the same on both; only packaging helpers and a few OS lock/path details differ. This file is the single source of truth for prereqs, CMake, packaging, Wayland/X11/offscreen, and paste-ready CI.

## CLI / MCP (no Qt)

```text
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## GUI (`sxpe_gui`)

CMake enables `sxpe_gui` when `find_package(Qt6 Widgets)` succeeds. Without Qt, CLI and MCP still build. The Linux Qt GUI is a **supported** target (same package-edit menus/workflows as Windows: open/save, merge/unmerge, editors, validate, compare, scan, Sims3Pack, etc.).

### Windows

Use Visual Studio 2022/2026 (MSVC) or the repo helpers:

```text
build.bat
```

If Qt lives next to the repo as `../qt/6.8.2/msvc2022_64`, CMake picks it up. Otherwise set `CMAKE_PREFIX_PATH` to your Qt kit.

Portable zip (GUI + CLI + MCP, Qt plugins, MSVC runtime):

```text
package.bat
```

Same as `scripts/package.ps1`. Output: `dist/sxpe/` and `dist/sxpe-<version>-windows-x64.zip`. Use `-SkipBuild` to package the current `build/` tree. The zip includes `SXPE.bat`, `sxpe-cli.bat`, and `sxpe-mcp.bat`.

### Linux (Ubuntu 24.04 / Debian)

Install a Qt 6.5+ Widgets kit, then configure as usual. Options:

1. **Official Qt** (matches CI sketch): install Qt 6.8.x desktop `gcc_64` and point CMake at it:

   ```text
   export CMAKE_PREFIX_PATH=/path/to/Qt/6.8.2/gcc_64
   cmake --preset default
   cmake --build --preset default
   ctest --preset default --output-on-failure
   ```

2. **Distro packages** (when they are ≥ 6.5): e.g. `qt6-base-dev`, `qt6-base-dev-tools`, and a C++ toolchain. Ubuntu 24.04’s archive Qt can be older than 6.5; use official Qt or a newer distro if `find_package` skips the GUI.

`gui_smoke` runs `sxpe_gui --smoke fixtures/synthetic/single-blob.bin` with `QT_QPA_PLATFORM=offscreen` (opens the synthetic package and lists/filters resources). No display server required.

### Wayland / X11 caveats

- **Headless / CI:** prefer `QT_QPA_PLATFORM=offscreen` (what `gui_smoke` sets). This does not need X11 or Wayland.
- **Interactive desktop:** Qt usually picks Wayland or X11 from the session. If the window fails to show, input is broken, or decorations look wrong under Wayland, force X11/XCB:

  ```text
  QT_QPA_PLATFORM=xcb ./build/sxpe_gui path/to/file.package
  ```

- **Xvfb fallback** (rare; only if offscreen plugins are missing):

  ```text
  xvfb-run -a ./build/sxpe_gui --smoke fixtures/synthetic/single-blob.bin
  ```

- Plugin search: official kits use `<prefix>/plugins`; Debian/Ubuntu multiarch often uses `<libdir>/qt6/plugins`. CMake’s `gui_smoke` sets `QT_PLUGIN_PATH` when either layout is present.

## Linux packaging (non-builders / local share)

Preferred approach: a **documented tarball** via `scripts/package-linux.sh` (not AppImage/Flatpak for v1 of this parity work). Windows keeps `package.ps1`.

```text
./scripts/package-linux.sh              # build (unless --skip-build), stage dist/sxpe/
./scripts/package-linux.sh --skip-build # package current build/
./scripts/package-linux.sh --no-gui     # force CLI+MCP only
```

| Output | When |
| --- | --- |
| `dist/sxpe-<ver>-linux-x64-cli.tar.gz` | CLI + MCP only (`--no-gui` or no `sxpe_gui`) |
| `dist/sxpe-<ver>-linux-x64.tar.gz` | CLI + MCP + `sxpe_gui` when the GUI binary was built |

Honest limits of the Linux tarball:

- **Does not vendor Qt shared libraries.** A tarball that includes `sxpe_gui` still needs Qt 6.5+ Widgets+Network (distro or official kit) on the machine that runs it.
- Launchers in the archive: `sxpe-cli.sh`, `sxpe-mcp.sh`, and `SXPE.sh` when GUI is included.
- Smoke the packager locally: `./scripts/test-package-linux.sh` (skips cleanly if `build/sxpe` is missing).

## Platform limits (honesty)

- **Third-party GUI plugins / DLL Handlers** are **permanently unsupported** (issue #60). No plugin SDK, no `plugins/` scanner, no `LoadLibrary` of arbitrary DLLs on any OS.
- **External programs** (`Settings → External programs`) are user-configured process commands with `{path}` substitution — not plugins.
  - Windows examples: `C:\Tools\hex.exe {path}`, `notepad {path}`, `ilspy {path}` / `dnSpy {path}`
  - Linux examples: `ghex {path}` / `okteta {path}`, `xdg-open {path}` / `nano {path}`, `ilspycmd {path}`
- File locking: Linux uses `flock`; Windows uses restrictive `CreateFile` share modes (see [workflows.md](workflows.md#edit-packages-the-game-might-have-open)).

## CI

Live `.github/workflows/ci.yml` currently runs:

| Job | What |
| --- | --- |
| `linux-cli` | Linux CLI/MCP (no Qt) on every push/PR |
| `windows-gui` | Windows GUI + `gui_smoke` on every push/PR |

**Linux GUI CI is not live in-tree yet.** Promoting the paste-ready `linux-gui` job into `.github/workflows/ci.yml` requires a token with the GitHub **`workflow` scope**. OAuth apps without that scope **cannot** push updates under `.github/workflows/*` (same residual blocker as shipping `release.yml` — see [#54](https://github.com/tofb15/sxpe/issues/54)).

### Linux GUI smoke job (paste-ready)

Canonical sketch (kept in sync here and as a standalone file):

- Inline below (copy under `jobs:` in `ci.yml`)
- Maintainer file: [`docs/ci/linux-gui.yml`](ci/linux-gui.yml)

```yaml
  linux-gui:
    name: Linux GUI smoke
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v6

      - uses: jurplel/install-qt-action@v4.3.1
        with:
          version: "6.8.2"
          host: linux
          target: desktop
          arch: linux_gcc_64
          cache: true
          archives: qtbase
          install-deps: true

      - name: Install ninja and CMake 3.28
        run: |
          sudo apt-get update
          sudo apt-get install -y ninja-build g++ wget
          wget -q https://github.com/Kitware/CMake/releases/download/v3.28.6/cmake-3.28.6-linux-x86_64.sh
          sudo sh cmake-3.28.6-linux-x86_64.sh --skip-license --prefix=/usr/local
          cmake --version
          g++ --version

      - name: Configure
        run: cmake --preset default

      - name: Build
        run: cmake --build --preset default --parallel

      - name: Test
        run: ctest --preset default --output-on-failure
```

Until that job is promoted, every PR still gets **Linux CLI/MCP** + **Windows GUI smoke**; Linux GUI is validated locally / offscreen as below.

### Proven in this environment (manual)

On a Linux host with Qt 6.8 Widgets (distro `qt6-base-dev` or official `gcc_64`), CMake 3.28+, Ninja, and g++:

```text
cmake --preset default
cmake --build --preset default --parallel
ctest --preset default --output-on-failure
# gui_smoke uses QT_QPA_PLATFORM=offscreen; opens fixtures/synthetic/single-blob.bin and lists resources
QT_QPA_PLATFORM=offscreen ./build/sxpe_gui --smoke fixtures/synthetic/single-blob.bin
./scripts/package-linux.sh --skip-build
./scripts/test-package-linux.sh
```

## Releases / packaging

**Published on v0.6.0:** Linux CLI+MCP tarball only. Windows portable zip is **not** on Releases yet ([#54](https://github.com/tofb15/sxpe/issues/54)).

| Platform | Local packaging | Typical artifact name |
| --- | --- | --- |
| Windows | `package.bat` / `scripts/package.ps1` | `sxpe-<ver>-windows-x64.zip` |
| Linux | `scripts/package-linux.sh` | `sxpe-<ver>-linux-x64-cli.tar.gz` or `sxpe-<ver>-linux-x64.tar.gz` |

Release notes should list **both** platform artifacts (template: [`docs/releases/TEMPLATE.md`](releases/TEMPLATE.md)).

### GitHub Actions release workflow (maintainer-only)

Preferred path: `.github/workflows/release.yml` (runs on `v*` tags).

If the pushing token lacks the GitHub `workflow` scope, OAuth cannot update files under `.github/workflows/`. In that case paste-ready **maintainer-only** copies live under [`docs/ci/`](ci/README.md):

- [`docs/ci/release.yml`](ci/release.yml) — Windows portable zip on `v*` tags
- [`docs/ci/linux-gui.yml`](ci/linux-gui.yml) — Linux GUI smoke job for `ci.yml`

Tagging and attaching assets is a separate coordinator step. Optional future: extend release automation to also run `scripts/package-linux.sh` and upload the Linux tarball alongside the Windows zip.
