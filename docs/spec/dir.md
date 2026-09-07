# DIR resource `0xE86B1EEF`

Sources:

- https://simstek.fandom.com/wiki/DBPF
- https://simswiki.info/wiki.php?title=Sims_3:DBPF/Compression

## Role

Older DBPF (Sims 2 / index 7.x) stored compressed size in the regular index and put **uncompressed** sizes in a DIR resource. TS3’s index already has `MemSize`, so DIR is redundant.

## Survey (2026-09-07)

DIR is **absent** from Steam FullBuild0 (102 127 rows), fallback, DeltaBuild_p20, other delta packs, worlds, caches, and local CC (198 files under 400 MiB). EA TS3 does not ship DIR.

## On disk (when present)

TS3 (64-bit instance) — **20 bytes** per record, little-endian. Writer emits this shape:

```
uint32 type
uint32 group
uint32 instance_hi
uint32 instance_lo
uint32 mem_size
```

Legacy 16-byte records (32-bit instance + mem_size) are accepted on read if `size % 16 == 0` and not `% 20`.

DIR lists compressed resources so `mem_size` can be cross-checked against the index. It does **not** store chunk offsets.

## Read

- No DIR → OK. Use index `mem_size`.
- DIR present → parse; `package.validate` reports `dir.records`, `dir.recordBytes`, `dir.unmatched`.
- Do not drop the resource on round-trip (copy bytes through unless the session deleted it).

## Write

- File→New / SXPE saveAs: **do not invent** a DIR (matches EA).
- If the input had a DIR, copy it through. Rebuild is a later merge-policy choice (`dirPolicy` on #9).

## Tests

Synthetic 20-byte round-trip in `tests/dir_test.cpp`. No EA bytes.
