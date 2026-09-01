# DBPF package header (The Sims 3)

Little-endian unless noted. Size **96 bytes**.

Sources:

- https://simswiki.info/wiki.php?title=Sims_3:DBPF
- https://simstek.fandom.com/wiki/DBPF
- https://www.wiki.sc4devotion.com/index.php?title=DBPF

## Layout

| Offset | Size | Field | TS3 value |
| --- | --- | --- | --- |
| 0x00 | 4 | Magic | ASCII `DBPF` (`44 42 50 46`) |
| 0x04 | 4 | Major | **2** |
| 0x08 | 4 | Minor | **0** |
| 0x0C | 24 | unknown1 | Preserve on round-trip. **TBC-game** |
| 0x24 | 4 | Index entry count | `0` ⇒ empty package (index may still contain `indexType`) |
| 0x28 | 4 | unknown2 | Preserve. **TBC-game** |
| 0x2C | 4 | Index size (bytes) | Size of the index blob including `indexType` |
| 0x30 | 12 | unknown3 | Older DBPF hole table. Preserve. **TBC-game** |
| 0x3C | 4 | Index version | **3** for TS3 |
| 0x40 | 4 | Index position | Absolute file offset of the index. Often near EOF. **TBC-game** |
| 0x44 | 28 | unknown4 | Preserve. **TBC-game** |

## Open rules

- Magic not `DBPF` → `unsupported_game_or_format`. `DBPP` / `DBBF` → treat as protected/other; do not parse as TS3.
- Major **3** → SimCity 2013 family. **Do not** create in v1 File→New. Refuse or open read-only only if later enabled.
- Major not 2 (and not handled SC5) → refuse (Sims 2 / Spore / other).
- Index count > 500_000 → refuse (cap).
- File smaller than 96 bytes → refuse.

## Empty package (synthetic)

`fixtures/synthetic/empty.bin` is a 100-byte file: 96-byte header, count 0, index version 3, index at offset 96, index size 4 (`indexType = 0` only).

## TBC-game

Confirm on `FullBuild0.package`: exact unknown fields, typical index position (EOF vs other).
