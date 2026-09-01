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
