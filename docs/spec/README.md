# SXPE format specs

Agent/CLI overview: [cli-mcp.md](../cli-mcp.md). Human GUI: [user-guide.md](../user-guide.md).

Implement codecs from these files plus cited public URLs. Do **not** treat s3pi/s3pe C# as the spec.

| File | Topic |
| --- | --- |
| [dbpf.md](dbpf.md) | Package header |
| [index.md](index.md) | TS3 index (version 3) |
| [dir.md](dir.md) | DIR resource `0xE86B1EEF` |
| [refpack.md](refpack.md) | RefPack/QFS compression |
| [stbl.md](stbl.md) | String tables |
| [xml.md](xml.md) | `_XML` / ITUN editor |
| [nmap.md](nmap.md) | Name map |
| [refs.md](refs.md) | REFS reference table |
| [s3sa.md](s3sa.md) | S3SA `073FAA07` (format + wrap/import gap vs s3pe) |
| [merge-manifest.md](merge-manifest.md) | SXPE merge manifest (`SXMM`) |
| [hashing.md](hashing.md) | FNV-1 and CLIP instance |
| [clip.md](clip.md) | CLIP metadata / safe fields / exportAs (#61) |
| [catalog.md](catalog.md) | Command catalog (CLI/MCP) |
| [sims3pack.md](sims3pack.md) | `.sims3pack` TS3Pack inspect (list/extract; no DRM) |
| [preview.md](../preview.md) | Inspector preview inventory (what can be shown, difficulty, priority) |
| [rcol.md](rcol.md) | RCOL/MATD summary + chunk replace |
| [preview-wave2.md](preview-wave2.md) | Wave 2 OBJD/CASP/CLIP/RCOL layout assumptions |
| [dds.md](dds.md) | DDS decode/replace format matrix (#55) |
| [tags.md](../tags.md) | What each Tag means and what bytes the resource holds |
| [neighborhood-layout.md](../neighborhood-layout.md) | `.nhd`/`.world`/`.dbc` layout lock: safe vs refused |

**TBC-game:** confirm on a local Steam install (`FullBuild0.package`, mmap in place). Optional harness: [testing.md](../testing.md). Do not copy EA files into git.

Synthetic bytes: `fixtures/synthetic/` (not EA content).
