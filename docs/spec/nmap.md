# NMAP name map `0x0166038C`

Source: https://simswiki.info/wiki.php?title=Sims_3:0x0166038C

## On disk (little-endian)

```
uint32 version           // usually 1
uint32 number_of_records
repeat:
    uint64 instance_id
    uint32 name_length
    uint8  name[name_length]   // encoding TBC-game (often ASCII/UTF-8 without NUL)
```

Used to show **Names** in the resource list. Missing NMAP → names empty, TGI still works.

## v1

Read for display; rewrite when renaming resources if we own the NMAP. Do not invent names for EA FullBuild.

## Merge

`resource.importPackage` concatenates NMAP records when source and dest share a TGI (community packages all use `0166038C` group 0 instance 0). That is a table union, not last-wins blob replace: dropping the earlier map blanks the Name column for those resources.

- First NMAP is copy-through (keeps on-disk RefPack if present).
- Later NMAPs with the same TGI are decompressed, appended in import order, and written uncompressed (same as `nmap.set`).
- Duplicate instance ids are kept as extra rows; the Name column uses last-wins (`name_index`).
- After import the (single) NMAP row is moved to package index 0, matching s3pe merge order. SXMM stays at the end.
- SXMM stores each source's original name table (`nameMap`). `package.unmerge` writes that table back into the child; it does not copy the concatenated merge NMAP.

## Editor (CLI / MCP / GUI)

| id | notes |
| --- | --- |
| `nmap.get` / `nmap.list` | Raw rows; `duplicates[]` with `effectiveName` (last-wins, same as Name column) |
| `nmap.set` | One instance; updates last matching row when duplicates exist |
| `nmap.delete` | Remove all rows for an instance |
| `nmap.replace` | Replace entire table in **one** write / one undo — batch editor save |

GUI: **Resource → Editors → Name map…** (enabled when an NMAP is selected or the package has an NMAP). Edit multiple names, search, add/delete rows, save once via `nmap.replace`.

CLI: `sxpe nmap list|set|delete|replace`. Optional `--entries '[{instance,name},…]'` for replace.
