# SXPE design (stub)

SXPE is a C++23 reimplementation of a Sims 3 package editor. Specs that codecs follow
live in [`docs/spec/`](docs/spec/README.md). This file is the in-repo architecture note;
it is not the workspace-only planning tree.

## Shape

- **License:** GPL-3.0-or-later. Unofficial. Not EA. Not Peter L Jones’s s3pe.
- **v1 game:** The Sims 3 DBPF only. Extra games plug in as `GameProfile` later; do not add Sims 4 now.
- **Command bus:** GUI, CLI (`sxpe`), and MCP share one catalog (`docs/spec/catalog.md`). Envelope `{schemaVersion, ok, data|error}`.
- **GUI:** Windows Qt 6 Widgets. CLI and MCP must not link Qt.
- **I/O:** mmap the package; copy unchanged compressed blobs; synthetic fixtures in `fixtures/synthetic/`.

## Dependencies

CMake `file(DOWNLOAD)` pins **nlohmann/json 3.11.3** and **CLI11 2.4.2** into the build `_vendor` directory. `vcpkg.json` lists the same two ports for optional vcpkg users. Tests use `tests/check.hpp`, not Catch2. spdlog and pugixml are not linked. Qt 6 Widgets is optional (`find_package`); without it, CLI/MCP still build.

Do not copy s3pe/s3pi source. Formats come from `docs/spec` and the public URLs cited there.

## Agents

CLI stdout is data (JSON/JSONL). MCP tools match bus ids (`noun.verb` → `noun_verb`). List tools paginate (`limit`/`cursor`). No TTY assumptions. See command descriptions on `manifest`.
