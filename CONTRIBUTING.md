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

CI uses **synthetic** fixtures. Optional local tests against a user-owned game directory stay gitignored.

## Commands

Non-UI features ship on the command bus, CLI, and MCP in the **same** change.

## Safety

Untrusted package bytes: use `std::span` and size caps. Do not `memcpy` from the index without checking. Fuzz codecs when they exist.
