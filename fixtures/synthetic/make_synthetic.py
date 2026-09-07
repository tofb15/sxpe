#!/usr/bin/env python3
"""Tiny TS3-style DBPF / RefPack / Sims3Pack fixtures (original bytes, not EA)."""
from __future__ import annotations

import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent
HDR = 96


def header(count: int, index_size: int, index_pos: int) -> bytes:
    buf = bytearray(HDR)
    buf[0:4] = b"DBPF"
    struct.pack_into("<I", buf, 0x04, 2)
    struct.pack_into("<I", buf, 0x08, 0)
    struct.pack_into("<I", buf, 0x24, count)
    struct.pack_into("<I", buf, 0x2C, index_size)
    struct.pack_into("<I", buf, 0x3C, 3)
    struct.pack_into("<I", buf, 0x40, index_pos)
    return bytes(buf)


def entry(typ, group, inst, off, size) -> bytes:
    hi, lo = (inst >> 32) & 0xFFFFFFFF, inst & 0xFFFFFFFF
    return struct.pack("<8I", typ, group, hi, lo, off, size, size, 0)


def build_pkg(resources: list[tuple[int, int, int, bytes]]) -> bytes:
    payloads = b"".join(p for *_, p in resources)
    idx = struct.pack("<I", 0)
    off = HDR
    for t, g, i, p in resources:
        idx += entry(t, g, i, off, len(p))
        off += len(p)
    pos = HDR + len(payloads)
    return header(len(resources), len(idx), pos) + payloads + idx


def write_pkg(name: str, resources: list[tuple[int, int, int, bytes]]) -> None:
    (OUT / name).write_bytes(build_pkg(resources))


def write_minimal_sims3pack() -> None:
    """SimsWiki TS3Pack frame + XML + one embedded synthetic DBPF."""
    pkg = build_pkg([(0, 0, 1, b"Hello SXPE\n")])
    name = "0x0000000000000001.package"
    xml = (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<Sims3Package Type="Object" SubType="0x00000000">'
        "<ArchiveVersion>1.4</ArchiveVersion>"
        "<DisplayName>SXPE Synthetic</DisplayName>"
        "<Description>Synthetic Sims3Pack fixture (not EA)</Description>"
        "<PackageId>sxpe-synthetic-0001</PackageId>"
        "<PackagedFile>"
        f"<Name>{name}</Name>"
        f"<Length>{len(pkg)}</Length>"
        "<Offset>0</Offset>"
        "<Crc>00000000</Crc>"
        "<Guid>00000000-0000-0000-0000-000000000001</Guid>"
        "<ContentType>package</ContentType>"
        "</PackagedFile>"
        "</Sims3Package>"
    ).encode("utf-8")
    hdr = struct.pack("<I", 7) + b"TS3Pack" + struct.pack("<HI", 0x0101, len(xml))
    data = hdr + xml + pkg
    (OUT / "minimal.sims3pack").write_bytes(data)


def main() -> None:
    write_pkg("empty.bin", [])
    write_pkg(
        "single-blob.bin",
        [(0, 0, 1, b"Hello SXPE\n")],
    )
    # 10 FB + 3-byte BE uncompressed length 2, stop 0xFE (2 literals), Hi
    ref = bytes([0x10, 0xFB, 0x00, 0x00, 0x02, 0xFE, ord("H"), ord("i")])
    (OUT / "refpack-hello.bin").write_bytes(ref)
    write_minimal_sims3pack()
    for p in sorted(OUT.glob("*")):
        if p.suffix in {".bin", ".sims3pack"}:
            print(p.name, p.stat().st_size)


if __name__ == "__main__":
    main()
