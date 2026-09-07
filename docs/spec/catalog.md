# Command catalog (M1 sketch)

Source of truth for GUI, CLI, and MCP. Architecture: [DESIGN.md](../../DESIGN.md).

CLI: `sxpe <noun> <verb>` · MCP: `noun_verb` · ids: dotted `noun.verb`.

## Envelope

```json
{ "schemaVersion": 1, "ok": true, "data": {} }
```

Error: `{ "schemaVersion": 1, "ok": false, "error": { "code", "message", "retryable", "side_effects" } }`

## P0 tools (implement with schemas in M3+)

| id | readOnly | destructive | notes |
| --- | --- | --- | --- |
| `package.new` | n | n | TS3 empty package |
| `package.open` | y | n | sniff; refuse unknown |
| `package.close` | n | n | |
| `package.save` | n | y | unmap then ReplaceFile |
| `package.saveAs` | n | y | |
| `package.info` | y | n | |
| `package.validate` | y | n | |
| `resource.list` | y | n | `limit` default 100, `cursor` |
| `resource.read` | y | n | metadata default; `maxBytes` |
| `resource.export` | y | n | write path; openWorld |
| `resource.importFiles` | n | y | `--force` / dryRun |
| `resource.delete` | n | y | |
| `stbl.get` / `stbl.set` | | | |
| `objk.get` | y | n | OBJK version / component IDs / data keys |
| `vpxy.get` | y | n | VPXY version / entries / bbox |
| `undo` / `redo` | n | y | Session mutation stack (50) |
| `s3sa.info` | y | n | Today: size / MZ offset / NMAP hint. Extend per [s3sa.md](s3sa.md) |
| `s3sa.exportDll` | y | n | Today: slice from first `MZ`. Fix: decrypt then write PE |
| `hash.fnv` | y | n | |
| `manifest` | y | n | tools/list |

GUI-only (no MCP): `preview.float`, `ui.selectAll`, `ui.palette`.

List never includes payloads. `resourceId`: `{ "type", "group", "instance", "ordinal" }`.

## Later (S3SA wrap — not implemented)

See [s3sa.md](s3sa.md). Do not `resource.add` a raw `.dll` as type `073FAA07`.

| id | readOnly | destructive | notes |
| --- | --- | --- | --- |
| `s3sa.importDll` | n | y | Wrap PE as community S3SA v1; replace or add |
| `s3sa.wrap` | y | n | Optional stateless wrap for tests |
| `package.makeScriptMod` | n | y | Optional later: S3SA + `_XML` `kInstantiator` + NMAP |
