# Version numbers

SXPE versions are **`MAJOR.MINOR.PATCH`**. GitHub tags are **`vMAJOR.MINOR.PATCH`**. Help → Check for update compares those numbers.

This page is the **only** policy for *when* to change the number and *by how much*. Where the number lives: [CONTRIBUTING.md](../CONTRIBUTING.md#version). How to tag and zip: [building.md](building.md#releases). Notes file: [releases/TEMPLATE.md](releases/TEMPLATE.md).

## When to bump

**Bump once per GitHub Release, in the release-prep change.** Ordinary feature and bugfix PRs keep the last shipped `PROJECT_VERSION`.

| Situation | Bump? |
| --- | --- |
| Merge a feature or fix to `dev` | No |
| Docs that live only on GitHub (README, this page, workflows.md) | No, until the next binary release |
| Re-run `release.yml` on an existing tag (`workflow_dispatch`) | No. Same tag, same number |
| Cut a new GitHub Release people can download | **Yes.** Pick PATCH, MINOR, or MAJOR from the table below using *everything since the last tag* |
| Hotfix a shipped release while `dev` already moved on | Yes. Patch the old line (see [Hotfixes](#hotfixes)) |

Why not bump on every PR: Check for update treats `PROJECT_VERSION` as what this binary is. If `dev` says `0.8.0` while the latest Release is still `v0.7.0`, a local build reports it is newer than GitHub even though nothing was published. Keep the number at the last tag until you intend to publish.

## How much (look at git since the last tag)

Apply **one** bump: the **highest** row that matches. Ten bugfixes plus one new command is still **one MINOR**, not MINOR plus PATCH. Do not skip numbers because a release feels big (`0.7.0` → `0.8.0`, not `0.9.0`).

While **MAJOR is 0** (public Beta, not 1.0):

| Bump | New version from 0.7.0 | When this is the highest match |
| --- | --- | --- |
| **PATCH** | `0.7.1` | Same capabilities. Fixes, polish, performance, tests, CI, in-app copy, GUI layout that does not add a new thing people can do. |
| **MINOR** | `0.8.0` | New capability, **or** a breaking CLI / MCP / bus change (allowed in 0.x), **or** a new kind of download artifact. |
| **MAJOR** | `1.0.0` | Leave Beta on purpose (see [1.0.0](#100-leave-beta)). Not used for a normal 0.x break. |

After **1.0.0**:

| Bump | When |
| --- | --- |
| **PATCH** | Bugfix, polish, docs in the binary, performance. No new capability. No break. |
| **MINOR** | New capability. Old CLI flags, MCP tools, bus ids, and save formats still work. |
| **MAJOR** | Break CLI / MCP / bus, or on-disk format that older SXPE or the game cannot read. |

If two rows could apply, take the **higher** one. If nothing since the last tag would change a user’s download, **do not tag**.

## What counts as what

Think about the **person running the app**, scripts talking to the bus, and **bytes on disk**. `schemaVersion` on the JSON envelope is a different number ([Bus envelope](#bus-envelope-schemaversion)).

### PATCH

- Crash, wrong result, or corrupt-looking save that you fix without adding a feature.
- GUI honesty: silent no-op, clipped column, wrong menu name, tooltip, enablement.
- Faster open/merge/scan with the same flags and the same answers.
- Tests and CI only.
- Strings and Help Contents **inside the binary**.
- Dependency pin that only rebuilds SXPE, unless it changes behavior (then judge the behavior).

Examples that would have been PATCH on top of 0.7.0: Scan folder Path column clipping; Ctrl+S on an untitled package opening Save As; Help → Welcome….

### MINOR

- New thing a user can do: Tools item, Resource editor, bus command, CLI verb, MCP tool.
- New **kind** of Release asset (the first Windows portable zip was this, not a PATCH).
- Intentional change of default behavior people must relearn (new default merge policy, new save layout).
- In **0.x only:** rename/remove a bus command or required flag (breaking, but we are still Beta). After 1.0 that is MAJOR.

Examples: first `folder.scan`; Merge assistant; typed OBJD editor; Linux GUI tarball as a new artifact type.

### Not a version bump by itself

- Refactor that does not change behavior.
- GitHub markdown the zip does not ship.
- Issue tracker / project board work.

Those ride along for free in the next real release.

### Grey cases (pick the higher bump)

| Change | Treat as |
| --- | --- |
| Existing command, broken GUI, now a usable table (Search JSON → table) | PATCH (fixing the surface, same command) |
| Same command, different answers or different files written | MINOR (behavior) |
| Menu grouping / separators / copy | PATCH |
| Remove a menu item whose bus command remains | PATCH, and say so in the notes |
| Remove a bus command | MINOR in 0.x; MAJOR after 1.0 |
| Layout-lock still refuses compact; you only renamed the menu | PATCH |
| SXMM / package bytes older SXPE cannot un-merge | MINOR in 0.x; MAJOR after 1.0 |

## 1.0.0 (leave Beta)

`1.0.0` is a **product** decision, not a count of features. README still calls SXPE a public Beta until you choose 1.0.

1.0 means all of:

- The Beta / pre-release banner comes down (Welcome, README, GitHub Release).
- CLI, MCP, and bus ids are a **compatibility surface**: later breaks need a MAJOR.
- On-disk writes (including SXMM) stay readable by that 1.0 line or you document a MAJOR.

1.0 does **not** mean a full s3pe replacement, 3D preview, Store/DRM Sims3Pack, or plugins. Those stay out of scope unless a later MAJOR says otherwise.

Do not jump `0.7.0` → `1.0.0` because a milestone felt large. Use `0.8.0` until you intend the promises above.

## Shape of the number

- Always three integers: `X.Y.Z`. Not `0.8`, not `0.7.0.1`, not a date.
- Tag: `vX.Y.Z` with a lowercase `v`, matching `PROJECT_VERSION`.
- Do not put `-beta` / `-dev` in `CMakeLists.txt`. Check for update **strips** a `-` suffix, so `0.8.0-dev` would compare as `0.8.0`. Mark Beta on the **GitHub Release** (`prerelease`) instead.
- `0.10.0` is greater than `0.9.0` (numeric compare). Fine to use.
- `+` build metadata is also stripped; do not rely on it for uniqueness.

## What to edit when you bump

**Required** (must match, or CLI `--version`, About, MCP `serverInfo`, and the zip name disagree):

1. `CMakeLists.txt`: `project(sxpe VERSION X.Y.Z …)`
2. `vcpkg.json`: `"version-string": "X.Y.Z"`
3. `docs/releases/vX.Y.Z.md`: new notes from [TEMPLATE.md](releases/TEMPLATE.md)

**User-facing copies of the current tag** (so download links are not stale):

4. `README.md`: "Current release", zip/tarball names, Version section
5. `docs/cli-mcp.md`: if it links a specific `vX.Y.Z`

**Do not churn** to match the new tag:

- Old `docs/releases/v0.6.0.md` / `v0.7.0.md` (history)
- `fixtures/synthetic/github-*.json` and tests that freeze a sample `v0.7.0` (they test the updater, not the current product)
- `CMakeLists.txt` `cli_check_update_fixture` `--current-version 0.7.0` (matches the fixture, not `PROJECT_VERSION`)
- `cli_version` already asserts `"${PROJECT_VERSION}"`; no edit

Optional: `.github/ISSUE_TEMPLATE/bug_report.yml` placeholder example.

After merge to `dev`, tag **`vX.Y.Z`** on that commit so `release.yml` builds `sxpe-X.Y.Z-…` zips. Tagging a commit whose `PROJECT_VERSION` is still `0.7.0` as `v0.8.0` is a bug: Check for update and the zip name will fight.

## Hotfixes

If `v0.7.0` is in the wild, `dev` already has `0.8.0` work, and you need a save-corruption fix on the shipped line:

1. Branch from the **`v0.7.0` tag**, not from current `dev`.
2. Bump **PATCH** only (`0.7.1`), notes `docs/releases/v0.7.1.md`.
3. Tag `v0.7.1`. Check for update prefers it over `v0.7.0`.
4. Cherry-pick the fix onto `dev` without a second bump.

This should be rare. Prefer shipping the fix as part of the next MINOR when the bug is not data-loss.

## Bus envelope (`schemaVersion`)

The JSON envelope field `"schemaVersion": 1` is **not** the product version. Leave it at `1` unless the envelope shape itself changes (`ok` / `data` / `error` keys).

- Envelope shape change: bump **envelope** `schemaVersion`, and treat that as a bus break (MINOR in 0.x, MAJOR after 1.0).
- New bus *command* with the same envelope: product **MINOR**, envelope stays `1`.

## Worked examples

Starting from shipped **0.7.0**:

| Since `v0.7.0` you would ship… | Next tag |
| --- | --- |
| Path column clip + Welcome menu + Tools separators | `v0.7.1` (PATCH) |
| That polish **plus** a new bus command | `v0.8.0` (MINOR; polish rides along) |
| Rename `package.validate` in 0.x | `v0.8.0` (MINOR break) |
| Same rename after 1.0 | `v2.0.0` (MAJOR) |
| Take the Beta banner down and freeze the bus | `v1.0.0` |
| README typo on GitHub only | no tag |
| Re-package `v0.7.0` because a CI job failed | no bump; `workflow_dispatch` on `v0.7.0` |
