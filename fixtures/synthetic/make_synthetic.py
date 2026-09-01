#!/usr/bin/env python3
"""Tiny TS3-style DBPF / RefPack fixtures (original bytes, not EA)."""
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


def write_pkg(name: str, resources: list[tuple[int, int, int, bytes]]) -> None:
    payloads = b"".join(p for *_, p in resources)
    idx = struct.pack("<I", 0)
    off = HDR
    for t, g, i, p in resources:
        idx += entry(t, g, i, off, len(p))
        off += len(p)
    pos = HDR + len(payloads)
    data = header(len(resources), len(idx), pos) + payloads + idx
    (OUT / name).write_bytes(data)


def main() -> None:
    write_pkg("empty.bin", [])
    write_pkg(
        "single-blob.bin",
        [(0, 0, 1, b"Hello SXPE\n")],
    )
    # 10 FB + 3-byte BE uncompressed length 2, stop 0xFE (2 literals), Hi
    ref = bytes([0x10, 0xFB, 0x00, 0x00, 0x02, 0xFE, ord("H"), ord("i")])
    (OUT / "refpack-hello.bin").write_bytes(ref)
    for p in OUT.glob("*.bin"):
        print(p.name, p.stat().st_size)


if __name__ == "__main__":
    main()
