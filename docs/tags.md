# Resource tags

The **Tag** column is a four-character label for the resource **type** id (the first field of a TGI). Same tag can cover several type ids (for example every catalog thumbnail size is `THUM`). Empty tag means the type is not in SXPE’s table yet; the bytes are still a normal DBPF resource.

Sources: public Sims 3 type lists (SimsWiki PackedFileTypes, catalog resource pages, TS3 `ResourceTypes`). Not an s3pi ExtList dump. Payload notes are what the resource **contains**, not a promise that SXPE fully parses it.

Type ids are hex, no `0x` prefix in tables. Group and instance are not part of the tag.

---

## How to read a TGI

`626F60CE-00000001-62A3D8757A6A4A6C` means:

| Field | Value | Meaning |
| --- | --- | --- |
| Type | `626F60CE` | What kind of blob (here CAS large thumbnail → tag `THUM`) |
| Group | `00000001` | Variant / preset / language / world partition |
| Instance | `62A3D8757A6A4A6C` | Which object (often FNV of a name, or copied from OBJD/CASP) |

The **payload** is the file bytes for that row (PNG, DDS, XML, mesh, PE, …), optionally RefPack-compressed on disk.

---

## Package bookkeeping

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **NMAP** | `0166038C` | **Name map.** Table of instance id → UTF-8 (or 7-bit) display name. This is the **Name** column and the string the game uses for script instantiator FQNs (`NRaas.NoCD`, `MomMoney.dll`). Not stored in the DBPF index. |
| **DIR** | `E86B1EEF` | **Compression directory.** List of which index entries are RefPack-compressed. Package metadata, not game content. |
| **STBL** | `220557DA` | **String table.** Map of 64-bit string id → localized text (catalog names, buffs, UI). One STBL per language; English is often instance `00…`. |

---

## Images (pixels)

These payloads are pictures. Preview can show them when the blob is PNG or DDS.

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **_IMG** | `00B2D882`, `8FFB80F6` | **DDS texture.** Diffuse/specular/normal maps for objects, sims, terrain. DXT1/DXT5 or uncompressed RGB(A). Not a compositor (see TXTC). |
| **THUM** | `0580A2B4–B6` (object catalog small/med/large), `0589DC44–47` (stairs), `05B17698–9A`, `05B1B524–26`, `2653E3C8–CA` (fence), `2D4284F0–F2`, `5DE9DBA0–A2`, **`626F60CC–CE` (CAS small/med/large)** | **PNG thumbnail** for Build/Buy or CAS. Same instance as the catalog/CASP item; group often selects a colour preset (`…01`, `…02`, …). Size is implied by which type id in the triple. |
| **ICON** | `2E75C764–767` | **PNG icon** for an object (small → very large). Same role as THUM, different type family. |
| **IMAG** | `2F7D0004` (PNG), `2F7D0002` (JPEG) | **Standalone image** (UI or compositor input), PNG or JPEG. |
| **SNAP** | `0580A2CD–CF` (sim portraits), `6B6D837D–7F` (household) | **PNG snapshot** the game took of a sim or family. Neighbourhood `.nhd` SNAPs are layout-locked and the decoder is picky about extra PNG chunks. |
| **TSNP** | `54372472` | **PNG travel snapshot** (vacation world portraits). |

---

## 3D / scene (RCOL)

RCOL resources are binary scene graphs: meshes, materials, buffers. They are **not** PNG/DDS.

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **GEOM** | `015A1849` | **Body / CAS geometry.** Skinned mesh: vertices, faces, UVs, bone weights for clothes and bodies. |
| **MODL** | `01661233` | **Object model** (high-level RCOL). References LODs, materials, and collision. World and Buy-mode objects. |
| **MLOD** | `01D10F34` | **Model LOD.** One level of detail for a MODL: actual mesh chunks. |
| **MATD** | `01D0E75D` | **Material definition.** Shader name plus parameters (colours, which `_IMG`/TXTC to bind). |
| **MTST** | `02019972` | **Material set.** Groups MATDs for an object. |
| **VBUF** | `01D0E6FB`, `0229684B` | **Vertex buffer.** Raw vertex positions/normals/UVs used by a mesh. |
| **IBUF** | `01D0E70F`, `0229684F` | **Index buffer.** Triangle indices into a VBUF. |
| **VRTF** | `01D0E723` | **Vertex format.** Describes the layout of a VBUF (which attributes, stride). |
| **SKIN** | `01D0E76B` | **Skinning data.** Bone indices/weights for a mesh. |
| **VPXY** | `736884F1` | **Visual proxy.** Lightweight pointer from a catalog object to its MODL/MATD set (what you “see”). |
| **LITE** | `03B4C61D` | **Light parameters.** Colour, type, falloff for lamps. |
| **BONE** | `00AE6C67` | **Skeleton (skcon).** Bone hierarchy and rest pose for a rig. |
| **_SPT** | `00B552EA` | **SpeedTree** asset (tree/plant procedural model). |
| **SPT2** | `021D7E8C` | Extra SpeedTree data used with `_SPT`. |

---

## Animation

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **CLIP** | `6B20C4F3` | **S3Clip.** Keyframed animation (walk, use object, CAS pose). Instance is often FNV-64 of the clip name. |
| **JAZZ** | `02D5DF13` | **Jazz state machine.** Which CLIPs play in which order (animation graphs). |
| **TKMK** | `033260E3` | **Track mask.** Which animation tracks a CLIP is allowed to drive. |
| **ANIM** | `63A33EA7` (not always tagged in SXPE) | Animated texture / scenegraph animation resource. |

---

## Audio and video

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **_AUD** | `01A527DB` (voice SNR), `01EEF63A` (fx/music SNS) | **Compressed audio** (Sims 3 SNR/SNS). Not a WAV sitting in the package. |
| **VOCE** | `029E333B` | **Voice mix controller.** How voice layers are mixed, not the samples themselves. |
| **MIXR** | `02C9EFF2` | **Audio submix / mixer** graph. |
| **_VID** | `0B2CB440` | **Video.** Often VP6 or AVI-in-package for cutscenes/TV. |

---

## Create-a-Sim (CAS) and sim appearance

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **CASP** | `034AEECB` | **CAS part definition.** Clothes, hair, accessories: category, age/gender flags, links to GEOM, TXTC, TONE, blends. Not the mesh or texture themselves. |
| **TONE** | `0354796A` (skin), `03555BA8` (hair) | **Tone.** Skin or hair colour definition and texture links. |
| **FACE** | `0358B08A` | **Face blend / face parts.** Morph targets for facial CAS. |
| **BOND** | `0355E0A6` | **Bone delta.** Slot/bone offsets (e.g. shoe, accessory placement). |
| **BBLN** | `062C8204` | **Body blend.** Fat/fit/thin (and similar) morph resources. |
| **BGEO** | `067CAA11` | **Blend geometry.** Morph mesh deltas. |
| **BLND** | `B52F5055` | **Blend unit.** How blends combine. |
| **CBLN** | `051DF2DD` | **Face-piece preset** (compound blend / preset). |
| **SIMO** | `025ED6F4` | **Sim outfit.** Which CASP/GEOM a sim is wearing. |
| **SBNO** | `04F51033` | **Bin outfit.** Library/bin clothing set. |
| **SIME** | `04F88964` | **Townie / sim export** blob (NPC data). |
| **COAT** | `DEA2951C` | **Coat set** (pet/animal coat). |
| **CINF** | `06302271` | **Colour information** for CAS materials. |
| **HINF** | `063261DA` | **Hair colour information.** |
| **OBCI** | `06326213` | **Object colour information.** |
| **UPST** | `0591B1AF` | **User CASt preset** saved from Create-a-Style. |
| **COMP** | `044AE110` | **Complate preset.** XML describing a style/preset. |
| **TXTC** | `033A1435` | **Texture compositor.** Recipe that *builds* a texture from layers/`_IMG`s (not the final DDS). |
| **TXTF** | `0341ACC9` | **Fabric compositor.** Same idea for fabric/pattern. |
| **CCHE** | `03D843C2` | **Compositor cache entry** (generated cache, not authored CC). |
| **CMRU** | `04D82D90` | **Compositor MRU** (recent compositor cache). |

---

## Catalog (Build/Buy Buy mode)

These are **buy-mode definitions**: name/description GUIDs (into STBL), price, flags, and which mesh/thumb to use. They are not the mesh and not the PNG.

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **OBJD** | `319E4F1D` | **Catalog object.** Furniture, appliances, deco: catalog header + object-specific fields. |
| **OBJK** | `02DC343F` | **Object key.** Ties a catalog item to scripts and VPXY (“what is this object”). |
| **OBJN** | `4D1A5589` | **Placed object instances** in a lot/world (not the catalog definition). |
| **CFEN** | `0418FE2A` | Catalog **fence**. |
| **CSTR** | `049CA4CD` | Catalog **stairs**. |
| **CRAL** | `04C58103` | Catalog **railing**. |
| **CFIR** | `04F3CC01` | Catalog **fireplace**. |
| **CFND** | `316C78F2` | Catalog **foundation**. |
| **CWAL** | `515CA4CD` | Catalog **wall/floor pattern**. |
| **CWST** | `9151E6BC` | Catalog **wall style**. |
| **CRST** | `91EDBD3E` | Catalog **roof style**. |
| **CRMT** | `F1EDBD86` | Catalog **roof pattern**. |
| **CTPT** | `04ED4BB2` | Catalog **terrain paint** brush. |
| **CTTL** | `04B30669` | Catalog **terrain geometry** brush. |
| **CWAT** | `060B390C` | Catalog **water** brush. |
| **CCFP** | `0A36F07A` | Catalog **fountain/pool**. |
| **CPRX** | `04AC5D93` | Catalog **proxy product** (placeholder catalog row). |

---

## Scripts and tuning (text / PE)

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **DIR** | `E86B1EEF` | **Compression directory.** Optional list of TGI + uncompressed size. EA TS3 packages omit it (MemSize is already in the index). 20-byte records. |
| **SXMM** | `53584D4D` | **SXPE merge manifest.** JSON listing which resources came from which source package. Required for un-merge. |
| **S3MF** | `73E93EEB` | **Sims3Pack leftover manifest XML** (instance 0). Launcher leftover; strip on merge (`leftoverManifestPolicy`). Not gameplay `_XML`. |
| **S3SA** | `073FAA07` | **Script assembly.** .NET DLL (PE) wrapped for the game. `s3sa.importDll` / `exportDll` / `s3sa.view` wrap, decrypt, and open via an external viewer. Never load it with `LoadLibrary`. |
| **_XML** | `0333406C` | **XML resource.** Tuning, instantiator doors (`kInstantiator`), snippets. UTF-8 or UTF-16, often with a BOM. Edit via `xml.get`/`xml.set` / Editors → XML…. |
| **ITUN** | `03B33DDF` | **Interaction tuning.** XML for autonomy, ads, pie-menu (TTAB-like). Same editor as `_XML`. |
| **LAYO** | `025C95B6` | **UI layout** XML. |
| **_CSS** | `025C90A6` | **CSS** for UI. |
| **DMTR** | `0604ABDA` | **Dreams and promises** tree (XML). |

---

## World, lot, household

| Tag | Type ids | What the bytes are |
| --- | --- | --- |
| **FAMD** | `062853A8` | **Household / family** data (members, funds, home). |
| **DETL** | `03D86EA4` | **Lot / world detail** (which lot, routing-related world info). |
| **REFS** | `05ED1226` | **Reference store.** List of TGIs this world/lot points at. |
| **2ARY** | `05FF6BA4` | **World binary** (terrain/elevation-style data). |
| **OBJN** | `4D1A5589` | Instances of objects placed on a lot (see catalog). |

---

## Empty tag

If Tag is blank, SXPE does not have a four-char name for that type id. The resource is still valid. Preview may still show a PNG/DDS if the payload starts with those magics. Add the type to `include/sxpe/resources/types.hpp` (and this file) when a public name is known.

---

## See also

- [preview.md](preview.md) — what is worth showing in the inspector, by difficulty
- [nmap.md](spec/nmap.md), [stbl.md](spec/stbl.md) — on-disk layouts we implement
- `include/sxpe/resources/types.hpp` — machine-readable tag table
