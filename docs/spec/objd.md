# OBJD catalog object `0x319E4F1D`

Sources: SimsWiki Catalog Resource / community layouts. SXPE uses **synthetic fixtures only** (no EA samples).

## Layout assumptions (editor)

Little-endian:

```
uint32 version
uint32 tgi_off          // bytes from end of this 12-byte header to TGI table
uint32 tgi_size
MaterialList            // skipped via each preset's size offset (same as Preview)
[7BITSTR instanceName]  // when version >= 0x16
Common:
  uint32 commonVersion
  uint64 nameGuid
  uint64 descGuid
  7BITSTR internalName
  7BITSTR internalDesc
  float32 price
  float32 niceness      // preserved, not edited
  float32 crapiness     // preserved, not edited
  uint8  statusFlags    // preserved, not edited
  uint64 thumbIid       // 0 = match catalog IID (raw field still reported)
… unknown object-specific fields …
TGI / key table at 12 + tgi_off
… trailing …
```

`objd.set` rewrites InstanceName + Common editable fields only. Materials, niceness/crap/status, unknown mid bytes, and the TGI block are **byte-preserved**. When string lengths change, `tgi_off` is adjusted by the size delta.

## Commands

| id | notes |
| --- | --- |
| `objd.get` | Same card fields as Preview |
| `objd.set` | Optional `nameGuid`, `descGuid`, `internalName`, `internalDesc`, `price`, `thumbIid`, `instanceName`; `dryRun` + undo |

GUI: **Resource → Editors → Catalog object…** (OBJD selected). CLI/MCP: `objd get|set` / `objd_get` / `objd_set`.
