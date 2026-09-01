# STBL string table `0x220557DA`

Source: https://simswiki.info/wiki.php?title=Sims_3:0x220557DA

## On disk (little-endian)

```
uint32 magic     // 'STBL' as 0x4C425453
uint8  version   // 2
uint8  unk0      // 0
uint8  unk1      // 0
uint32 count
uint8  reserved[6]  // 0
repeat count times:
    uint64 id      // string GUID (do not change when translating)
    uint32 length  // number of UTF-16 code units
    char16 text[length]  // UTF-16LE, no NUL required
```

## Instance language

The **high byte** of the resource **instance** is the locale (e.g. `00` English). Remaining 7 bytes identify the string set. Translators change the high byte, not the GUIDs.

Do not rewrite `{0.SimName}` / `{MA.…}{FA.…}` tokens except by whole-token copy.

## v1 commands

`stbl.get` / `stbl.set` / `stbl.delete` / merge-on-import. Editor must be usable from CLI/MCP (JSON of `{id, text}`).
