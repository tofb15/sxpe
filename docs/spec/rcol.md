# RCOL / MATD summary and chunk replace

Issue #59. Extends Preview Wave 2 `rcol.summary` with MATD material refs and a safe per-chunk replace. **Not** an in-app 3D viewport (see [preview.md](../preview.md) P3).

Sources: SimsWiki RCOL / `0x01D0E75D` (MATD) / Shaders - community layouts, not EA-shipped samples.

## `rcol.summary`

- RCOL header: version, public/internal/external counts, internal chunk TGIs + absolute ranges, external TGI table.
- Tags known chunk types (MODL/MLOD/GEOM/MATD/VBUF/IBUF/…).
- MLOD/GEOM mesh counts when the subset/format walk succeeds (unchanged from Wave 2).
- **MATD** (when the chunk starts with `MATD` and MTNF/MTRL parses):
  - `shaderHash` / `shaderName` when the hash matches the community SimsWiki shader list (FNV-1 32, lowercase).
  - `textures[]`: type-code `4` params - either RCOL reference (`0x0/0x1/0x3` × 1-based index into internal/external tables) or TextureKey ITG (instance/type/group).
  - Top-level `textures[]` flattens MATD refs; `externalTgis[]` lists the external table (often `_IMG` / TXTC / ANIM).
- Caps oversized chunk tables (`4096`). Partial parses set `partial`.
- Bare `GEOM` / `MATD` (no RCOL wrapper) still summarize as a single chunk.

## `rcol.replaceChunk`

| Arg | Notes |
| --- | --- |
| `sessionId` / `resourceId` | Target MODL/MLOD/GEOM/MATD (or any RCOL body) |
| `chunkIndex` | 0-based internal chunk index |
| `payloadB64` or `path` | New chunk bytes |
| `backupPath` | Optional: write **previous** chunk bytes before mutate |
| `dryRun` | Report old/new sizes without writing |

- Rebuilds the location table; **preserves** version, public count, and TGI tables.
- Other chunk payloads are copied verbatim; only the selected index changes.
- Session **undo** / **redo** snapshots the whole resource (same stack as `resource.replace`).
- Bare GEOM/MATD/MLOD bodies only accept `chunkIndex` 0 (replace the whole body).

## Commands

| id | notes |
| --- | --- |
| `rcol.summary` | Chunk / LOD / v-f counts + MATD shader/textures |
| `rcol.replaceChunk` | Safe replace by index; backupPath + undo |

## Explicit non-goals

- In-app mesh GL / 3D viewport
- Full material/shader editors or MTNF param writing
- Shipping EA packages as fixtures (synthetic RCOL-like bytes only)

See also [preview-wave2.md](preview-wave2.md).
