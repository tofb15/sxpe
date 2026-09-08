# Building SXPE

Requires CMake 3.28+, a C++23 compiler, and (for the GUI) Qt 6.5+ Widgets.

## CLI / MCP (no Qt)

```text
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## GUI (`sxpe_gui`)

CMake enables `sxpe_gui` when `find_package(Qt6 Widgets)` succeeds. Without Qt, CLI and MCP still build.

### Windows

Use Visual Studio 2022/2026 (MSVC) or the repo helpers:

```text
build.bat
```

If Qt lives next to the repo as `../qt/6.8.2/msvc2022_64`, CMake picks it up. Otherwise set `CMAKE_PREFIX_PATH` to your Qt kit. Portable packaging stays Windows-only (`package.bat` / `scripts/package.ps1`); that path is unchanged by the Linux GUI port.

### Linux (Ubuntu 24.04 / Debian)

Install a Qt 6.5+ Widgets kit, then configure as usual. Options:

1. **Official Qt** (matches CI): install Qt 6.8.x desktop `gcc_64` and point CMake at it:

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

## CI

`.github/workflows/ci.yml` already runs **Linux CLI/MCP** (no Qt) and **Windows GUI smoke**.

### Linux GUI smoke job (recommended)

Add this job next to `linux-cli` / `windows-gui` when the pushing token has the GitHub `workflow` scope (OAuth apps without that scope cannot update `.github/workflows/*`):

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

### Proven in this environment (manual)

On a Linux host with Qt 6.8 Widgets (distro `qt6-base-dev` or official `gcc_64`), CMake 3.28+, Ninja, and g++:

```text
cmake --preset default
cmake --build --preset default --parallel
ctest --preset default --output-on-failure
# gui_smoke uses QT_QPA_PLATFORM=offscreen; opens fixtures/synthetic/single-blob.bin and lists resources
QT_QPA_PLATFORM=offscreen ./build/sxpe_gui --smoke fixtures/synthetic/single-blob.bin
```

All of the above were run successfully while implementing #31 (26/26 ctest, including `gui_smoke`).

## Releases / packaging

**Published on v0.6.0:** Linux CLI+MCP tarball only. Windows portable zip is **not** on Releases yet ([#54](https://github.com/tofb15/sxpe/issues/54)).

Windows portable zip for local use (GUI + CLI + MCP, Qt plugins, MSVC runtime):

```text
package.bat
```

Same as `scripts/package.ps1`. Output: `dist/sxpe/` and `dist/sxpe-<version>-windows-x64.zip`. Use `-SkipBuild` to package the current `build/` tree. The zip includes `SXPE.bat`, `sxpe-cli.bat`, and `sxpe-mcp.bat`.

### GitHub Actions release workflow (maintainer-only)

Preferred path: `.github/workflows/release.yml` (runs on `v*` tags, builds the portable zip via `package.ps1`).

If the pushing token lacks the GitHub `workflow` scope, OAuth cannot update files under `.github/workflows/`. In that case a paste-ready **maintainer-only** copy lives at [`docs/ci/release.yml`](ci/release.yml) (see [`docs/ci/README.md`](ci/README.md)) — add it in the GitHub UI or with a token that has `workflow` scope. Tagging and attaching the zip asset is a separate coordinator step.

