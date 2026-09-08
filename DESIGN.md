# SXPE design

SXPE is a C++23 reimplementation of a Sims 3 package editor. Specs that codecs follow
live in [`docs/spec/`](docs/spec/README.md). This file is the in-repo architecture note
for **0.7.0+**; it is not the workspace-only planning tree.

## Shape

- **License:** GPL-3.0-or-later. Unofficial. Not EA. Not Peter L Jones’s s3pe.
- **Game:** The Sims 3 DBPF only. Extra games plug in as `GameProfile` later; do not add Sims 4 now.
- **Command bus:** GUI, CLI (`sxpe`), and MCP (`sxpe_mcp`) share one catalog (`docs/spec/catalog.md`). Envelope `{schemaVersion, ok, data|error}`. Surfaces are thin adapters over the bus — prefer bus-first design for new features.
- **GUI:** Qt 6 Widgets on **Windows and Linux** when Qt is present (`find_package`). CLI and MCP must not link Qt.
- **I/O:** mmap the package; copy unchanged compressed blobs; synthetic fixtures in `fixtures/synthetic/`.

## Dependencies

CMake `file(DOWNLOAD)` pins **nlohmann/json 3.11.3** and **CLI11 2.4.2** into the build `_vendor` directory. `vcpkg.json` lists the same two ports for optional vcpkg users. Tests use `tests/check.hpp`, not Catch2. spdlog and pugixml are not linked. Qt 6 Widgets (+ Network for update check) is optional; without it, CLI/MCP still build.

Do not copy s3pe/s3pi source. Formats come from `docs/spec` and the public URLs cited there.

## Agents

CLI stdout is data (JSON/JSONL). MCP tools match bus ids (`noun.verb` → `noun_verb`). List tools paginate (`limit`/`cursor`). No TTY assumptions. See command descriptions on `manifest`.

## Packaging (honest)

- **Current project version:** 0.7.0. Published GitHub Release **v0.7.0** includes the Windows portable zip.
- **Windows portable zip:** local via `package.bat` / `scripts/package.ps1` ([docs/building.md](docs/building.md)); also on GitHub Releases (`sxpe-<ver>-windows-x64.zip`).
- **Linux tarball:** local via `scripts/package-linux.sh` (CLI+MCP; optionally includes `sxpe_gui` when Qt was present — does **not** vendor Qt libs). Tag workflow attaches the CLI+MCP tarball.

## Extensibility (honest)

Third-party GUI plugins / Handlers are **permanently out of scope** (issue #60). SXPE does not
scan a `plugins/` folder, does not `LoadLibrary` arbitrary DLLs, and will not ship a thin plugin
SDK. First-party type handling stays compiled into the command bus. Optional external viewers
are user-configured process commands only (`PluginHost::run_user_command*`).
