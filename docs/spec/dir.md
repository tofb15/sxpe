# DIR resource `0xE86B1EEF`

Sources:

- https://simstek.fandom.com/wiki/DBPF
- https://simswiki.info/wiki.php?title=Sims_3:DBPF/Compression

## Role

A package **may** contain a DIR resource listing compressed entries (type/group/instance + uncompressed size). Used to cross-check `MemSize` when `Compressed == 0xFFFF`.

## Read (v1)

- If no DIR is present → OK. Use index `mem_size` / `file_size`.
- If DIR is present → **TBC-game** exact record layout on FullBuild0. Until then, ignore DIR for decode but do not drop the resource on round-trip (copy bytes through).

## Write (v1)

- Always write the index compressed WORD (`0` or `0xFFFF`).
- **Do not invent** a DIR if the input package had none.
- If the input had a DIR, copy it through until the FullBuild survey specifies rewrite rules.

## TBC-game

On `FullBuild0.package`: is DIR present? Record size? Does every compressed index row appear in DIR?
