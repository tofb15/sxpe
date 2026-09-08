# Contributing to SXPE

## License

Contributions are GPL-3.0-or-later. Add `Signed-off-by: Name <email>` to each commit (DCO).

## Original code

SXPE is a **reimplementation** in C++. Do not copy or mechanically translate source from s3pe, s3pi, or their helper EXEs. Do not vendor those binaries. Do not paste LLM output that was prompted with those trees.

## v1 scope

- Implement **The Sims 3** only, in **C++**.
- Do **not** add `GameId::Sims4`, `src/games_sims4`, or other-game codecs.
- Do **not** add C# / CLR / mixed-language UI.
- Keep `GameProfile` so a later profile can plug in.
- Refuse files no registered profile will sniff.
- CLI and MCP must not link Qt.

## Git hygiene

Do not commit:

- EA or custom-content packages (`.package`, `.dbc`, `.world`, `.nhd`)
- Personal filesystem paths, LAN IP addresses, or hosting account names
- Secrets, tokens, or credentials
- `s3pe.ico`, ExtList dumps, or GPL helper EXEs

## Tests

CI (`.github/workflows/ci.yml`) builds CLI/MCP on Linux without Qt and a Windows job with Qt that runs
`gui_smoke`. Linux Qt GUI steps and a paste-ready CI job live in `docs/building.md`. Synthetic fixtures
only. Optional local FullBuild/CC round-trip: `docs/testing.md` and `scripts/roundtrip.ps1`. Game/CC
bytes stay in `fixtures/local/` (gitignored) or the install tree.

## Docs

User-facing docs live under `docs/` (`user-guide.md`, `cli-mcp.md`, `workflows.md`, `building.md`). Format specs stay in `docs/spec/`. Keep README quick starts accurate when menus or packaging change.

## Commands

Non-UI features ship on the command bus, CLI, and MCP in the **same** change.

## Libraries

Do not reimplement JSON or CLI parsing. CMake downloads nlohmann/json **v3.11.3** and CLI11 **v2.4.2**
into the build `_vendor` dir (see `CMakeLists.txt`). `vcpkg.json` lists those same two ports for
optional vcpkg users. Tests use `tests/check.hpp`. Qt 6 Widgets (and Network, for Help → Check for update) is optional via `find_package`.
See [DESIGN.md](DESIGN.md).

## Version

`project(sxpe VERSION …)` in `CMakeLists.txt` is the single source of truth. CMake generates
`sxpe/version.hpp` (`SXPE_VERSION`) for CLI `--version`, GUI About, and MCP `serverInfo.version`.
Keep `vcpkg.json` `"version-string"` equal to that same `PROJECT_VERSION` when bumping.

## Safety

Untrusted package bytes: use `std::span` and size caps. Do not `memcpy` from the index without checking. Fuzz codecs when they exist.

## Plugins

Third-party GUI plugins and DLL Handlers are **permanently unsupported** (issue #60 / Option B).
Do **not** add a plugin SDK, sample DLL, `plugins/` scanner, or `LoadLibrary` of arbitrary
DLLs. Do not resurrect “Handlers / plugins” menu wording.

`Settings → Built-in handlers` lists compiled first-party type tags only (`handler.list`).
External hex / text / S3SA viewers under **Settings → External programs** are user-configured
processes (`{path}` substitution), not plugins.
