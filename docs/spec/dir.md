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

- File→New / SXPE `package.saveAs`: **do not invent** a DIR (matches EA).
- Opening a package that already has a DIR keeps it on round-trip (copy bytes through unless the session deleted it).

## Merge `dirPolicy` (`notes.dirPolicy` on SXMM)

`resource.importPackage` accepts `dirPolicy`:

| Value | Default when | Behavior |
| --- | --- | --- |
| `strip` | `writeMergeManifest` | Skip source DIR rows |
| `copy-through` | import without merge manifest | Copy source DIR through (force on duplicate TGI) |
| `rebuild` | — | **Not yet**; refused with a clear error |

Pass `--dir-policy copy-through` (or `dirPolicy` in JSON) with `--write-merge-manifest` to preserve legacy DIR in an SXPE merge and record that choice in SXMM `notes.dirPolicy`.

## Tests

Synthetic 20-byte round-trip in `tests/dir_test.cpp`. Strip + copy-through import coverage in `tests/commands_test.cpp`. No EA bytes.
