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

GUI drop-merge and `resource.importPackage --writeMergeManifest` write this after copying. Source DIR and SXMM rows are skipped (`dirPolicy: strip`).

## Un-merge

`package.unmerge --package merged.package --outDir DIR`

- Require `format` + `version` ≥ 1 + `sources`.
- Write one child package per source (basename only).
- Copy listed TGIs as they are now; warn if missing.
- Do not copy SXMM into children.

## Non-goals

Unmerging packages without SXMM. S4S/TS4 interop. Guessing CAS items.
