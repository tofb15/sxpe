# CASP CAS part `0x034AEECB`

Sources: SimsWiki CAS Part / community layouts. Synthetic fixtures only.

## Layout assumptions (editor)

```
uint32 version
uint32 ref_off          // TGI absolute = ref_off + 8
uint32 presetCount
repeat presets: uint32 len; char16le[len]; uint32 trailing
7STRING Unicode BE name
float32 sortPriority
uint8  unused           // preserved
uint32 clothingType
uint32 typeFlags
uint32 ageGender        // age | (species|gender<<4)<<8 | handedness<<16
uint32 clothingCategory
… unknown mid bytes …
I64GT: uint8 count; repeat count × (uint64 instance, uint32 group, uint32 type)
… trailing …
```

`casp.set` rewrites name + fixed header fields and optionally replaces the I64GT table. Presets, the unused byte, unknown mid bytes, and post-TGI trailing bytes are preserved when possible. `ref_off` is rewritten to the new TGI absolute − 8.

Age/species/gender may be set via packed `ageGender` **or** individual `ageFlags` / `species` / `genderFlags` / `handedness`.

## Commands

| id | notes |
| --- | --- |
| `casp.get` | Clothing / age-gender + `tgis[]` (Preview card) |
| `casp.set` | Optional fields above + `tgis[]`; `dryRun` + undo |

GUI: **Resource → Editors → CAS part…**. CLI/MCP: `casp get|set` / `casp_get` / `casp_set`.
