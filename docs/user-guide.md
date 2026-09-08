# SXPE user guide (GUI)

For builders running `sxpe_gui` on **Windows or Linux**, or anyone using the portable folder. Project version is **0.7.0**. GUI menus/workflows are the same on both OSes; OS-specific notes appear only where needed below.

**Related:** [workflows](workflows.md) · [building](building.md) · [CLI/MCP](cli-mcp.md) · [format specs](spec/README.md)

## Install / run

1. **Linux CLI/MCP (published):** download `sxpe-*-linux-x64-cli.tar.gz` from **[Releases](https://github.com/tofb15/sxpe/releases)** when the tag workflow has attached it, or build locally with `./scripts/package-linux.sh --no-gui`.
2. **Windows GUI:** download `sxpe-0.7.0-windows-x64.zip` from **[v0.7.0](https://github.com/tofb15/sxpe/releases/tag/v0.7.0)**. Unzip; keep folder layout intact; run **`SXPE.bat`**. Local rebuild: `build.bat`, then `package.bat` / `scripts/package.ps1` ([building.md](building.md)).
3. **Linux GUI:** build with Qt 6 present ([building.md](building.md)), run `./build/sxpe_gui`, or `./scripts/package-linux.sh` (includes GUI in the tarball; **does not** vendor Qt libs — install Qt on the machine that runs it).
4. **From a build tree (either OS):** run `sxpe_gui` when Qt was found at configure time.

Drop one `.package` on the window to open it. Drop **several** to **merge** them into a new package (SXPE writes an `SXMM` manifest so un-merge can reverse SXPE merges only).

## Main window

- **Tabs** — one package (or session) per tab. Right-click a tab: save, close this / others / left / right, bookmark.
- **Resource grid** — virtualized list (Type, Group, Instance, Name, size, flags, …). Filter box narrows the list. Right-click headers to show/hide columns (same as **View → Columns**).
- **Inspector** — preview / hex / structured summary for the selection (DDS, text, OBJD/CASP/CLIP counts, etc.). Honesty: no full 3D mesh playback.
- **Status bar** — path, counts, and a **layout lock** badge for `.nhd` / `.world` / `.dbc`.

## Menus (summary)

### File

New, Open, Open read-only, Open Sims3Pack, Save / Save As / Save Copy As, Close, Recent, Bookmarks, Exit.

### Edit

Undo / Redo (session mutation stack), copy/save/float preview, open in text editor, Select All, Command palette (`Ctrl+K`).

### View

Column visibility for the resource list.

### Resource

Add, Copy, Paste, Duplicate, Replace; Compressed / Deleted flags; Details; Copy resource key; Import (file / into-this-package / DBC-equivalent); Export; Editors (STBL, Name map, XML, Catalog object, CAS part, REFS, Replace RCOL chunk, S3SA, CLIP metadata/export, DDS, SNAP, VID); open in hex/text editor; Delete.

### Tools

FNV hash, **Compare packages**, **Find references**, **Scan folder**, **Inspect Sims3Pack**, **Create Sims3Pack…** (limited packer), **Merge packages…** (Merge assistant), **Un-merge package**, byte Search, **Validate**, Compact / save.

### Settings

Preview toggles (DDS / text / hex), DBC import checkpoint, bookmarks, **Built-in handlers** (first-party types only; plugins permanently unsupported), external programs (hex / text / **S3SA viewer** — not DLL plugins), Save settings.

### Help

**Contents**, **Common tasks…** (links to workflows.md), **Check for update…**, About, Warranty, Licence.

## Check for update

**Help → Check for update…** (GUI) and `sxpe app checkUpdate` (CLI / MCP `app_checkUpdate`) share one bus command. Both call:

`https://api.github.com/repos/tofb15/sxpe/releases/latest`

and compare the release `tag_name` to the running `SXPE` version (from CMake `PROJECT_VERSION`).

| Result | Meaning |
| --- | --- |
| Up to date | Your version matches the latest release tag |
| Newer available | Opens a link to that release; **nothing is downloaded automatically** |
| Not found (404) | No public latest release (repo may be **private**, or none published). Set `SXPE_GITHUB_TOKEN` / `GITHUB_TOKEN`, or use `gh auth token`. Page link still offered. |
| Network error | Offline, firewall, or API failure — try again later |

Consent: SXPE never auto-installs or silently fetches zip assets. You choose whether to open the browser and download.

CLI:

```text
sxpe app checkUpdate
sxpe app checkUpdate --format text
```

## Merge and un-merge

1. **Tools → Merge packages…** opens the **Merge assistant**: choose a **folder** (or files) → preview **count and total size** → **Merge** into a **new untitled** package → optional **Validate after merge**. A **progress dialog** shows per-package status with **Cancel** (aborts and rolls back). SXPE writes an **SXMM** manifest and **strips** known leftover Sims3Pack manifests (`0x73E93EEB` instance 0) by default.
2. Drop several files → **Merge into new package** uses the same bus path (no folder preview step).
3. **Resource → Import → From package(s) into this package…** copies into the **open** tab (different intent). **As DBC into this package…** is the historical s3pe DBC-equivalent path — same bus merge/import, not a different file format.
4. **Tools → Un-merge package…** recreates source packages **only** when an SXMM manifest is present and valid. Merges from other tools are not reversible this way.
5. **Tools → Validate** summary highlights **conflict hotspots** (leftover manifests / duplicate TGIs).

Large CC sets: prefer modest batches; SXPE refuses oversized jobs with a clear `cap_exceeded` message instead of OOM. See [workflows.md](workflows.md#large-cc-batches-s3pe-oom-pain). CLI/MCP share the same bus command (`resource.importPackage`) including leftover / duplicate policies and progress events.

See [spec/merge-manifest.md](spec/merge-manifest.md).

## If you used s3pe before

| s3pe habit | SXPE equivalent |
| --- | --- |
| File open / save | **File** menu |
| Drop-merge / “combine CC” tutorials | **Tools → Merge packages…** (Merge assistant) or multi-file drop |
| Import as DBC / from package into current | **Resource → Import → As DBC…** / **From package(s)…** |
| Helpers wrapper | **Resource → Editors** |
| Auto Preview | **Inspector** (no full 3D mesh / CLIP play) |
| External programs | **Settings → External programs** |

First launch shows a one-time tip pointing at **Help → Common tasks** and the Merge assistant. Details: [workflows.md](workflows.md).

## Editors

| Editor | Menu | Notes |
| --- | --- | --- |
| STBL | Resource → Editors → String table | String tables |
| NMAP | Resource → Editors → Name map | Creates NMAP if missing when renaming |
| XML / ITUN | Resource → Editors → XML | UTF-8 / UTF-16; size capped |
| OBJD | Resource → Editors → Catalog object | Name/desc GUIDs, price, thumb IID |
| CASP | Resource → Editors → CAS part | Clothing type, age/gender, TGI refs |
| REFS | Resource → Editors → Reference table | TGI+aux entries and WORD indices |
| RCOL | Resource → Editors → Replace RCOL chunk… | Safe single-chunk replace (MODL/MLOD/GEOM/MATD); see [rcol.md](spec/rcol.md) |
| S3SA | Export / Import DLL / View | Never `LoadLibrary`s game code; View uses your configured external program |
| CLIP | Metadata… / Export as new name… | Safe fields + exportAs / batch CLI; no playback |
| DDS / SNAP / VID | Replace / export | Image / video payload helpers |

## Validate, compare, scan, Sims3Pack

- **Validate** — Tools → Validate. Report includes DIR / layout-lock notes and **conflict hotspots** (leftover manifests / duplicate TGIs). Prefer fixing issues before sharing a mod.
- **Compare** — Tools → Compare packages…. Diff by TGI+ordinal and payload hash.
- **Find references** — Tools → Find references…. Inbound (who points at the selection) or outbound (what this REFS/OBJK/VPXY/CASP points at).
- **Scan folder** — Tools → Scan folder…. Read-only hygiene over a Downloads-style tree (empty / corrupt / wrong-game / duplicate TGI sample). Does **not** auto-delete.
- **Sims3Pack** — list embedded packages and extract; no Store/DRM handling.

## Game / file locks

The Sims 3 (or another tool) may keep a `.package` open. Opening or saving that same path from SXPE can corrupt the package or make the game crash.

- **Open / Save:** if the OS reports a sharing violation or SXPE cannot take an exclusive lock, GUI and CLI show the same actionable bus error: **close the game or copy the file first** (not a raw I/O code).
- **Mods folder:** paths under `Documents/Electronic Arts/.../Mods` may also show a **warning** when SXPE opens the file but cannot take an exclusive lock — prefer working on a **copy** outside Mods, then replace the mod after the game is closed.
- CLI/MCP share these messages via the command bus (`package.open` / `package.save` / `package.saveAs`).

See [workflows](workflows.md#edit-packages-the-game-might-have-open).

## Neighborhood / world / DBC layout lock

`.nhd`, `.world`, and `.dbc` sessions are **layout-locked**:

- **Safe:** replace a resource payload in place if it still fits the existing hole.
- **Refused / greyed out:** add, delete, reorder, compact, create NMAP.

Details: [neighborhood-layout.md](neighborhood-layout.md).

## Keyboard shortcuts (common)

| Action | Shortcut |
| --- | --- |
| New / Open / Save / Close / Exit | Ctrl+N / O / S / W / Q |
| Undo / Redo | Ctrl+Z / Ctrl+Y |
| Command palette | Ctrl+K |
| Add resource | Ctrl+I |
| Copy / Paste / Duplicate | Ctrl+C / V / D |
| Copy resource key | Ctrl+Shift+C |
| Delete | Delete |
| Search | Ctrl+F |
| Select all | Ctrl+A |

## External programs (OS examples)

**Settings → External programs** configures hex / text / S3SA viewers. Commands use `{path}` substitution (not plugins; plugins are permanently unsupported — #60).

| Role | Windows example | Linux example |
| --- | --- | --- |
| Hex | `C:\Tools\hex.exe {path}` | `ghex {path}` or `okteta {path}` |
| Text | `notepad {path}` | `xdg-open {path}` or `nano {path}` |
| S3SA | `ilspy {path}` / `dnSpy {path}` | `ilspycmd {path}` (or any PE/.NET viewer you install) |

## Limits to remember

- Sims 3 only (v1). Unknown formats are refused.
- Third-party GUI plugins / DLL Handlers are **permanently unsupported** on **all** platforms (no plugin SDK, no surprise DLL execution). **Settings → Built-in handlers** lists first-party types only.
- Keep backups. Compact/save rewrites packages; test in a copy first.
