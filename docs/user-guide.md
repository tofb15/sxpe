# SXPE user guide (GUI)

For non-builders who downloaded a portable zip, or builders running `sxpe_gui`.

**Related:** [workflows](workflows.md) · [building](building.md) · [CLI/MCP](cli-mcp.md) · [format specs](spec/README.md)

## Install / run

1. Prefer a **[GitHub Release](https://github.com/tofb15/sxpe/releases)** portable zip when available.
2. Unzip; keep folder layout intact.
3. Run **`SXPE.bat`** (Windows) or `sxpe_gui` from a build tree.

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

Add, Copy, Paste, Duplicate, Replace; Compressed / Deleted flags; Details; Copy resource key; Import (file / package / DBC); Export; Editors (STBL, Name map, XML, S3SA, CLIP, DDS, SNAP, VID); open in hex/text editor; Delete.

### Tools

FNV hash, **Compare packages**, **Find references**, **Scan folder**, **Inspect Sims3Pack**, **Un-merge package**, byte Search, **Validate**, Compact / save.

### Settings

Preview toggles (DDS / text / hex), DBC import checkpoint, bookmarks, built-in handlers, external programs (hex / text / **S3SA viewer**), Save settings.

### Help

**Contents**, **Check for update…**, About, Warranty, Licence.

## Check for update

**Help → Check for update…** calls:

`https://api.github.com/repos/tofb15/sxpe/releases/latest`

and compares the release `tag_name` to the running `SXPE` version (from CMake `PROJECT_VERSION`).

| Result | Meaning |
| --- | --- |
| Up to date | Your version matches the latest release tag |
| Newer available | Opens a link to that release; **nothing is downloaded automatically** |
| No releases yet | GitHub returned 404 / empty — page link still offered |
| Network error | Offline, firewall, or API failure — try again later |

Consent: SXPE never auto-installs or silently fetches zip assets. You choose whether to open the browser and download.

## Merge and un-merge

1. Drop multiple `.package` files (or import from package) to combine resources.
2. SXPE stores an **SXMM** merge manifest in the result.
3. **Tools → Un-merge package…** recreates source packages **only** when that manifest is present and valid. Merges from other tools are not reversible this way.

See [spec/merge-manifest.md](spec/merge-manifest.md).

## Editors

| Editor | Menu | Notes |
| --- | --- | --- |
| STBL | Resource → Editors → String table | String tables |
| NMAP | Resource → Editors → Name map | Creates NMAP if missing when renaming |
| XML / ITUN | Resource → Editors → XML | UTF-8 / UTF-16; size capped |
| S3SA | Export / Import DLL / View | Never `LoadLibrary`s game code; View uses your configured external program |
| CLIP | Export as new name | Hash helpers; no animation playback |
| DDS / SNAP / VID | Replace / export | Image / video payload helpers |

## Validate, compare, scan, Sims3Pack

- **Validate** — Tools → Validate. Report includes DIR / layout-lock notes. Prefer fixing issues before sharing a mod.
- **Compare** — Tools → Compare packages…. Diff by TGI+ordinal and payload hash.
- **Find references** — Tools → Find references…. Search for TGI references inside the open package.
- **Scan folder** — Tools → Scan folder…. Read-only hygiene over a Downloads-style tree (empty / corrupt / wrong-game / duplicate TGI sample). Does **not** auto-delete.
- **Sims3Pack** — list embedded packages and extract; no Store/DRM handling.

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

## Limits to remember

- Sims 3 only (v1). Unknown formats are refused.
- No third-party GUI plugin loading in this build.
- Keep backups. Compact/save rewrites packages; test in a copy first.
