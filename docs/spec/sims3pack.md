# Sims3Pack (`.sims3pack`) — inspect + limited pack

**Scope:** list / extract packaged payloads, and **limited authoring** (`sims3pack.pack`) from a folder of `.package` files plus a metadata XML subset. **Not** Store download/upload, **not** DRM/DBPP decryption, **not** monetization, **not** full parity with legacy Store pack tools.

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
| version word | `uint16` | Observed / written `0x0101` |
| `XMLLength` | `uint32` | Size of the following XML section in bytes |

Absolute archive offset = `4 + StrLength + 2 + 4 + XMLLength`.

### XML (best-effort)

Root `<Sims3Package Type="…" SubType="…">` with metadata elements (`ArchiveVersion`, `DisplayName`, `Description`, `PackageId`, …) and zero or more `<PackagedFile>` blocks:

| Element | Meaning |
| --- | --- |
| `Name` | Payload file name (often `*.package` or `*.png`) |
| `Length` | Payload byte length |
| `Offset` | Offset from **start of archive section** |
| `Crc` | CRC string (algorithm **unknown** — recorded / written as zeros, not verified) |
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
| `sims3pack.extract` | Write one entry (`index`) into `outDir` |
| `sims3pack.pack` | Limited authoring: pack `sourceDir` `*.package` → `path` |

CLI:

```text
sxpe sims3pack info|list|extract --path FILE.sims3pack …
sxpe sims3pack pack --source-dir DIR --path OUT.sims3pack [--display-name …] [--meta-xml …] --force
```

GUI: **Tools → Inspect Sims3Pack…**, **Tools → Create Sims3Pack…**, and **File → Open Sims3Pack…**.

### Pack inputs (`sims3pack.pack`)

| Arg | Required | Notes |
| --- | --- | --- |
| `path` | yes | Output `.sims3pack` |
| `sourceDir` | yes | Non-recursive; all `*.package` (sorted by name) |
| `metaXml` | no | Optional metadata XML subset (`Sims3Package` Type/SubType + DisplayName / Description / PackageId / ArchiveVersion) |
| `displayName` / `description` / `packageId` / `packageType` / `packageSubType` / `archiveVersion` | no | Override metaXml / defaults |
| `force` | for overwrite | Refuses existing dest without it |
| `dryRun` | no | Lists would-be package names without writing |

Defaults when omitted: `Type=Object`, `SubType=0x00000000`, `ArchiveVersion=1.4`, `DisplayName` = first package basename, `PackageId` = generated placeholder, `Crc` = `00000000`, `Guid` = placeholder per index.

## Caps / safety

- Open / pack cap 4 GiB; XML section ≤ 16 MiB; ≤ 50 000 `PackagedFile` rows
- Extract / pack per-entry ≤ 256 MiB (`kMaxResourceBytes`)
- Paths refuse `..`; `SXPE_ALLOW_PATHS` honored via bus `check_path`
- Extract writes **basename only** (no nested paths from `Name`)
- Pack refuses duplicate basenames; overwrite requires `force`
- **No** DBPP / Store decryption or upload

## What we will not support (documented limits)

- EA Store upload, DRM, monetization, DBPP
- Full Store packer feature parity (LocalizedNames trees, rich `metatags`, thumbnail pipelines, dependency graphs)
- Verifying or computing the real PackagedFile CRC (algorithm unknown publicly)
- Bare XML + trailing DBPF (non-TS3Pack frame)
- Recursive multi-folder layouts or arbitrary non-`.package` payloads in the folder collector (explicit future extension)

## Limitations (honest)

- Only the **TS3Pack-framed** layout from SimsWiki is supported. Bare XML + trailing DBPF, or a file that starts with `DBPF`/`DBPP`, is refused with a clear error.
- CRC algorithm is unknown; values are listed / written as zeros but not validated.
- XML scrape is best-effort; CDATA and common entities (`&amp;` …) are handled; full XML Schema fidelity is not claimed.
- Pack is incremental / limited: enough for synthetic round-trips and simple CC packaging — not a Store authoring studio.
- Synthetic fixtures only — do not commit EA Store packs.

## Synthetic fixture

`fixtures/synthetic/minimal.sims3pack` — crafted TS3Pack + XML + one embedded synthetic DBPF (`Hello SXPE\n` resource). Regenerate via `python fixtures/synthetic/make_synthetic.py`.

Round-trip coverage: `tests/sims3pack_test.cpp` packs a folder containing that extracted `.package`, then list + extract and checks the payload.
