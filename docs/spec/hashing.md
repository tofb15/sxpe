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

## CLIP instance

FNV-1 64 of the clip name after **age-letter substitution** per SimsWiki CLIP (`0x6B20C4F3`): top bit and XOR masks. Exact mask table **TBC-game** / wiki; do not copy s3pi `FNV64CLIP`.

Community filename:

`S3_{type}_{group}_{instance}_{name}%%+CLIP.animation`

(hex type/group/instance, no `0x` **TBC** community convention — match existing CC files).

## Synthetic vectors (FNV-1, lowercase ASCII `"a"`)

Compute in tests when the codec lands. Known public: empty string FNV-1 32 = offset basis `0x811C9DC5`.
