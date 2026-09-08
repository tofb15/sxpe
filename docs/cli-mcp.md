# SXPE CLI and MCP (agents)

GUI, CLI, and MCP share one **command bus**. Prefer this page plus [spec/catalog.md](spec/catalog.md) over scraping the GUI.

What SXPE is: [README](../README.md). Recipes: [workflows](workflows.md). GUI: [user-guide](user-guide.md). Architecture: [DESIGN.md](../DESIGN.md).

## Binaries

| Binary | Role | Qt? |
| --- | --- | --- |
| `sxpe` | CLI - `noun verb` | No |
| `sxpe_mcp` | MCP stdio server | No |
| `sxpe_gui` | Desktop UI | Yes (Widgets) |

**Cross-platform:** the same `sxpe` / `sxpe_mcp` binaries and bus commands run on Windows and Linux (Qt-free). Examples on this page use POSIX-style paths; on Windows pass normal Windows paths to `--package` / `--path`.

- **Windows portable zip** ships `sxpe-cli.bat` and `sxpe-mcp.bat` next to the GUI.
- **Linux CLI tarball** ships `sxpe` / `sxpe_mcp` plus `sxpe-cli.sh` / `sxpe-mcp.sh`. Both are on [v0.7.0](https://github.com/tofb15/sxpe/releases/tag/v0.7.0). Build: [building.md](building.md).

## Bus model

- **Ids:** dotted `noun.verb` (e.g. `resource.list`, `package.validate`).
- **CLI:** `sxpe <noun> <verb> [flags]`.
- **MCP:** tool name `noun_verb` (same id with `_`).
- **Sessions:** many commands take `sessionId` after `package.open`. One-shot CLI often uses `--package PATH` (open → run → save if writing → close).
- **Huge packages:** `package.open` with `--writable` demotes to read-only at ≥256 MiB (`openedReadOnlyDueToSize`) unless `--force-writable`. Preview/list stay index-first; see [testing.md](testing.md#huge-package-open-performance-issue-65).

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

`--progress` (merge/import/scan): JSON progress events on **stderr**; stdout stays the final envelope. **Ctrl+C / SIGINT** cooperatively cancels merge/import/scan and rolls the session back (`error.message=cancelled`, `side_effects=none`).

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
| `sims3pack.list` / `extract` / `pack` | TS3Pack inspect + limited pack |
| `resource.list` / `read` / `export` / `importFiles` / `importPackage` / `delete` / `setFlags` / `rename` | Resources; merge via `importPackage` |
| `nmap.*` / `stbl.*` / `xml.*` / `objd.*` / `casp.*` / `refs.*` | Structured editors |
| `rcol.summary` / `rcol.replaceChunk` | Mesh/MATD summary; safe chunk replace |
| `resource.findRefs` / `resource.listRefs` | Inbound / outbound TGI references |
| `s3sa.info` / `exportDll` / `importDll` / `view` | Script assembly wrapper (never LoadLibrary) |
| `hash.fnv` | FNV helpers |
| `app.checkUpdate` | Compare this build to GitHub Releases (falls back to list if `/latest` 404s on Beta prereleases); never downloads |
| `undo` / `redo` | Session stack |

Full table and flags: [spec/catalog.md](spec/catalog.md). Codec bytes: [spec/](spec/README.md).

## Merge (bus)

Full recipe: [workflows.md](workflows.md#merge-custom-content-into-one-package) (same steps as **Tools → Merge packages…**).

Flags: `--max-packages`, `--max-total-bytes`, `--max-resources`; optional `--checkpoint-path` / `--checkpoint-between-packages`. `leftoverManifestPolicy` defaults to `strip` (Sims3Pack leftover `0x73E93EEB` instance 0). `duplicateTgiPolicy` is `force` | `skip` | `fail`. `resource.importDbc` is the DBC-equivalent of the same import. Cancel: GUI **Cancel** / CLI **Ctrl+C**.

## Examples

```text
sxpe --version
sxpe app checkUpdate --format text
sxpe resource list --package path/to/file.package --format json --limit 50
sxpe package info --package path/to/file.package
sxpe resource rename --package path/to/file.package \
  --type 0x0333406C --group 0 --instance 0x1 --name NRaas.NoCD --force
sxpe folder scan --path path/to/Downloads --format json
sxpe sims3pack list --path mod.sims3pack
sxpe sims3pack extract --path mod.sims3pack --out-dir out --index 0 --force
sxpe sims3pack pack --source-dir pkgs --path out.sims3pack --display-name MyMod --force
```

MCP: start `sxpe_mcp`, then call tools with the same arguments as bus JSON (see MCP `tools/list` / `manifest`).

## Safety for agents

- Synthetic fixtures only in CI (`fixtures/synthetic/`). Do not commit EA/CC bytes.
- Respect `layoutLocked` on neighborhood / world / DBC paths.
- Destructive ops need explicit flags (`--force`) where required; prefer dry-run when offered.
- `s3sa.*` decrypts/wraps PE bytes for export/import/view - never execute package code in-process.

## CLIP metadata

- `sxpe clip info` -- full summary (no playback).
- `sxpe clip set --anim-name ... --source-file ... --actor-name ...` -- safe fields only ([clip.md](spec/clip.md)).
- `sxpe clip export-as --name ...` / `sxpe clip export-as-batch --items '[...]'` -- copy with `fnv64_clip` instance.
