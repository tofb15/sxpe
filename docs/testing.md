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
5. Optional `-Payloads`: SHA-256 of **uncompressed** bodies (via `resource.export` to a temp dir) for packages under 32 MiB by default (`-PayloadMaxBytes`). Slow on huge packages; skip FullBuild. Do not commit hash logs that embed EA content.

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

## Large-merge stress (synthetic)

`sxpe_commands_test` builds many tiny DBPF packages under `%TEMP%/sxpe-m3` (or `/tmp`) and merges them via `resource.importPackage`. Cases cover: clean multi-package merge (no leftover `*.sxpe-tmp-*`), mid-merge failure leaving no orphan multi-GB temps, `maxPackages` / `maxTotalBytes` refusals, progress events, explicit `checkpointBetweenPackages`, and cancel/rollback (`request_cancel` / `cancel_check`) for issue #66. No EA/CC bytes. Issue #64: synthetic packages plant leftover manifest TGI `0x73E93EEB` instance 0 and duplicate TGIs — strip/warn/keep and force/skip/fail policies are asserted; `package.validate` lists conflict hotspots.


## Huge-package open performance (issue #65)

SXPE opens DBPF via **mmap + index parse only**. Payloads are not decoded until a
command/UI action asks. Virtualized GUI rows and CLI `resource list --limit` / `--cursor`
must stay **O(index)** (and snappy when paging), not O(total payload bytes).

### Caps (see `include/sxpe/core/caps.hpp`)

| Cap | Default | Role |
| --- | --- | --- |
| `kMaxMapBytes` | 4 GiB | Refuse mmap above this |
| `kMaxIndexEntries` | 500 000 | Refuse absurd indexes |
| `kOpenReadOnlyBytes` | 256 MiB | `package.open` with `writable:true` demotes to read-only unless `forceWritable` |
| `kMaxLivePreviewBytes` | 8 MiB | Hex/text/preview refuse compressed decode above this |
| `kMaxResourceBytes` | 256 MiB | Refuse **full** uncompressed decode; package **open/list still succeed** |
| `kMaxNmapIndexBytes` | 16 MiB | Skip huge NMAP bodies when building the name cache for list/UI |
| `kLargeIndexBenchmarkEntries` | 25 000 | Synthetic CI benchmark size |
| `kLargeIndexOpenBudgetMs` | 2000 | CI-class budget for open + paged list on that synthetic package |

### Synthetic CI (`sxpe_commands_test` / `sxpe_dbpf_io_test`)

Builds packages under the process temp dir (e.g. `%TEMP%/sxpe-m3/huge-65` or `/tmp/...`):

1. **Large index** — 25 000 empty rows; `package.open` reports `openMs`; must be ≤ 2000 ms on CI-class hardware; two `resource.list` pages of 100 stay within the same budget (name cache, no payload walk).
2. **Huge resource** — index row with `memSize` above the live-preview cap; open/list OK; `hex.get` / `text.get` / `resource.read includePayload` return `cap_exceeded` without hanging.
3. **Oversize decode cap** — `memSize > kMaxResourceBytes`; open still OK; `Package::uncompressed` refuses.
4. **Auto read-only** — sparse file ≥ `kOpenReadOnlyBytes`; writable open sets `openedReadOnlyDueToSize` and `readWrite:false`; `forceWritable:true` keeps writable.

No EA/FullBuild bytes in git or CI.

### FullBuild expectations (honest, opt-in local)

Surveyed TBC `FullBuild0.package` (~1.0 GiB, ~102 127 index rows) — see evidence table above.
On developer hardware SXPE should:

- **Open (mmap + index)** in on the order of **a few seconds**, not minutes; never by decompressing the whole file.
- **List / GUI grid** from index metadata (+ optional NMAP name cache if under `kMaxNmapIndexBytes`).
- **Default read-only** (auto-demote above 256 MiB) so a FullBuild tab does not hold a writable mapping.
- **Selecting a multi‑hundred‑MB resource** shows a clear “preview refused / cap” message — no UI hang from full RefPack decode.

Do **not** copy FullBuild into `fixtures/` or CI. Use `SXPE_GAME_DIR` + mmap in place via the round-trip harness for local checks; skip `-Payloads` on FullBuild.

## CI

GitHub Actions must stay synthetic-only. Do not upload `fixtures/local/` or game installs.


## File-lock detection (issue #68)

`sxpe_dbpf_io_test` and `sxpe_commands_test` simulate a locked file with Windows share-mode `0` or Linux `flock(LOCK_EX|LOCK_NB)` on a **synthetic** package under the temp dir — no EA/CC bytes. Asserts actionable *close the game or copy the file first* messages and optional Mods-path `warnings[]` on `package.open`.
