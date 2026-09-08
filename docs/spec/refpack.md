# RefPack / QFS (TS3 payload compression)

Sources:

- https://simstek.fandom.com/wiki/RefPack
- http://wiki.niotso.org/RefPack
- https://simswiki.info/wiki.php?title=Sims_3:DBPF/Compression

**This is not zlib.** Do not substitute zlib/zstd.

## Detection

Common: byte0 `0x10`, byte1 `0xFB`, then **3-byte big-endian** uncompressed size (Niotso).

FullBuild0 survey (first 200 compressed blobs, 2026-09-07): every sample starts `10 FB` then a **3-byte big-endian** uncompressed size equal to index `mem_size`. No 4-byte size-prefix variant observed. See [testing.md](../testing.md).

## Opcodes (after the header)

| First byte | Width | Literals | Copy length | Distance |
| --- | --- | --- | --- | --- |
| `0xxxxxxx` | 2 | `b0 & 3` | `((b0 & 0x1C) >> 2) + 3` | `((b0 & 0x60) << 3) + b1 + 1` |
| `10xxxxxx` | 3 | `(b1 & 0xC0) >> 6` | `(b0 & 0x3F) + 4` | `((b1 & 0x3F) << 8) + b2 + 1` |
| `110xxxxx` | 4 | `b0 & 3` | `((b0 & 0x0C) << 6) + b3 + 5` | `((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1` |
| `111xxxxx` and `< 0xFC` | 1 | `((b0 & 0x1F) + 1) << 2` | 0 | 0 |
| `>= 0xFC` | 1 | `b0 & 3` | 0 | 0 | **stop** |

After each command, copy `literals` bytes from the stream, then if copy length > 0 copy from `output[pos - distance]`.

Writers **must emit a stop opcode**.

## Caps

- Uncompressed ≤ 256 MiB
- Ratio uncompressed/compressed ≤ 1024
- On violation → error, do not allocate

## Round-trip

Unchanged compressed resources: **copy on-disk bytes**. Re-encode only when the decompressed body changed.

## Synthetic

`fixtures/synthetic/refpack-hello.bin` - encode a short ASCII string with `10 FB` header + stop. Decoder tests use this, not FullBuild.

## Encoder (SXPE)

SXPE’s `refpack_compress` is a real LZ77/RefPack encoder (not literal-only):

- Header: `10 FB` + **3-byte big-endian** uncompressed size (same form the game/s3pe emit for TS3).
- Emits 2-/3-/4-byte backreference opcodes plus `0xE0`–`0xFB` long literals and a stop opcode (`≥ 0xFC`).
- Compatible with `refpack_decompress` (round-trip required). Output is **not** guaranteed bit-identical to EA Gimex / s3pe.
- Inputs larger than `0xFFFFFF` bytes are rejected (3-byte size field).

## Merge / unmerge copy-through

`resource.importPackage`, `package.unmerge`, `resource.exportToPackage`, and `Package::duplicate` copy **on-disk** blobs when present (including `compressed == 0xFFFF`), preserving `file_size` / `mem_size` / flags. Re-encode with `refpack_compress` only when writing new or edited uncompressed payloads with `compress=true`.

