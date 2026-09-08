# Preview Wave 2 — layout notes

Issue #20. Parsers are **summary-only** for the inspector Preview tab. No mesh GL, CLIP playback, or full catalog editors.

Sources: SimsWiki Catalog Resource / `0x319E4F1D` / `0x034AEECB` / `0x6B20C4F3` / RCOL / GEOM — community layouts, not EA-shipped samples.

## OBJD (`objd.get`)

- Skips the Material List using each preset’s size `offset` (from that field through its TGI list), then reads optional `InstanceName` when `version >= 0x16`, then **Common**: name/desc GUIDs, internal 7-bit ASCII strings, price, thumb IID.
- Synthetic fixtures use empty materials. Odd community presets may set `partial` or fail closed.
- Thumb IID `0` means “match catalog IID” per SimsWiki; we still report the raw field.

## CASP (`casp.get`)

- Best-effort public layout: skip UTF-16LE preset blobs, then 7-string Unicode BE name, sort priority, clothing type, type flags, packed age/species/gender/handedness DWORD, clothing category.
- Age/species/gender decode follows SimsWiki CAS Part Flags. Unknown clothing types show the numeric id only.
- TGI refs: I64GT key table at `offset+8` (BYTE count, then instance/group/type). Used by `resource.findRefs` (`casp.tgi`).
- Later CASP versions may add fields we do not read; Preview labels partial parses honestly.

## CLIP (`clip.info` / `clip.set`)

- Main-header offsets are relative to each field (SimsWiki). Duration = `frameDuration * frameCount`.
- Reports anim/source/actor names when present and up to 64 joint-rule hashes (track hashes). No frame decode or playback.
- Safe edits: `animName`, `sourceFile`, `actorName`, `trackHashes[{index,hash}]` via `clip.set`. See [clip.md](clip.md).
- `fnv64_clip` for `clip.exportAs` / `clip.exportAsBatch` / hashing.

## MODL / MLOD / GEOM / MATD (`rcol.summary`)

- RCOL header scan: internal chunk TGIs + absolute chunk ranges + external TGI table. Tags known chunk types (MODL/MLOD/GEOM/MATD/VBUF/IBUF/…).
- MLOD: sum `VertexCount` / `PrimitiveCount` across groups when the subset size walk succeeds.
- GEOM: bare `GEOM` fourcc or RCOL chunk — vertex count and face count (`NumFacePoints / 3`) when the format walk succeeds.
- MATD: shader name (when hash is known) + texture TGIs from MTNF/MTRL type-code 4 params when parseable. See [rcol.md](rcol.md).
- Caps oversized chunk tables (`4096`). Not a mesh viewer. Safe chunk replace: `rcol.replaceChunk` (backupPath + session undo).

## OBJK / VPXY

- Existing `objk.get` / `vpxy.get`; Preview now shows visibility, TGI count, bbox, and entry rows clearly (Graph still available).

## Commands

| id | notes |
| --- | --- |
| `objd.get` / `objd.set` | Common header card; typed editor |
| `casp.get` / `casp.set` | Clothing / age-gender / TGIs; typed editor |
| `clip.info` / `clip.set` | Duration + tracks; safe metadata |
| `clip.exportAs` / `clip.exportAsBatch` | Copy with new fnv64_clip instance |
| `rcol.summary` | Chunk / LOD / v-f counts + MATD shader/textures |
| `rcol.replaceChunk` | Replace one chunk by index; backup + undo |

See also [objd.md](objd.md) and [casp.md](casp.md) for field layout assumptions.
