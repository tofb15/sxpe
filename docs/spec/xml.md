# XML / ITUN editor

Types: `_XML` (`0x0333406C`), `ITUN` (`0x03B33DDF`). Payloads that sniff as XML (`<?xml` / leading `<`, including UTF-16 BOM) are also editable.

## Commands

| id | CLI | MCP | notes |
| --- | --- | --- | --- |
| `xml.get` | `sxpe xml get` | `xml_get` | Decode to UTF-8 text; returns `encoding` |
| `xml.set` | `sxpe xml set` | `xml_set` | Write UTF-8 `text`; optional `encoding`; `dryRun`; undo |

GUI: **Resource → Editors → XML…** (plain text dialog). Same bus commands.

Encoding values: `utf-8`, `utf-8-bom`, `utf-16le`, `utf-16be`. On `xml.set`, default is the sniff of the existing payload so UTF-16LE mods round-trip.

## Cap

Uncompressed body (read or write) must be ≤ `kMaxXmlEditorBytes` (**4 MiB**). Oversize → `cap_exceeded` with a clear message; use `resource.export` / `resource.replace` for huge blobs.

## Preview vs editor

Preview still pretty-prints ~4 KiB (`docs/preview.md`). This editor is the round-trip path.
