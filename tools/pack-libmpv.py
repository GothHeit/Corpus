#!/usr/bin/env python3
"""Compresses libmpv-2.dll for corpus-gui.exe: pack-libmpv.py <libmpv-2.dll> <output>

The build runs this, and video_player.cpp unpacks the result with the Windows Compression API (MSZIP, raw blocks).
Layout: the DLL's size as a 64-bit number, then one record per 32 KiB of the DLL: the size of its compressed block as
a 32-bit number, and the block, "CK" followed by a raw deflate stream. Each block stands alone, as the API's raw mode
decompresses them one by one.
"""
import struct
import sys
import zlib

BLOCK = 32768


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as source:
        data = source.read()
    out = bytearray(struct.pack("<Q", len(data)))
    for start in range(0, len(data), BLOCK):
        packer = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
        block = b"CK" + packer.compress(data[start:start + BLOCK]) + packer.flush()
        out += struct.pack("<I", len(block)) + block
    with open(sys.argv[2], "wb") as target:
        target.write(out)


main()
