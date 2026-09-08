# DDS decode / replace matrix (#55)

SXPE parses and (where marked **decode**) expands DDS payloads for `_IMG` preview and
`dds.decode` / `dds.replace`. Caps: edge ≤ `kMaxDdsEdge` (8192). No `LoadLibrary`, no
DirectXTex, no BC7/DX10 pipeline.

Synthetic fixtures only in tests — never commit EA textures.

## Supported (decode + Replace DDS)

| Format | FourCC / layout | Notes |
| --- | --- | --- |
| DXT1 | `DXT1` | BC1 colour; 1-bit alpha when c0≤c1 |
| DXT3 | `DXT3` | Explicit 4-bit alpha + BC1 colour |
| DXT5 | `DXT5` | Interpolated 8-alpha + BC1 colour |
| A8R8G8B8 | RGB32 masks `00FF0000/0000FF00/000000FF/FF000000` | Also legacy omitted masks → BGRA |
| X8R8G8B8 | Same RGB masks, no alpha bit | Opaque |
| A8B8G8R8 | `000000FF/0000FF00/00FF0000/FF000000` | |
| R8G8B8A8 | `FF000000/00FF0000/0000FF00/000000FF` | |
| B8G8R8A8 | `0000FF00/00FF0000/FF000000/000000FF` | |
| R8G8B8 / B8G8R8 | RGB24 8-bit masks | Alpha forced 255 |
| R5G6B5 | RGB16 `F800/07E0/001F` | |
| A1R5G5B5 / X1R5G5B5 | RGB16 `7C00/03E0/001F` (±`8000` alpha) | |

`encode_dds_bgra` writes **A8R8G8B8** (uncompressed).

## Refused (clear `unsupported_game_or_format`)

| Case | Behaviour |
| --- | --- |
| Cubemap (`DDSCAPS2_CUBEMAP`) | `dds.decode` / `dds.replace` / preview refuse with “cubemap … 2D only” |
| Volume / 3D (`DDSCAPS2_VOLUME`) | Same, “volume/3D …” |
| DXT2 / DXT4 | Premultiplied; ask for DXT3/DXT5 |
| Other FourCC (ATI1/2, BC4/5, DX10, BC7, …) | Message lists supported set; no DirectXTex |
| Oversized edge / pixel budget | `cap_exceeded` |
| Truncated block payload | `corrupt` |

`parse_dds` / `dds.info` still report width/height/format/`cubemap`/`volume`/`decodeSupported`
for refused payloads when the header is intact.

## Commands (bus / CLI / MCP)

| Command | Role |
| --- | --- |
| `dds.info` | Header fields including `decodeSupported` |
| `dds.decode` | RGBA byte count (no pixel embed in MCP) |
| `dds.export` | Raw on-disk DDS bytes |
| `dds.replace` | Validate then replace resource (GUI **Replace DDS…**) |

## Inventory note (CC traffic)

Common custom-content `_IMG` traffic is DXT1/DXT5 diffuse/normal and A8R8G8B8 UI/skin.
DXT3 appears on older specular/alpha sheets. Cubemaps are rare for replace and are refused
on purpose. BC7/DX10 is out of scope unless justified later.
