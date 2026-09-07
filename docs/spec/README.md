# SXPE format specs (M1)

Implement codecs from these files plus cited public URLs. Do **not** treat s3pi/s3pe C# as the spec.

| File | Topic |
| --- | --- |
| [dbpf.md](dbpf.md) | Package header |
| [index.md](index.md) | TS3 index (version 3) |
| [dir.md](dir.md) | DIR resource `0xE86B1EEF` |
| [refpack.md](refpack.md) | RefPack/QFS compression |
| [stbl.md](stbl.md) | String tables |
| [nmap.md](nmap.md) | Name map |
| [s3sa.md](s3sa.md) | S3SA `073FAA07` (format + wrap/import gap vs s3pe) |
| [merge-manifest.md](merge-manifest.md) | SXPE merge manifest (`SXMM`) |
| [hashing.md](hashing.md) | FNV-1 and CLIP instance |
| [catalog.md](catalog.md) | Command catalog (CLI/MCP) sketch |
| [preview.md](../preview.md) | Inspector preview inventory (what can be shown, difficulty, priority) |
| [tags.md](../tags.md) | What each Tag means and what bytes the resource holds |

**TBC-game:** confirm on a local Steam install (`FullBuild0.package`, mmap in place). Optional harness: [testing.md](../testing.md). Do not copy EA files into git.

Synthetic bytes: `fixtures/synthetic/` (not EA content).
