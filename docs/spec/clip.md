# CLIP animation `0x6B20C4F3`

Sources: [SimsWiki CLIP](https://simswiki.info/wiki.php?title=Sims_3:0x6B20C4F3) (community layout). SXPE uses **synthetic fixtures only** (no EA samples). **No CLIP playback.**

Related: [hashing.md](hashing.md) (`fnv64_clip`), [preview-wave2.md](preview-wave2.md).

## Safe fields (editable)

| Field | Bus key | Notes |
| --- | --- | --- |
| Animation name | `animName` | NUL C-string via offset from `_S3Clip_` start |
| Source file | `sourceFile` | NUL C-string; jazz scripts often key off this |
| Actor name | `actorName` | NUL C-string at main-header `actoroffset`, padded to DWORD with `0x7e` |
| Track / joint hashes | `trackHashes` | `[{index, hash}, …]` — FNV32 bone (or morph) hashes in the joint-rule table |

`clip.set` rewrites only these. Frame data, event sections, slot tables, duration/frame counts, and playback are **out of scope**.

When a new string is longer than the old slot, SXPE appends it (preserving the trailing 16-byte end block when present) and updates the relative offset. Shorter strings overwrite in place.

## Read-only summary fields

| Field | Notes |
| --- | --- |
| `version` | S3 clip header version (typically 2) |
| `frameDuration` / `frameCount` / `durationSeconds` | `duration = frameDuration * frameCount` |
| `trackCount` | Joint movement rule count |
| `partial` | Parse recovered via magic scan or truncated tables |

## Commands

| id | notes |
| --- | --- |
| `clip.info` | Full summary for Preview / Graph; includes `safeFields` |
| `clip.set` | Patch safe fields; `dryRun` + session undo |
| `clip.exportAs` | Copy resource with `instance = fnv64_clip(name)` |
| `clip.exportAsBatch` | `items: [{resourceId,name},…]` (cap 256); per-item results |

GUI: **Resource → Editors → CLIP metadata…** and **CLIP export as new name…**.  
CLI: `sxpe clip info|set|export-as|export-as-batch` (see [cli-mcp.md](../cli-mcp.md)).

## Track rename helpers

1. **Instance rename:** `clip.exportAs` / `clip.exportAsBatch` — new package instance via age-letter `fnv64_clip`.
2. **In-clip anim name:** `clip.set` `animName` (and optionally `sourceFile` / `actorName`) so the payload matches the new identity.
3. **Bone/track hash:** `clip.set` `trackHashes: [{index, hash}]` when a rig bone rename changes the joint-rule hash.

Do not use these tools as a CLIP player or mesh retargetter.
