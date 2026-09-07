# Local round-trip testing (opt-in)

CI (`ctest`) uses **synthetic** fixtures only (`fixtures/synthetic/`). This document is the optional
harness for real FullBuild / custom-content packages that **must never be committed**.

## Gitignored local paths

| Path / env | What belongs there |
| --- | --- |
| `fixtures/local/` | Copies **you** make for experiments. Already gitignored. |
| `*.package`, `*.dbc`, `*.world`, `*.nhd` | Ignored anywhere in this repo. |
| `$env:TEMP\sxpe-roundtrip\` | Work copies created by `scripts/roundtrip.ps1`. Not in git. |
| `SXPE_GAME_DIR` | Steam/EA install root (read-only oracle). Do not copy FullBuild here into git. |
| `SXPE_USER_DIR` | Documents `Electronic Arts\The Sims 3` (CC, saves). |
| `SXPE_ROUNDTRIP_PACKAGES` | Optional `;`-separated extra package paths. |

Workspace `LOCAL.md` (not in this repo) may list this machine’s Steam paths. The harness never
writes those paths into committed files.

Typical Steam layout (relative to `SXPE_GAME_DIR`):

```text
Game\Bin\Misc\fallback.package          tiny real package first
GameData\Shared\DeltaPackages\p20\DeltaBuild_p20.package
GameData\Shared\Packages\FullBuild0.package   mmap in place; do not copy
```

## Harness

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/roundtrip.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/roundtrip.ps1 -Packages "D:\path\to\mod.package"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/roundtrip.ps1 -GameDir $env:SXPE_GAME_DIR
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/roundtrip.ps1 -Tbc
```

Checklist the script runs per package (work copy under `%TEMP%`, never the install tree):

1. `package.info` — `indexCount`, `compressedCount`, `deletedCount`, `dirPresent`, `major`/`minor`/`indexVersion`
2. `package.saveAs` to a temp path (`force`) — compact/rebuild
3. Reopen the copy; compare the fields above
4. `resource.list` (paged) — every TGI + `memSize` + compressed flag must match (order-independent)
5. Optional `-Payloads`: SHA-256 of uncompressed bodies for packages under 32 MiB (skip FullBuild)

Exit code 0 = all compared packages matched. Failures print the field diffs. The original install
files are not written.

M5.1 bar: run this on ~10 popular CC packages you own plus `fallback.package`. Keep the log
local; do not paste EA bytes into issues.

## TBC-game evidence (2026-09-07)

Surveyed **in place** (mmap/read; no copies into git): Steam `fallback.package` (184 B, 2 rows),
`DeltaBuild_p20.package` (14 rows), `FullBuild0.package` (1 027 971 289 B, 102 127 rows).

| Spec item | Result | Spec |
| --- | --- | --- |
| Header `unknown1` / `unknown2` / `unknown3` / `unknown4` | All zeros | [dbpf.md](spec/dbpf.md) |
| Index position | `index_pos + index_size == file size` (EOF) | [dbpf.md](spec/dbpf.md) |
| FileSize high bit | Set on **every** surveyed row; length = low 31 bits | [index.md](spec/index.md) |
| Uncompressed `file_size` vs `mem_size` | Equal (masked) on all 10 152 raw FullBuild0 rows | [index.md](spec/index.md) |
| CompressedFlags high 16 (`unknown2`) | Always `1` on surveyed EA rows — **not** a deleted flag | [index.md](spec/index.md) |
| DIR `0xE86B1EEF` | **Absent** from FullBuild0 / fallback / DeltaBuild_p20 | [dir.md](spec/dir.md) |
| RefPack magic | First 200 FullBuild0 compressed blobs: `10 FB` + 3-byte BE size == `mem_size`. No 4-byte prefix. | [refpack.md](spec/refpack.md) |

Still open (follow-ups, not blocking this harness):

- On-disk **deleted**: none. Session flag; save omits the row (confirmed: no trash index, no `0xFFE0`, group high byte = EP flags). See [index.md](spec/index.md).
- DIR: **absent** from EA TS3. Record layout (when a tool writes one) is 20-byte TGI64 + mem_size. See [dir.md](spec/dir.md).
- NMAP name encoding beyond ASCII/UTF-8 without NUL.
- FNV Unicode rules beyond lowercase ASCII.

## CI

GitHub Actions must stay synthetic-only. Do not upload `fixtures/local/` or game installs.
