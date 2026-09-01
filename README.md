# SXPE

**SXPE** is a 2026 reimplementation of a Sims 3 package editor: inspect and edit DBPF `.package` files used by *The Sims 3*.

- **Humans:** Windows desktop app (Avalonia)
- **Agents / scripts:** JSON/JSONL CLI and MCP (stdio)
- **License:** [GPL-3.0-or-later](LICENSE)
- **v1 target:** The Sims 3 only

The core is built so additional *game profiles* can be added later (The Sims 4 is the likely next profile). **v1 does not implement other games.** Unknown formats are refused.

This project is unofficial. The Sims 3 is a trademark of Electronic Arts. SXPE is not affiliated with EA and is not Peter L Jones’s s3pe.

## Status

Foundation only. Package I/O, CLI, MCP, and GUI land in later work.

Requires **.NET 10** SDK:

```text
dotnet build
dotnet test
```

## Layout

- `src/Sxpe.Games.Abstractions` — game-profile interfaces
- `src/Sxpe.Games.Sims3` — Sims 3 profile (v1)
- `src/Sxpe.Core` — sessions, mmap, registry
- `tests/` — unit tests (synthetic fixtures only)

Do not commit game packages, custom-content, or other copyrighted binaries.
