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
      "resources": [{ "type": 1, "group": 0, "instance": 2, "ordinal": 0 }]
    }
  ],
  "notes": { "forceOverwriteOnDuplicateTgi": true, "dirPolicy": "strip" }
}
```

GUI drop-merge and `resource.importPackage --writeMergeManifest` write this after copying. Source DIR and SXMM rows are skipped (`dirPolicy: strip`). Duplicate NMAP TGIs concatenate name records instead of last-wins replace (see [nmap.md](nmap.md)).

## Un-merge

`package.unmerge --package merged.package --outDir DIR`

- Require `format` + `version` ≥ 1 + `sources`.
- Write one child package per source using **basename-only** `originalFileName`.
  Reject path separators, `..`, and absolute paths (traversal hardening).
- Copy listed TGIs via **on-disk blob copy-through** (preserves RefPack sizes/flags).
- Warn when a listed TGI is missing; report orphan resources present in the merge but not listed in SXMM.
- Do not copy SXMM (or source DIR) into children.

## Non-goals

Unmerging packages without SXMM. S4S/TS4 interop. Guessing CAS items.
