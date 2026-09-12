# REFS reference table `0x05ED1226`

Sources: SimsWiki / community world-lot reference store. Synthetic fixtures only.

## Layout (editor)

```
uint16 version
[uint8 thingy]          // when version >= 3 and parse selects the thingy candidate
uint32 entryCount
repeat entryCount:
  uint32 type
  uint32 group
  uint64 instance
  uint16|uint32 aux     // WORD preferred; DWORD if WORD body does not fit
uint32 indexCount
repeat indexCount:
  uint16 index
```

`refs.set` replaces `entries[]` and/or `indices[]`. Version, optional thingy, and aux width are preserved from the successful parse. Partial parses are refused for edit.

## Commands

| id | notes |
| --- | --- |
| `refs.get` | Entries + indices (+ Preview card) |
| `refs.set` | Optional `entries[]` (`type`/`group`/`instance`/`aux`) and `indices[]`; `dryRun` + undo |
| `resource.listRefs` | Outbound TGIs from this REFS / OBJK / VPXY / CASP |
| `resource.findRefs` | Inbound (who points at a target TGI) |

GUI: **Resource → Editors → Reference table…**. Find references dialog has Inbound / Outbound. CLI/MCP: `refs get|set`, `resource list-refs` / `resource_list-refs`.
