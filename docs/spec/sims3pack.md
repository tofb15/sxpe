# Sims3Pack (`.sims3pack`) — read-only inspect

**Scope:** list packaged payloads and extract them to a folder. **Not** Store download, **not** DRM/DBPP decryption, **not** full authoring.

Sources (public community docs only — do **not** copy EA or s3pe/s3pi proprietary tooling source):

- https://simswiki.info/wiki.php?title=Sims_3:SIMS3PACK
- Cross-check: https://simswiki.info/wiki.php?title=Sims_3:DBPF (embedded `.package` payloads are ordinary TS3 DBPF)

## Layout (SimsWiki)

Three sections, contiguous:

1. **File header** (little-endian)
2. **XML document** (`XMLLength` bytes)
3. **Archive** (raw concatenated payloads; offsets in XML are relative to archive start)

### Header

| Field | Type | Notes |
| --- | --- | --- |
| `StrLength` | `uint32` | Length of signature string; **7** for `TS3Pack` |
| `Signature` | `char[StrLength]` | ASCII `TS3Pack` |
| version word | `uint16` | Observed `0x0101` (endian-neutral for this value) |
| `XMLLength` | `uint32` | Size of the following XML section in bytes |

Absolute archive offset = `4 + StrLength + 2 + 4 + XMLLength`.

### XML (best-effort)

Root `<Sims3Package Type="…" SubType="…">` with metadata elements (`ArchiveVersion`, `DisplayName`, `Description`, `PackageId`, …) and zero or more `<PackagedFile>` blocks:

| Element | Meaning |
| --- | --- |
| `Name` | Payload file name (often `*.package` or `*.png`) |
| `Length` | Payload byte length |
| `Offset` | Offset from **start of archive section** |
| `Crc` | CRC string (algorithm **unknown** — recorded, not verified) |
| `Guid` | Resource GUID string |
| `ContentType` | Resource kind string |
| `metatags` | Ignored in v1 |

SXPE scrapes these tags with a lightweight string walk (no XML DOM library). Unusual nesting or unexpected element names may yield **zero entries** even when the header is valid — see limitations.

### Archive

Each `PackagedFile` names a slice `[archive_offset + Offset, Length)`. Embedded `.package` files are standard TS3 DBPF (`DBPF` major 2); open them with `package.open` after extract.

## What SXPE implements

Bus (CLI / MCP / GUI parity):

| id | role |
| --- | --- |
| `sims3pack.info` | Header + metadata + entry count |
| `sims3pack.list` | Entry table (`index`, `name`, `length`, `offset`, …) |
| `sims3pack.extract` | Write one entry (`index`) or all into `outDir` |

CLI: `sxpe sims3pack info|list|extract --path FILE.sims3pack …`  
GUI: **Tools → Inspect Sims3Pack…** (and **File → Open Sims3Pack…**).

## Caps / safety

- Open cap 4 GiB; XML section ≤ 16 MiB; ≤ 50 000 `PackagedFile` rows
- Extract per-entry ≤ 256 MiB (`kMaxResourceBytes`)
- Paths refuse `..`; `SXPE_ALLOW_PATHS` honored via bus `check_path`
- Extract writes **basename only** (no nested paths from `Name`)
- Overwrite requires `force`
- **No** DBPP / Store decryption

## Limitations (honest)

- Only the **TS3Pack-framed** layout from SimsWiki is supported. Bare XML + trailing DBPF, or a file that starts with `DBPF`/`DBPP`, is refused with a clear error.
- CRC algorithm is unknown; values are listed but not validated.
- XML scrape is best-effort; CDATA and common entities (`&amp;` …) are handled; full XML Schema fidelity is not claimed.
- Synthetic fixture only — do not commit EA Store packs.

## Synthetic fixture

`fixtures/synthetic/minimal.sims3pack` — crafted TS3Pack + XML + one embedded synthetic DBPF (`Hello SXPE\n` resource). Regenerate via `python fixtures/synthetic/make_synthetic.py`.
