# Command catalog

Source of truth for GUI, CLI, and MCP. Architecture: [DESIGN.md](../../DESIGN.md).

CLI: `sxpe <noun> <verb>` (kebab verbs: `find-refs`) · MCP: `noun_kebab-verb` (`resource_find-refs`) · ids: dotted `noun.verb`.

## Envelope

```json
{ "schemaVersion": 1, "ok": true, "data": {} }
```

Error: `{ "schemaVersion": 1, "ok": false, "error": { "code", "message", "retryable", "side_effects" } }`

## Commands

| id | readOnly | destructive | notes |
| --- | --- | --- | --- |
| `package.new` | n | n | TS3 empty package |
| `package.open` | y | n | mmap+index; `forceWritable`; auto RO ≥256 MiB |
| `package.close` | n | n | |
| `package.save` | n | y | unmap then ReplaceFile |
| `package.saveAs` | n | y | |
| `package.info` | y | n | Includes `layoutLocked`, `pathKind` (nhd/world/dbc/package) |
| `package.validate` | y | n | Returns `layoutLocked`, `pathKind`, `conflictHotspots[]`, `summary[]` (layout lock + conflict hotspots) |
| `package.diff` | y | n | Compare two packages by TGI+ordinal; SHA-256 of uncompressed payload; `summary[]` |
| `package.unmerge` | n | y | Recreate sources from SXMM; refuse if missing |
| `folder.scan` | y | n | Read-only recursive `*.package` hygiene; empty/corrupt/wrong-game + duplicate TGI sample; `summary[]` |
| `sims3pack.info` / `sims3pack.list` / `sims3pack.extract` / `sims3pack.pack` | y / y / y* / n | n | TS3Pack inspect + limited pack (`openWorld`); no Store/DRM |
| `resource.list` | y | n | `limit` default 100, max 500, `cursor`; `returned` + `total`; CLI `--all` |
| `resource.read` | y | n | metadata default; `includePayload` base64 cap 1 MiB; prefer `resource.export` |
| `resource.findRefs` | y | n | Inbound TGI refs (REFS/OBJK/VPXY/CASP + optional byteScan) |
| `resource.export` | y | n | write path; openWorld |
| `resource.importFiles` | n | y | `--force` / dryRun |
| `resource.importPackage` | n | y | Merge/import; caps + progress + checkpoint + cancel/rollback; `dirPolicy`; `leftoverManifestPolicy` strip/keep/warn; `duplicateTgiPolicy` force/skip/fail |
| `resource.delete` | n | y | |
| `resource.setFlags` | n | y | `deleted` is session-only; save omits the row |
| `stbl.get` / `stbl.set` | | | |
| `nmap.get` / `nmap.list` / `nmap.set` / `nmap.delete` / `nmap.replace` | y / y / n / n / n | n / n / y / y / y | Name map; replace = one undo batch; set/replace/delete optional `compress` (default false) |
| `xml.get` / `xml.set` | y / n | n / y | `_XML`/`ITUN`; UTF-8/UTF-16 sniff; cap 4 MiB |
| `objk.get` | y | n | OBJK version / component IDs / data keys |
| `vpxy.get` | y | n | VPXY version / entries / bbox |
| `objd.get` / `objd.set` | y / n | n / y | OBJD Common fields; set preserves trailing + TGI off |
| `casp.get` / `casp.set` | y / n | n / y | CASP clothing/flags/TGIs; set preserves presets/mid |
| `refs.get` / `refs.set` | y / n | n / y | REFS TGI+aux table + indices; dryRun + undo |
| `resource.listRefs` | y | n | Outbound TGIs from REFS/OBJK/VPXY/CASP |
| `clip.info` / `clip.set` / `clip.exportAs` / `clip.exportAsBatch` | y / n / n / n | n / y / n* / n* | CLIP metadata + exportAs helpers (no playback); *adds copies |
| `rcol.summary` | y | n | MODL/MLOD/GEOM/MATD chunks, mesh counts, MATD textures |
| `rcol.replaceChunk` | n | y | Replace one RCOL chunk by index; backupPath + undo |
| `undo` / `redo` | n | y | Session mutation stack (50) |
| `s3sa.info` | y | n | Wrapper + decrypted PE facts. Never LoadLibrary |
| `s3sa.exportDll` | y | n | Decrypt then write PE |
| `s3sa.importDll` | n | y | Wrap PE as community S3SA v1; replace or add |
| `s3sa.wrap` | y | n | Stateless wrap |
| `s3sa.view` | y | n | Export PE for external viewer; require `path` or `viewer`/`keepTemp`. Optional `viewer` `{path}` spawn. Never LoadLibrary. GUI: `ext/s3sa` + `keepTemp` |
| `hash.fnv` | y | n | |
| `app.checkUpdate` | y | n | GitHub `/latest` + releases list (prefer newer incl. Beta); never downloads. Token via env/`gh`. |
| `manifest` | y | n | tools/list |

GUI-only (no MCP): `preview.float`, `ui.selectAll`, `ui.palette`.

List never includes payloads. `resourceId`: `{ "type", "group", "instance", "ordinal" }`.

## Later / optional

See [s3sa.md](s3sa.md). Do not `resource.add` a raw `.dll` as type `073FAA07`.

| id | readOnly | destructive | notes |
| --- | --- | --- | --- |
| `package.makeScriptMod` | n | y | Optional later: S3SA + `_XML` `kInstantiator` + NMAP |
