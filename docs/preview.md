# Resource preview inventory

Long-term goal: **every selected resource shows something useful** on the inspector Preview tab (image, structured summary, text, or an honest “opaque binary” card). This file lists what we know can be previewed, whether or not SXPE should do it soon.

s3pe’s Auto Preview is the reference *behaviour*, not source: image / wrapper `Value` string / type-specific control, then fallback hex or text. It does **not** render 3D meshes or play clips in the main pane.

## What SXPE already shows

| Surface | Today |
| --- | --- |
| Preview tab | PNG (magic `89 PNG`) or DDS (DXT1 / DXT5 / 24/32-bit RGB) as pixels. Else “No image preview”. |
| Hex tab | First 4 KiB as hex (`hex.get`). |
| Graph tab | Size plus a few parsed nodes (`graph.get`; STBL ids, otherwise one blob). |
| Text tab | STBL table (`stbl.get`) or first 8 KiB as text (`text.get`). |

Caps: skip live preview above `kMaxLivePreviewBytes` (8 MiB).

Difficulty: **S** = days (reuse existing codecs), **M** = a week or two (new small parser + UI), **L** = months (real 3D / media stack).  
Priority: **P0** ship next, **P1** after that, **P2** later, **P3** research / maybe never in-process.

---

## 1. Raster images (already mostly done)

These are the only types s3pe draws as pictures in Preview.

| Tag | Types (hex) | Payload | Difficulty | Priority | Notes |
| --- | --- | --- | --- | --- | --- |
| `_IMG` | `00B2D882`, `8FFB80F6` | DDS | S (done) | P0 | Decode DXT1/DXT5/24/32. No BC7 / DirectXTex. |
| `THUM` | `0580A2B4–B6`, `0589DC44–47`, `05B17698–9A`, `05B1B524–26`, `2653E3C8–CA`, `2D4284F0–F2`, `5DE9DBA0–A2`, **`626F60CC–CE` (CAS)** | PNG | S (done for listed IDs) | P0 | Catalog / CAS / fence thumbs. More IDs may appear; magic sniff covers unlisted PNGs. |
| `SNAP` | `0580A2CD–CF`, `6B6D837D–7F` | PNG | S (done) | P0 | Sim / family snapshots. Neighbourhood SNAPs are game-picky on encode, not on preview. |
| `ICON` | `2E75C764–767` | PNG | S (done) | P0 | Object icons. |
| `IMAG` | `2F7D0004` PNG, `2F7D0002` JPEG | PNG/JPEG | S | P1 | JPEG not sniffed yet; Qt can load it. |
| `TSNP` | `54372472` | PNG | S (done) | P1 | Travel snapshot. |
| `TWNI` | `0668F635` | PNG | S | P1 | Town image; not in tag table yet. |
| extra THUM/ICON | `AD366F95–96`, `D84E7FC5–C7`, `FCEAB65B` | PNG | S | P1 | s3pe ImageControl list; add tags when seen. |

**Catch-all:** Preview already sniffs PNG/DDS magic regardless of type. Unknown PNG thumbs still *display*; they just lack a Tag until catalogued.

**Do not** treat TXTC/TXTF as raw images; they are compositors, not DDS/PNG.

---

## 2. Structured text (high value, s3pe does this)

s3pe TextControl / wrapper `Value`. SXPE has `text.get` but Preview does not use it.

| Tag | Types | Preview | Difficulty | Priority |
| --- | --- | --- | --- | --- |
| `_XML` | `0333406C` | Pretty XML, encoding BOM, root element | S | **P0** |
| `ITUN` | `03B33DDF` | Same as XML (`<base/>` tuning) | S | **P0** |
| `LAYO` | `025C95B6` | XML UI layout | S | P1 |
| `_CSS` | `025C90A6` | CSS text | S | P1 |
| `COMP` | `044AE110` | XML complate preset | S | P1 |
| `DMTR` | `0604ABDA` | XML dream tree | S | P2 |
| other XML stores | `73E93EEB` manifest, `A8D58BE5` skills, `D4D9FBE5` patterns, `DD3223A7` buffs | XML | S | P2 |
| `1F886EAD` | INI-like config | Text | S | P2 |

UTF-8 / UTF-16LE sniff + first N lines is enough. Full schema validation is out of scope for Preview.

---

## 3. Tables SXPE already parses

| Tag | Types | Preview | Difficulty | Priority |
| --- | --- | --- | --- | --- |
| `STBL` | `220557DA` | Count + first rows (id → string). Full table is already the Text tab. | S | **P0** |
| `NMAP` | `0166038C` | Count + first instance→name rows (`nmap.get`) | S | **P0** |
| `DIR` | `E86B1EEF` | Compressed-resource count / sample TGIs | S | P1 |

---

## 4. Assemblies, keys, catalog (summary cards)

No pixels. A few decoded fields beat hex.

| Tag | Types | Preview | Difficulty | Priority |
| --- | --- | --- | --- | --- |
| `S3SA` | `073FAA07` | Size, PE/`MZ` offset, module hint from NMAP (`s3sa.info`). Never `LoadLibrary`. | S | **P0** |
| `OBJK` | `02DC343F` | Graph of known fields (`objk.get` already exists, thin) | S–M | P1 |
| `VPXY` | `736884F1` | Chunk list (`vpxy.get`) | S–M | P1 |
| `OBJD` | `319E4F1D` | Catalog name/desc GUIDs, price, thumbnail IID (public catalog header) | M | P1 |
| `CASP` | `034AEECB` | Clothing type, age/gender flags, TGI refs | M | P1 |
| `SIMO` | `025ED6F4` | Outfit TGI list | M | P2 |
| `FAMD` | `062853A8` | Household / member count | M | P2 |
| `OBJN` | `4D1A5589` | Instance count | M | P2 |
| catalog (`CFEN` `CSTR` `CWAL` `CRAL` `CFIR` `CTPT` `CFND` `CWST` `CRST` `CRMT` `CWAT` `CCFP` `CPRX` `CTTL`) | see `types.hpp` | Same catalog common header as OBJD | M | P2 |
| `TONE` | `0354796A` skin, `03555BA8` hair | Colour / related TGIs | M | P2 |
| `CINF` `HINF` `OBCI` | colour info | RGB / labels | M | P2 |
| `REFS` | `05ED1226` | Referenced TGI list | M | P2 |
| `DETL` | `03D86EA4` | Lot/world summary | M | P2 |

---

## 5. Geometry / RCOL (possible, expensive)

Public RCOL chunk layout (MODL/MLOD/GEOM/MATD). s3pe does **not** 3D-preview these in the main pane.

| Tag | Preview that is still useful | Difficulty | Priority |
| --- | --- | --- | --- |
| `GEOM` | Vertex/face counts, bone count, UV sets — **not** a 3D view at P0 | M (counts) / **L** (mesh GL) | P1 counts, P3 GL |
| `MODL` `MLOD` | LOD count, chunk types (MATD/VBUF/IBUF/SKIN) | M | P1 |
| `MATD` | Shader name, texture TGI refs | M | P1 |
| `VBUF` `IBUF` `VRTF` `SKIN` | Buffer sizes / format | M | P2 |
| `BONE` | Bone names / count (`00AE6C67` skcon) | M | P2 |
| `VPXY` | Already listed; often a TGI list to MODL | S–M | P1 |
| `BGEO` `BLND` `BBLN` `BOND` `FACE` | Blend morph metadata | M–L | P2 |
| `SPT2` `_SPT` | SpeedTree: size / version only | M | P3 |
| `TREE` | Same | M | P3 |

A real mesh preview (Qt + GL, skinning, materials) is a product of its own. Do not block other work on it.

---

## 6. Animation / audio / video

| Tag | Preview | Difficulty | Priority |
| --- | --- | --- | --- |
| `CLIP` | Duration, track names, hashed names (`clip.exportAs` exists; no player) | M | P1 |
| `JAZZ` | State-machine / clip name list | M | P2 |
| `TKMK` | Track-mask bit count | M | P2 |
| `_AUD` | Fourcc / sample rate if we parse SNR; optional PCM play | M / L (playback) | P2 metadata, P3 play |
| `VOCE` `MIXR` | Controller/mixer names | M | P2 |
| `_VID` | VP6/AVI header; first-frame still would need a decoder | M header / **L** frames | P2 header, P3 video |
| `ANIM` | `63A33EA7` animated texture — treat as image sequence later | L | P3 |

s3pe does not play CLIP or audio in Preview.

---

## 7. Catch-all (every remaining type)

For anything unparsed, Preview can still be non-empty:

| Mode | What | Difficulty | Priority |
| --- | --- | --- | --- |
| Identity card | Tag, TGI hex, size, compressed, NMAP name | S | **P0** (default when nothing else matches) |
| Magic sniff | PNG / DDS / JPEG / `MZ` / `<?xml` / UTF-16 BOM | S | **P0** |
| Hex excerpt | Same as Hex tab, 256 bytes | S | P0 fallback |
| UTF-8 excerpt | Same as Text tab if mostly printable | S | P0 fallback |

That is how “preview for all types” is reachable without a decoder per fourcc.

---

## Recommended focus (next implementations)

Do **not** start with 3D. Fill the Preview tab so a click always answers “what is this?”

### Wave 1 — P0 (reuse codecs we have)

1. **Identity card** for every resource (TGI + tag + size + name).  
2. **STBL** — first ~20 strings.  
3. **NMAP** — first ~20 names.  
4. **XML family** (`_XML`, `ITUN`) — pretty first ~4 KiB.  
5. **S3SA** — `s3sa.info` card (PE / module hint).  
6. Keep **PNG/DDS** as now; add **JPEG** magic (`IMAG` `2F7D0002`).

### Wave 2 — P1 (modder daily drivers)

7. Catalog **OBJD** header (name GUID, price, thumb IID).  
8. **CASP** clothing type / flags.  
9. **OBJK** / **VPXY** (extend existing graph commands into Preview).  
10. **CLIP** duration / tracks (no playback).  
11. **MODL/MLOD/GEOM** counts only.  
12. Remaining PNG type IDs from s3pe’s image list (`TWNI`, `AD366F95`, `D84E7FC*`, `FCEAB65B`).

### Wave 3 — P2 / P3

Audio metadata, VID header, full catalog family, then optional GL mesh / CLIP play / VP6 — only if Wave 1–2 stay solid.

---

## Out of scope for Preview

- Executing S3SA (no `LoadLibrary`).  
- Writing neighbourhood SNAPs (encode rules stay on File → Save).  
- Pixel-perfect s3pe wrapper parity.  
- Shipping EA packages as fixtures; use synthetic PNG/DDS/STBL/NMAP only.
