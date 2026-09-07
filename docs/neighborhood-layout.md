# Neighborhood / world layout lock (`.nhd` / `.world` / `.dbc`)

EA neighborhood and world packages use a **fixed on-disk layout**. SXPE must not rebuild
the index or move payload holes: the game treats a compacted or reordered file as corrupt.

## Detection

| Field | Source | Values |
| --- | --- | --- |
| `layoutLocked` | `package.info`, `package.validate` | `true` when the session path ends in `.nhd`, `.world`, or `.dbc` |
| `pathKind` | same | `nhd` / `world` / `dbc` / `package` |

CLI `--format text` prints these on `package info`. Validate `summary[]` (CLI / GUI / MCP)
names **neighborhood / world layout lock** when locked. The GUI shows a status-bar badge
and greys out unsupported Resource actions.

## Safe

- **In-place payload replace** for an existing index row, when the new on-disk bytes fit the
  existing hole (`payload_capacity` / original file size). Editors that rewrite a body
  (STBL, XML/ITUN, SNAP PNG, DDS, S3SA replace) are OK if the result fits.
- Read / export / list / find-refs / validate.
- `package.save` that only flushes in-place overrides (byte-identical layout, same file size).

## Not supported (refused; no silent success-then-fail)

Refusals include the phrase **neighborhood / world layout lock**:

- Add / duplicate / paste / import that grows the index
- Delete / deleted flag (would drop an index row on save)
- Reorder (`Package::move`)
- Compact / full rebuild save
- Creating an NMAP when the file has none

Prefer **Save Copy As** to a `.package` if you need a free-layout working copy; do not
expect EA to load a rebuilt neighborhood file.
