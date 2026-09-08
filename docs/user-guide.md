# SXPE user guide (GUI)

Install and “what is SXPE”: **[README](../README.md)**. This page assumes `sxpe_gui` is running (portable zip or a local build). Menus are the same on Windows and Linux.

Recipes: [workflows](workflows.md). Scripts/agents: [cli-mcp](cli-mcp.md). Formats: [spec/](spec/README.md).

## Main window

- **Tabs** - one package (session) per tab. Right-click a tab: save, close this / others / left / right, bookmark.
- **Resource grid** - virtualized list (Type, Group, Instance, Name, size, flags). Filter box; right-click headers to show/hide columns (**View → Columns**).
- **Inspector** - preview / hex / structured summary (DDS, text, OBJD/CASP/CLIP counts). No full 3D mesh playback.
- **Status bar** - path, counts, and a **layout lock** badge for `.nhd` / `.world` / `.dbc`.

Drop one `.package` to open it. Drop **several** to merge into a new package (SXMM manifest; un-merge reverses **SXPE** merges only).

## Menus

### File

New, Open, Open read-only, Open Sims3Pack, Save / Save As / Save Copy As, Close, Recent, Bookmarks, Exit.

### Edit

Undo / Redo (session stack), copy/save/float preview, open in text editor, Select All, Command palette (`Ctrl+K`).

### View

Column visibility for the resource list.

### Resource

Add, Copy, Paste, Duplicate, Replace; Compressed / Deleted flags; Details; Copy resource key; Import (file / into-this-package / DBC-equivalent); Export; Editors (STBL, Name map, XML, Catalog object, CAS part, REFS, Replace RCOL chunk, S3SA, CLIP metadata/export, DDS, SNAP, VID); open in hex/text editor; Delete.

### Tools

FNV hash, Compare packages, Find references, Scan folder, Inspect Sims3Pack, Create Sims3Pack… (limited packer), **Merge packages…**, Un-merge package, byte Search, Validate, Compact / save.

### Settings

Preview toggles (DDS / text / hex), DBC import checkpoint, bookmarks, **Built-in handlers** (first-party types only), external programs (hex / text / S3SA viewer - not DLL plugins), Save settings.

### Help

Contents, Common tasks… (opens [workflows](workflows.md)), **Check for update…**, **Feedback…** (opens [GitHub Issues](https://github.com/tofb15/sxpe/issues)), About, Warranty, Licence.

## If you used s3pe before

SXPE is a separate program. Jobs map roughly:

| s3pe | SXPE |
| --- | --- |
| File open / save | **File** menu |
| Drop-merge / “combine CC” | **Tools → Merge packages…** or multi-file drop |
| Import as DBC / from package | **Resource → Import → As DBC…** / **From package(s)…** |
| Helpers | **Resource → Editors** |
| Auto Preview | **Inspector** (no full 3D / CLIP play) |
| External programs | **Settings → External programs** (`{path}`) |

First launch shows a one-time tip for **Help → Common tasks** and the Merge assistant. Un-merge needs an **SXMM** manifest from an SXPE merge.

## Merge and un-merge

**Tools → Merge packages…** is the Merge assistant (folder or files → count/size preview → merge into a **new untitled** package → optional validate). Progress dialog **Cancel** rolls back. Default: write **SXMM**, strip leftover Sims3Pack manifests (`0x73E93EEB` instance 0).

- Multi-file drop → **Merge into new package** - same bus, no folder preview.
- **Resource → Import → From package(s)…** copies into the **open** tab (different intent).
- **As DBC…** is the historical s3pe name for the same import path, not a different format.
- **Un-merge** recreates sources **only** when SXMM is present.
- **Validate** lists **conflict hotspots** (leftover manifests / duplicate TGIs).

Large CC: SXPE refuses oversized jobs (`cap_exceeded`) instead of OOM. Step-by-step and CLI: [workflows.md](workflows.md#merge-custom-content-into-one-package). Spec: [merge-manifest.md](spec/merge-manifest.md).

## Editors

| Editor | Menu | Notes |
| --- | --- | --- |
| STBL | Resource → Editors → String table | String tables |
| NMAP | Resource → Editors → Name map | Creates NMAP if missing when renaming |
| XML / ITUN | Resource → Editors → XML | UTF-8 / UTF-16; size capped |
| OBJD | Resource → Editors → Catalog object | Name/desc GUIDs, price, thumb IID |
| CASP | Resource → Editors → CAS part | Clothing type, age/gender, TGI refs |
| REFS | Resource → Editors → Reference table | TGI+aux entries and WORD indices |
| RCOL | Resource → Editors → Replace RCOL chunk… | One chunk (MODL/MLOD/GEOM/MATD); [rcol.md](spec/rcol.md) |
| S3SA | Export / Import DLL / View | Never `LoadLibrary`s game code; View uses your external program |
| CLIP | Metadata… / Export as new name… | Safe fields + exportAs; no playback |
| DDS / SNAP / VID | Replace / export | Image / video payload helpers |

## Validate, compare, scan, Sims3Pack

- **Validate** - DIR / layout-lock + conflict hotspots. Prefer a clean report before sharing.
- **Compare** - TGI+ordinal and payload hash.
- **Find references** - inbound (who points at the selection) or outbound (REFS/OBJK/VPXY/CASP).
- **Scan folder** - read-only Downloads-style hygiene. Does **not** auto-delete.
- **Sims3Pack** - list/extract/limited pack; no Store/DRM.

## Check for update

**Help → Check for update…**, CLI `sxpe app checkUpdate`, and MCP `app_checkUpdate` share `app.checkUpdate`. They GET `https://api.github.com/repos/tofb15/sxpe/releases/latest` and compare `tag_name` to this build. **Nothing is downloaded.**

If `/releases/latest` returns **404** (common when the newest tag is only a GitHub **pre-release** / Beta, because GitHub excludes prereleases from “latest”), SXPE falls back to listing recent releases and picks the newest **non-draft** tag, **including prereleases**.

| Result | Meaning |
| --- | --- |
| Up to date | Running version matches the newest published tag (stable or Beta) |
| Newer available | Link to that release; you choose whether to install |
| Not found | No visible non-draft release (repo may be **private**, or none published yet). Set `SXPE_GITHUB_TOKEN` / `GITHUB_TOKEN`, or use `gh auth token`. |
| Network error | Offline, firewall, or API failure |

## Game / file locks

Editing a `.package` while The Sims 3 has it open can corrupt the file or crash the game.

- Sharing violation / exclusive lock failure → **close the game or copy the file first** (same text in GUI, CLI, MCP).
- Paths under `Documents/Electronic Arts/.../Mods` may add `warnings[]` when SXPE cannot take an exclusive lock - work on a copy, then replace after the game is closed.

Details: [workflows](workflows.md#edit-packages-the-game-might-have-open).

## Neighborhood / world / DBC layout lock

`.nhd`, `.world`, and `.dbc` are **layout-locked**:

- **Safe:** replace a payload in place if it still fits the hole.
- **Refused:** add, delete, reorder, compact, create NMAP.

Full rules: [neighborhood-layout.md](neighborhood-layout.md).

## Keyboard shortcuts

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

## External programs

**Settings → External programs** - `{path}` substitution, not plugins ([#60](https://github.com/tofb15/sxpe/issues/60)).

| Role | Windows | Linux |
| --- | --- | --- |
| Hex | `C:\Tools\hex.exe {path}` | `ghex {path}` / `okteta {path}` |
| Text | `notepad {path}` | `xdg-open {path}` / `nano {path}` |
| S3SA | `ilspy {path}` / `dnSpy {path}` | `ilspycmd {path}` |

## Known limits

Same list as the [README](../README.md#what-this-version-does-not-do-yet) (**Known limits** today): Sims 3 only; no full 3D/CLIP play; no Store/DRM Sims3Pack; layout-locked neighborhood files; no third-party plugins. Keep backups.
