# TS3 resource index (version 3)

Source: https://simswiki.info/wiki.php?title=Sims_3:DBPF

The index starts at the header’s **index position**. First field:

```
uint32 indexType   // bitmask of fields stored once in an "index header"
```

Then `popcount(indexType)` dwords of **shared** header values (fields whose bits are set).

Then **per entry** `8 - popcount(indexType)` dwords: the fields whose bits are **clear** in `indexType`.

Field order (bit 0 = LSB):

| Bit | Field | Type | Notes |
| --- | --- | --- | --- |
| 0 | ResourceType | uint32 | TGI type |
| 1 | ResourceGroup | uint32 | TGI group; top byte sometimes flags |
| 2 | InstanceHi | uint32 | High 32 of instance |
| 3 | InstanceLo | uint32 | Low 32; instance = `(hi << 32) \| lo` |
| 4 | ChunkOffset | uint32 | Absolute offset of payload |
| 5 | FileSize | uint32 | **Low 31 bits** = on-disk size; **high bit** Unknown1 **TBC-game** |
| 6 | MemSize | uint32 | Uncompressed size |
| 7 | CompressedFlags | uint32 | Low 16 = compressed (`0x0000` or `0xFFFF`); high 16 = Unknown2 **TBC-game** |

## Writer (v1)

Use **`indexType = 0`**: no shared header, **32 bytes per entry** (8 dwords). Do not hoist offset/size/compressed into a shared header unless FullBuild proves we must.

```
struct IndexEntry {
    uint32_t type;
    uint32_t group;
    uint32_t instance_hi;
    uint32_t instance_lo;
    uint32_t chunk_offset;
    uint32_t file_size;      // mask 0x7FFFFFFF for length
    uint32_t mem_size;
    uint16_t compressed;     // 0 or 0xFFFF
    uint16_t unknown2;
};
```

`resourceId` for commands: `{ type, group, instance, ordinal }` where `ordinal` is the 0-based index among entries with the same TGI (duplicate TGI is legal).

## Compressed

- `compressed == 0` → payload is raw `mem_size` bytes (`file_size` should match, **TBC-game**).
- `compressed == 0xFFFF` → payload is RefPack; on-disk length = `file_size & 0x7FFFFFFF`; uncompressed = `mem_size`.

## Deleted

Encoding **TBC-game** (group high byte vs `unknown2` vs hole). Until confirmed, SXPE may track a session flag without claiming on-disk encoding.

## Caps

- Entry count ≤ 500_000
- `mem_size` ≤ 256 MiB
- `mem_size / file_size` ≤ 1024 when compressed (bomb)

## Synthetic

`fixtures/synthetic/single-blob.bin` — one uncompressed resource, `indexType = 0`.
