# SXPE merge manifest (`SXMM` `0x53584D4D`)

Machine-readable provenance for packages **SXPE itself merged**. Un-merge refuses files without this resource. Not an S4S manifest. Not a heuristic split of s3pe mega-packs.

## Resource

| Field | Value |
| --- | --- |
| Type | `0x53584D4D` (`SXMM`) |
| Group | `0` |
| Instance | `1` |
| Payload | UTF-8 JSON, no BOM |

## JSON

```json
{
  "format": "sxpe.mergeManifest",
  "version": 1,
  "sources": [
    {
      "id": "src-1",
      "originalFileName": "hair.package",
      "resources": [{ "type": 1, "group": 0, "instance": 2, "ordinal": 0 }],
      "nameMap": {
        "type": 23462796,
        "group": 0,
        "instance": 0,
        "ordinal": 0,
        "version": 1,
        "entries": [{ "instance": 2, "name": "hair" }]
      }
    }
  ],
  "notes": { "forceOverwriteOnDuplicateTgi": true, "dirPolicy": "strip", "nmapPolicy": "concat" }
}
```

GUI drop-merge and `resource.importPackage --writeMergeManifest` write this after copying. Source SXMM rows are always skipped when writing a new manifest. Source DIR handling follows `notes.dirPolicy` (see [dir.md](dir.md)):

| `dirPolicy` | Behavior |
| --- | --- |
| `strip` | Default with `--writeMergeManifest`. Skip source DIR rows (EA TS3 does not ship DIR). |
| `copy-through` | Default without a merge manifest. Copy source DIR bytes into the dest when present. |
| `rebuild` | Not yet implemented; `resource.importPackage` refuses with a clear error. |

Duplicate NMAP TGIs concatenate name records instead of last-wins replace, and the name map is moved to index 0 (see [nmap.md](nmap.md)). Each source records its original `nameMap` so un-merge can restore that table instead of copying the concatenated NMAP.

## Un-merge

`package.unmerge --package merged.package --outDir DIR`

- Require `format` + `version` ≥ 1 + `sources`.
- Write one child package per source using **basename-only** `originalFileName`.
  Reject path separators, `..`, and absolute paths (traversal hardening).
- Copy listed TGIs via **on-disk blob copy-through** (preserves RefPack sizes/flags), except NMAP.
- Restore each child's NMAP from that source's `nameMap` snapshot (uncompressed). If `nameMap` is absent (older SXMM), keep merged NMAP rows whose instance appears on that source's other listed resources.
- Warn when a listed TGI is missing; report orphan resources present in the merge but not listed in SXMM.
- Do not copy SXMM (or source DIR) into children.

## Non-goals

Unmerging packages without SXMM. S4S/TS4 interop. Guessing CAS items.


## Leftover manifest allowlist (issue #64)

On `resource.importPackage` / `resource.importDbc`, SXPE can strip or warn on **documented** leftover TGIs that cause community merge conflicts:

| Type | Instance | Group | Reason |
| --- | --- | --- | --- |
| `0x73E93EEB` | `0` | any | Sims3Pack leftover package-manifest XML (launcher); classic MATY / Anach pain |

`leftoverManifestPolicy`:

| Value | Behavior |
| --- | --- |
| `strip` | **Default.** Skip copying allowlisted leftovers; list them in `strippedLeftovers[]`. |
| `keep` | Copy as normal resources. |
| `warn` | Copy, but list in `warnings[]`. |

Allowlist lives in `include/sxpe/resources/merge_hygiene.hpp`. Do not extend it with gameplay `_XML` / ITUN keys.

## Duplicate TGI policy (issue #64)

When a source TGI already exists in the destination (non-NMAP; NMAP still concatenates):

| `duplicateTgiPolicy` | Behavior |
| --- | --- |
| `force` | Overwrite destination blob (default when `force=true`) |
| `skip` | Leave destination; list in `duplicates[]` with `action=skip` |
| `fail` | Stop that source package; list in `duplicates[]` / `errors[]` (default when `force=false`) |

If `duplicateTgiPolicy` is omitted, `force` argument selects force vs fail (backward compatible). Response always includes `duplicates[]` for force/skip/fail hits.
