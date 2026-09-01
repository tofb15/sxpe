# Contributing to SXPE

## License

Contributions are GPL-3.0-or-later. Add `Signed-off-by: Name <email>` to each commit (DCO).

## Original code

SXPE is a **reimplementation**. Do not copy or mechanically translate source from s3pe, s3pi, or their helper EXEs. Do not vendor those binaries. Do not paste LLM output that was prompted with those trees.

Read public format documentation and `docs/spec/` (when present). A local copy of original s3pe may be used as a **feature checklist** or **black-box oracle** only.

## v1 scope

- Implement **The Sims 3** only.
- Do **not** add `GameId.Sims4`, `Sxpe.Games.Sims4`, or other-game codecs.
- Keep `IGameProfile` / codec interfaces so a later profile can plug in.
- Refuse files no registered profile will sniff.

## Git hygiene

Do not commit:

- EA or custom-content packages (`.package`, `.dbc`, `.world`, `.nhd`)
- Personal filesystem paths, LAN IP addresses, or hosting account names
- Secrets, tokens, or credentials
- `s3pe.ico`, ExtList dumps, or GPL helper EXEs

## Tests

CI uses **synthetic** fixtures you authored. Optional local tests against a user-owned game directory must stay gitignored and must not embed machine-specific paths in committed files.

## Commands

Non-UI features ship on the command bus, CLI, and MCP in the **same** change.
