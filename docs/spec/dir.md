# DIR resource `0xE86B1EEF`

Sources:

- https://simstek.fandom.com/wiki/DBPF
- https://simswiki.info/wiki.php?title=Sims_3:DBPF/Compression

## Role

A package **may** contain a DIR resource listing compressed entries (type/group/instance + uncompressed size). Used to cross-check `MemSize` when `Compressed == 0xFFFF`.

## Read (v1)

- If no DIR is present → OK. Use index `mem_size` / `file_size`.
- If DIR is present → ignore for decode but do not drop the resource on round-trip (copy bytes through). **FullBuild0 / fallback / DeltaBuild_p20 have no DIR** (2026-09-07 survey). Record layout remains TBC until a package that contains one is inspected.

## Write (v1)

- Always write the index compressed WORD (`0` or `0xFFFF`).
- **Do not invent** a DIR if the input package had none.
- If the input had a DIR, copy it through until the FullBuild survey specifies rewrite rules.

## TBC-game

DIR is **absent** from Steam FullBuild0 (102 127 rows), fallback, and DeltaBuild_p20. Follow-up: record layout and whether every compressed row appears, on a package that actually has a DIR.
