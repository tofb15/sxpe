# SXPE workflows

Task-oriented recipes. Pair with the [user guide](user-guide.md) or [CLI/MCP](cli-mcp.md).

## Merge custom content into one package

1. Collect the `.package` files you want to combine (work on **copies**).
2. GUI: drop all of them onto SXPE (or Resource → Import → From package(s)…).
3. Save the merged result. SXPE embeds an **SXMM** manifest.
4. To split again later: Tools → **Un-merge package…** (SXPE merges only).

CLI/agents: use import/merge bus commands; un-merge with `package.unmerge` when an SXMM is present.

## Strip thumbnail resources (THUM)

1. Open the package.
2. Filter the resource list for thumbnail types / names you intend to remove (confirm TGI against your modding notes).
3. Select → Resource → **Delete** (or Delete key).
4. Tools → **Validate**, then Save.

CLI: `resource.list` to find rows, then `resource.delete` with the resourceId. Always validate before sharing.

## Scripting DLL loop (S3SA)

1. Export: Resource → Editors → **Export S3SA as DLL…** (or `s3sa.exportDll`).
2. Edit the DLL in your usual toolchain (outside SXPE).
3. Import: **Import DLL into S3SA…** (`s3sa.importDll`) — wraps as community S3SA; does not load the DLL.
4. Optional: **View S3SA…** opens an external viewer configured under Settings → External programs.
5. Validate and test in-game on a copy of the package.

## Validate before you share

1. Tools → **Validate** (or `package.validate`).
2. Read `summary[]` / dialog lines for DIR policy, layout lock, and structural issues.
3. Fix, re-validate, save.
4. Optional: Tools → **Compare packages…** against the previous good version.

## Folder hygiene (Downloads)

1. Tools → **Scan folder…** and point at your Downloads (or Mods) tree.
2. Review empty / corrupt / wrong-game / duplicate-TGI hits.
3. Open flagged packages from the dialog; SXPE does **not** auto-delete anything.

CLI: `sxpe folder scan --path … --format json`.

## Inspect a Sims3Pack

1. File → **Open Sims3Pack…** or Tools → **Inspect Sims3Pack…**.
2. List embedded packages; extract selected entries to a folder.
3. Open extracted `.package` files in SXPE as usual.

No Store/DRM unpacking — unsupported packs are refused or reported clearly.

## Neighborhood / world edit (limited)

1. Open `.nhd` / `.world` / `.dbc` — expect a layout-lock badge.
2. Only replace payloads that fit existing holes.
3. If you need structural edits, work in a normal `.package` workflow or external tools designed for world editing; SXPE will refuse add/delete/compact here.

See [neighborhood-layout.md](neighborhood-layout.md).

## Check for a new SXPE build

1. Help → **Check for update…**, or open [Releases](https://github.com/tofb15/sxpe/releases).
2. If newer: download the portable zip yourself, unzip beside or over your previous folder (keep backups of your work files — not the app folder).
3. SXPE never auto-downloads updates.
