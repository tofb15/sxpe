# SXPE CLI and MCP (agents)

GUI, CLI, and MCP share one **command bus**. Prefer this page plus [docs/spec/catalog.md](spec/catalog.md) over scraping the GUI.

**Related:** [workflows](workflows.md) · [user guide](user-guide.md) · [DESIGN.md](../DESIGN.md)

## Binaries

| Binary | Role | Qt? |
| --- | --- | --- |
| `sxpe` | CLI — `noun verb` | No |
| `sxpe_mcp` | MCP stdio server | No |
| `sxpe_gui` | Desktop UI | Yes (Widgets + Network) |

A local Windows portable zip (`package.bat`) ships `sxpe-cli.bat` and `sxpe-mcp.bat` next to the GUI. v0.6.0 Releases publish a Linux CLI+MCP tarball instead.

## Bus model

- **Ids:** dotted `noun.verb` (e.g. `resource.list`, `package.validate`).
- **CLI:** `sxpe <noun> <verb> [flags]`.
- **MCP:** tool name `noun_verb` (same id with `_`).
- **Sessions:** many commands take `sessionId` after `package.open`. One-shot CLI often uses `--package PATH` (open → run → save if writing → close).

Discover tools:

```text
sxpe --help
sxpe help
sxpe help resource
sxpe resource rename --help
sxpe manifest --format json
```

## JSON envelopes

Default CLI/MCP result shape:

```json
{ "schemaVersion": 1, "ok": true, "data": { } }
```

Errors:

```json
{
  "schemaVersion": 1,
  "ok": false,
  "error": {
    "code": "…",
    "message": "…",
    "retryable": false,
    "side_effects": false
  }
}
```

Human views: `--format text` or `--format table`. Agents should keep `--format json` (default).

Stdout is **data**. Do not assume a TTY. List endpoints paginate with `limit` / `cursor`; lists do not include payloads.

**resourceId** object: `{ "type", "group", "instance", "ordinal" }` (numbers; hex strings accepted on CLI where documented).

## Key commands

| id | Purpose |
| --- | --- |
| `package.new` / `open` / `close` / `save` / `saveAs` / `info` | Lifecycle |
| `package.validate` | Hygiene / DIR / layout-lock summary |
| `package.diff` | Compare two packages |
| `package.unmerge` | Reverse SXPE SXMM merge |
| `folder.scan` | Read-only tree hygiene |
| `sims3pack.list` / `extract` | TS3Pack inspect |
| `resource.list` / `read` / `export` / `importFiles` / `delete` / `setFlags` / `rename` | Resources |
| `nmap.*` / `stbl.*` / `xml.*` | Structured editors |
| `s3sa.info` / `exportDll` / `importDll` / `view` | Script assembly wrapper (never LoadLibrary) |
| `hash.fnv` | FNV helpers |
| `undo` / `redo` | Session stack |

Full table and flags: [spec/catalog.md](spec/catalog.md). Codec bytes: [spec/](spec/README.md).

## Examples

```text
sxpe --version
sxpe resource list --package path/to/file.package --format json --limit 50
sxpe package info --package path/to/file.package
sxpe resource rename --package path/to/file.package \
  --type 0x0333406C --group 0 --instance 0x1 --name NRaas.NoCD --force
sxpe folder scan --path path/to/Downloads --format json
sxpe sims3pack list --path mod.sims3pack
sxpe sims3pack extract --path mod.sims3pack --out-dir out --index 0 --force
```

MCP: start `sxpe_mcp`, then call tools with the same arguments as bus JSON (see MCP `tools/list` / `manifest`).

## Safety for agents

- Synthetic fixtures only in CI (`fixtures/synthetic/`). Do not commit EA/CC bytes.
- Respect `layoutLocked` on neighborhood / world / DBC paths.
- Destructive ops need explicit flags (`--force`) where required; prefer dry-run when offered.
- `s3sa.*` decrypts/wraps PE bytes for export/import/view — never execute package code in-process.
