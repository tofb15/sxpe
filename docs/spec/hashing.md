# FNV-1 and CLIP instance hashing

Sources:

- https://simswiki.info/wiki.php?title=FNV
- https://simswiki.info/wiki.php?title=Sims_3:0x6B20C4F3
- Public FNV-1 (offset/prime below)

## FNV-1 (not FNV-1a)

Process: **multiply then xor** (FNV-1). Input strings are **lowercased** for TS3 catalog/instance hashing (**TBC-game** exact Unicode rules).

| Width | Offset | Prime |
| --- | --- | --- |
| 32 | `0x811C9DC5` | `0x01000193` |
| 64 | `0xCBF29CE484222325` | `0x00000100000001B3` |

## S3SA / instantiator instance

Community script mods (not CLIP): FNV-1 64, **lowercase ASCII**, of

- the DLL **file name** including `.dll` → S3SA instance (`0x073FAA07`, group `0`)
- the C# `Namespace.Class` of the instantiator → `_XML` instance (`0x0333406C`, group `0`)

See [s3sa.md](s3sa.md). Do not use FNV-1a. Do not split the 64-bit hash across group/instance.

## CLIP instance

FNV-1 64 of the clip name after **age-letter substitution** per SimsWiki CLIP (`0x6B20C4F3`). Implemented in `fnv64_clip` (do not copy s3pi `FNV64CLIP`).

Names: `x_anim` (one actor) or `x2y_anim` (two actors). `a` and `o` are defaults.

1. Lowercase ASCII.
2. Hash the name with non-default `x`/`y` replaced by `a` (`o` stays `o`).
3. If every actor is `a` or `o`: clear bit 63.
4. Else: set bit 63. XOR the high byte with the primary age mask and, for two-actor names, the second-highest byte with the target mask.

| Mask | Age letter |
| --- | --- |
| `0x01` | b |
| `0x02` | p |
| `0x03` | c |
| `0x04` | t |
| `0x05` | h |
| `0x06` | e |

Names without an `x_` / `x2y_` prefix hash as plain FNV-1 64 lowercase (no top-bit change).

Frozen test vectors (paste into tests; do not recompute in the assert helper):

| Name | Instance (hex) | Notes |
| --- | --- | --- |
| `walk` | plain FNV-1 64(`walk`) | no `x_` prefix → no age masks |
| `a_walk` | `0x11a06ab91bca6bde` | default age `a`; bit 63 clear |
| `t_walk` | `0x95a06ab91bca6bde` | hash as `a_walk`, set bit 63, XOR high byte with `0x04` |
| `a2a_sit` | `0x3b27e4eac9eb9af8` | defaults; bit 63 clear |
| `t2c_sit` | `0xbf24e4eac9eb9af8` | two-actor; XOR high/`0x04` and mid/`0x03` |

Source algorithm: [SimsWiki CLIP `0x6B20C4F3`](https://simswiki.info/wiki.php?title=Sims_3:0x6B20C4F3) + public FNV-1. `clip.exportAs` must set `resourceId.instance` to these values (`tests/commands_test.cpp`).

Community filename:

`S3_{type}_{group}_{instance}_{name}%%+CLIP.animation`

(hex type/group/instance, no `0x` **TBC** community convention - match existing CC files).

## Synthetic vectors (FNV-1, lowercase ASCII `"a"`)

Compute in tests when the codec lands. Known public: empty string FNV-1 32 = offset basis `0x811C9DC5`.

## Package compare (`package.diff`)

Payload equality for `package.diff` uses **SHA-256 of the uncompressed resource body**
(after RefPack decompress when the index marks compression). On-disk compressed bytes are
not compared, so recompression alone does not count as a difference. Keys are
`type` + `group` + `instance` + `ordinal`.

Alternative considered: size + compressed flag only - rejected because identical sizes can
hide content changes. Documented choice: SHA-256 uncompressed.

