# SXPE workflows

Task-oriented recipes. Pair with the [user guide](user-guide.md) or [CLI/MCP](cli-mcp.md).

## Merge custom content into one package

1. Collect the `.package` files you want to combine (work on **copies**).
2. GUI — pick **one** clear path:
   - **Tools → Merge packages…** (or drop several files → **Merge into new package**) — new untitled package + **SXMM** manifest. Prefer this for “combine CC into one file”.
   - **Resource → Import → From package(s) into this package…** — copy into the **open** tab (no SXMM unless you pass `writeMergeManifest` via CLI/MCP).
   - **Resource → Import → As DBC into this package…** — **DBC-equivalent** of the same bus command (`resource.importDbc`); same copy-through / caps / leftover strip / duplicate policy as `importPackage`. Historical s3pe name; not a different format.
3. Save the merged result. Drop-merge / Tools → Merge write **SXMM** so un-merge can reverse SXPE merges only.
4. Tools → **Validate** — summary lists **conflict hotspots** (leftover Sims3Pack manifests `0x73E93EEB:0`, duplicate TGIs).
5. To split again later: Tools → **Un-merge package…** (SXPE merges only).

CLI/agents:

```text
sxpe resource importPackage --session s-1 --progress \
  --paths '["a.package","b.package"]' --force --write-merge-manifest true \
  --leftover-manifest-policy strip --duplicate-tgi-policy force
```

Un-merge with `package.unmerge` when an SXMM is present. See [merge-manifest.md](spec/merge-manifest.md) for leftover allowlist + duplicate policy.

### Large CC batches (s3pe OOM pain)

Community merges of ~150–250 MiB+ / dozens of packages often OOMed or left giant temps in s3pe. SXPE:

| Guard | Default | Override |
| --- | --- | --- |
| `maxPackages` | 500 | lower for cautious batches |
| `maxTotalBytes` | 2 GiB | sum of **input** file sizes |
| `maxResources` | 200 000 | refuse mid-job before RAM blow-up |

Clear `cap_exceeded` errors say **split the job** — prefer batches of **~20–40 packages** or **under ~100–150 MiB** of inputs when machines are tight (same advice as old s3pe guides, with hard caps instead of mystery crashes).

**Explicit checkpoint (optional):** pass `checkpointPath` + `checkpointBetweenPackages: true` to save after each successful source. This is **not** autosave — you choose the path. Checkpoint flushes the session to disk (unique `*.sxpe-tmp-*` temps, deleted on failure) so peak RAM stays closer to one package’s overrides. MCP clients can send `_meta.progressToken` to receive `notifications/progress`. Cancel: GUI **Cancel** aborts and rolls back; CLI **Ctrl+C** sets cooperative cancel (same bus `request_cancel`). Mid-flight MCP `notifications/cancelled` is not wired yet (sync tools/call).

Synthetic stress coverage: many tiny packages under a temp dir (no EA files) — see `tests/commands_test.cpp` large-merge cases.

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
2. Read `summary[]` / dialog lines for DIR policy, layout lock, **conflict hotspots**, and structural issues.
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
2. If newer: open the release page and install the asset that matches your platform (v0.6.0 publishes a Linux CLI+MCP tarball; Windows portable zip is local-build until [#54](https://github.com/tofb15/sxpe/issues/54)). Keep backups of your work files.
3. SXPE never auto-downloads updates.
