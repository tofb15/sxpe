# S3SA script assembly `0x073FAA07`

Source: https://simswiki.info/wiki.php?title=Sims_3:0x073FAA07

**S3SA** = Encrypted Signed Assembly. A `.package` resource whose decrypted payload is a CLR / .NET PE (a C# `.dll`). The game’s script host loads these; SXPE must not.

This file is the format spec **and** the later-work list. Preview Wave 1 shipped a thin identity card only. Do **not** treat s3pi/s3pe C# as the spec.

Related: [hashing.md](hashing.md) (FNV-1 64, lowercase), [tags.md](../tags.md), [preview.md](../preview.md). Instantiator XML is type `0x0333406C` (`_XML`), not part of this codec.

## Status (today)

| Surface | What it does |
| --- | --- |
| `s3sa.info` | Resource **size**, first `MZ` byte offset in the **raw** blob, NMAP name as `moduleHint` (else `assembly.dll`) |
| `s3sa.exportDll` | Write bytes from that `MZ` to EOF (or the whole blob if no `MZ`) |
| GUI Editors → Export S3SA as DLL | Same |
| Preview tab | Size / PE offset / module hint |
| Wrap / import / decrypt / version fields | **Missing** |
| `resource.add` of a raw `.dll` as type `073FAA07` | **Wrong** — that is a PE, not an S3SA |

`exportDll` works for **community** blobs (zero XOR table → plaintext PE after a 135-byte v1 prologue). It is not a decoder. Encrypted or v2 resources, or a PE with no `MZ` in the wrapped bytes, export garbage or the wrapper.

`editor.list` claiming “S3SA editor” is a catalog comment. There is no wrap/replace editor.

## On disk (little-endian)

Wiki layout. Field names below match the wiki; community packages use **version 1**.

```
uint8  version              // 1 = community; 2 = EA signed (EP2+)
-- if version >= 2:
    uint32 gameVersionChars // UTF-16 code unit count
    char16 gameVersion[gameVersionChars]
uint32 checksumType         // 0x2BC4F79F in practice (wiki: checksum_typeid)
uint8  checksum[64]         // wiki: checksum_data / md5sum
uint16 blockCount           // number of 8-byte key words
uint64 keyTable[blockCount] // wiki: decryption table; each word is 8 bytes
uint8  cipher[blockCount * 512]
```

**v1 prologue** (no `gameVersion`): `1 + 4 + 64 + 2 + 8*blockCount` bytes. For `blockCount == 8` that is **135** bytes, then `cipher`.

`blockCount` is `ceil(plaintext_size / 512)`. A 4096-byte DLL → 8 blocks, 64-byte key table, 4096-byte cipher, 4231-byte resource.

### Community write (what SXPE must emit)

- `version = 1` (no `gameVersion` string)
- `checksumType = 0x2BC4F79F`
- `checksum` all zeros
- `keyTable` all zeros
- `cipher` = plaintext DLL, **zero-padded** to `blockCount * 512`

Wiki: signature checks have been off since EP2 (June 2010); the key table **may be left full of zeros**. Do not invent a checksum. Do not target version 2 for a community package (EA `scripts.package` is signed official code).

### Decrypt / encrypt

Wiki algorithm (implement from the wiki, not from s3pi):

1. `seed = (sum of keyTable words) & (keyTable byte length - 1)`
2. For each key word, in order:
   - If bit 0 of the word is set: emit 512 zero bytes (skip cipher for this block).
   - Else read 512 cipher bytes. For each byte `b`:
     - `out = b XOR keyTableBytes[seed]`
     - `seed = (seed + b) % keyTableByteLength`  (`b` is the **cipher** byte, not `out`)
3. Encrypt is the reverse of decrypt.

All-zero `keyTable`: XOR is identity. Decrypted assembly **starts at `MZ`**.

Parser must still walk `version` / optional `gameVersion` / `blockCount` so v2 and non-zero tables round-trip. Writer for **new** community resources always uses the zero-table v1 shape above.

Refuse: `blockCount == 0`, `cipher` size ≠ `blockCount * 512`, `mem_size` over the usual resource cap.

## Default TGI (new resource)

| Field | Value |
| --- | --- |
| Type | `0x073FAA07` |
| Group | `0` |
| Instance | FNV-1 **64**, input **lowercased**, of the DLL **file name** including `.dll` (e.g. `testmod.dll`) |

That is the community convention (NMAP name is often the same filename). **Replace** of an existing S3SA keeps the current TGI.

Do **not** put the high 32 bits of the hash in `group`. Do **not** default instance to `0x00010000`.

## What s3pe does here (behaviour only)

s3pe’s S3SA helper (Import / Export / View) wraps and unwraps through a `ScriptResource`-class encoder: decrypt on export, re-encode on import, optional launch of a user-configured assembly viewer. Property grid shows wrapper fields. It does **not** compile C#, does **not** add the `_XML` instantiator, and its viewer historically `Assembly.Load`s the PE (SXPE must not).

SXPE 1.0 target is **that helper’s wrap/unwrap/inspect**, reimplemented from this spec:

- Import DLL → valid community S3SA (replace selected, or add)
- Export DLL → decrypted PE (not an `MZ` hunt)
- Preview / `s3sa.info` → wrapper fields + decrypted PE facts
- Optional: user-configured external viewer on a **temp file**, never in-process load

Not a goal: pixel-identical helper EXE, s3pi type names, or EA v2 signing.

## Planned commands

CLI `sxpe s3sa <verb>`; MCP `s3sa_<verb>`; same bus ids. Envelope unchanged. `--force` / `dryRun` as for other writers.

| id | readOnly | Status | Contract |
| --- | --- | --- | --- |
| `s3sa.info` | y | **extend** | Today: `size`, `peOffset?`, `moduleHint`. After codec: also `version`, `gameVersion` (string or absent), `checksumType`, `checksumZero` (bool), `blockCount`, `keyTableZero` (bool), `assemblyBytes` (decrypted length **before** padding), `peOffset` measured in **decrypted** bytes (0 for a well-formed PE). Keep `moduleHint` from NMAP. |
| `s3sa.exportDll` | y | **fix** | Decrypt, trim padding after last non-zero? **No** — trim to PE size from the PE headers (or to `assemblyBytes` if we store unpadded length). Write that PE to `path`. Refuse if decrypt fails. `force` if the file exists. |
| `s3sa.importDll` | n | **new** | Read a PE from `path` (`MZ` required). Wrap as community v1. **Replace** the selected S3SA if `resourceId` is set; **add** if omitted (TGI as above; optional `instance` / `group` overrides). Update NMAP name to the filename when the package has (or we create) an NMAP. `force` / `dryRun`. |
| `s3sa.wrap` | y | **new** (optional alias) | Stateless: `{path}` → `{bytes}` or write-file; no session. Useful for tests. Prefer `importDll` for the GUI. |

Do **not** add `s3sa.load` / `LoadLibrary` / CLR host.

Undo: `importDll` is a normal `resource.replace` / `resource.add` mutation (stack 50).

### GUI

- Editors → **Export S3SA as DLL…** — keep; route through the decoder.
- Editors → **Import DLL into S3SA…** — new; enabled on an S3SA row (replace) **and** with no row / via Resource → Import as type S3SA (add).
- Preview: show the extended `s3sa.info` card (version, zero-key, assembly size, `MZ` in decrypted bytes).

Settings: optional `s3saViewer` path + args (generic `{path}` placeholder), same pattern as other user EXEs. **Not** bundled. **Not** a clone of s3pe `.helper` grammar. View = export temp DLL, spawn, delete temp on process exit. If unset, no View action.

## PE metadata (later, still no load)

After decrypt, a read-only walk of PE → CLI metadata is enough to answer “will Mono accept this?”

| Field | Why |
| --- | --- |
| Assembly name / `ManifestModule` | Filename hint when NMAP is empty |
| `mscorlib` AssemblyRef version | Must be **2.0.0.0** for the TS3 script host. 4.0.0.0 is the usual crash (`EXCEPTION_BREAKPOINT` in `TS3W`) |
| Other AssemblyRef names | `SimIFace`, `ScriptCore`, `UI`, … — listing is enough; do not resolve |

Implement with a small PE/CLI parser in `sxpe::resources`. Never map the PE as an executable, never `LoadLibrary`, never run static constructors.

Expose on `s3sa.info` as `assemblyName`, `mscorlib` (`"2.0.0.0"` / `"4.0.0.0"` / absent), `refs` (short name list, cap ~32). Preview shows those lines.

If the PE is truncated or not CLI, omit the fields; still allow export of the decrypted bytes.

## Out of scope

- Executing the assembly.
- Computing a real `checksum` / EA signature (needs EA’s private key; unused since EP2).
- Writing S3SA **version 2** or a `gameVersion` string for community mods.
- Compiling C# (no `csc` / MSBuild in SXPE).
- Shipping EA `scripts.package` / `gameplay.package` as fixtures.
- Copying s3pi `ScriptResource.cs` or the s3pe helper EXE.
- Treating `resource.add` of raw PE bytes as an S3SA (keep refusing or auto-wrap; auto-wrap only via `s3sa.importDll`).

## Script-mod door (not S3SA, do not forget)

A net20 DLL in a valid S3SA can **load without crashing and still never run**. The game fires the static constructor through an `_XML` instantiator:

| Tag | Type | Group | Instance |
| --- | --- | --- | --- |
| S3SA | `073FAA07` | `0` | FNV-1 64 lowercase of `AssemblyName.dll` |
| `_XML` | `0333406C` | `0` | FNV-1 64 lowercase of `Namespace.Class` |
| NMAP | `0166038C` | (map) | names |

`_XML` body is a tiny tunable, field name **`kInstantiator`**, value true. C# side: `[assembly: Tunable]` plus `[Tunable] static bool kInstantiator` and a static ctor that hooks `Sims3.SimIFace.World.OnWorldLoadFinishedEventHandler` (not `ScriptCore.World`).

s3pe does not auto-build this pair. SXPE 1.0 S3SA work **does not** have to either. A later `package.makeScriptMod` (or CLI recipe) may: wrap DLL + write `_XML` + NMAP. Until then, document in Preview that an S3SA without a matching `_XML` is “assembly present, instantiator not in this package.”

Wrong XML type `0x4D584C5F` (fourcc mash of `_XML`) is not valid. Hash split into `group=hi32` / `instance=lo32` is not valid.

## Implementation order

1. **Codec + tests** — parse v1/v2; decrypt; wrap v1 zeros; round-trip synthetic PE. Replace `inspect_s3sa` `MZ` scan with “decrypt then `MZ` at 0.”
2. **`s3sa.info` / `exportDll`** — decoder path; keep old JSON keys so GUI does not break.
3. **`s3sa.importDll`** + GUI Import + NMAP name.
4. **PE/CLI refs** on `s3sa.info` (mscorlib version).
5. Optional external viewer.
6. Optional `package.makeScriptMod` (`_XML` door).

## Tests (synthetic only)

`fixtures/synthetic/` — invented bytes, not game DLLs.

- Tiny buffer starting `MZ` + padding; wrap → `version==1`, `blockCount` matches `ceil(n/512)`, decrypt equals input (trimmed to original length).
- Zero-key v1 blob: `exportDll` equals the PE.
- Non-zero key table: decrypt matches the known plaintext (construct in the test; do not use EA files).
- v2 header with a short `gameVersion`; parse does not throw; **writer still emits v1**.
- Refuse `importDll` of a file whose first two bytes are not `MZ`.
- `resource.add` of raw PE as type `073FAA07` must not be the documented happy path (test `importDll` instead).

## Caps

Same as other resources: uncompressed ≤ 256 MiB. `blockCount * 512` must equal cipher size. Preview still skips live decode above `kMaxLivePreviewBytes` (8 MiB) except via explicit Export.
