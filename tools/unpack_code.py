"""Writes build/excitebike64.us.code.z64, the copy of the ROM N64Recomp reads the game's code from (us.toml): the ROM
with its boot loader (ROM 0x1000) replaced by the game image the boot loader unpacks, so that the image's VRAM
0x80000400 is ROM 0x1000, as syms/ says. It is never run and never leaves build/: the program runs the ROM itself,
and src/main/main.cpp's entry unpacks the same image into RDRAM in C++ (eb::unpack_game).

The cartridge stores the game image in a container at ROM 0x12230 (its tag "BRSOHYES", then four words pairs: each
segment's unpacked and packed size, then the four segments, each SWD-packed). The boot loader unpacks the four, one
after the other, and copies the whole over itself to 0x80000400 (lib/excitebike64's docs/overview.md, Startup).
SWD is John Brandwood's LZSS variant (his SWD32's SWDN64.ASM, Boost license); expand() below is written from it,
and src/main/main.cpp's swd_expand is the same in C++.

Usage: py -3 -I tools/unpack_code.py <rom> <out>
"""
import hashlib
import struct
import sys

ROM_SHA1 = "c2fac422af135409c1b38569b309f9d452c81c35"  # Excitebike 64 (USA) (Rev 1)
CONTAINER = 0x12230
HEADER_SIZE = 0x28
IMAGE_ROM = 0x1000
IMAGE_SIZE = 0xDC5B8  # 0x80000400-0x800DC9B8: the game image's code and initialized data

OFFSETS = ((5, 0x0001), (7, 0x0021), (9, 0x00A1), (10, 0x02A1))


def expand(src, pos, size):
    """Unpacks one SWD stream at src[pos] into size bytes."""
    out = bytearray()
    bits = 1

    def bit():
        nonlocal bits, pos
        if bits == 1:
            bits = src[pos] | 0x100
            pos += 1
        b = bits & 1
        bits >>= 1
        return b

    def read(n, v=0):
        for _ in range(n):
            v = (v << 1) | bit()
        return v

    while len(out) < size:
        if not bit():
            out.append(src[pos])
            pos += 1
            continue
        if not bit():
            length = 2
        else:
            v = read(2)
            if v:
                length = v + 2
            else:
                v = read(4)
                if v:
                    length = v + 5
                else:
                    v = src[pos]
                    pos += 1
                    if not v:
                        break
                    length = v + 20
        nbits, base = OFFSETS[read(2)]
        v = 0
        if nbits >= 8:
            v = src[pos]
            pos += 1
            nbits -= 8
        start = len(out) - (base + read(nbits, v))
        for i in range(length):
            out.append(out[start + i])
    if len(out) != size:
        raise SystemExit(f"a segment unpacked to {len(out):#x} bytes, not {size:#x}")
    return bytes(out)


def unpack(rom):
    if rom[CONTAINER:CONTAINER + 8] != b"BRSOHYES":
        raise SystemExit("no BRSOHYES container at ROM 0x12230")
    sizes = struct.unpack(">8I", rom[CONTAINER + 8:CONTAINER + HEADER_SIZE])
    image = bytearray()
    src = CONTAINER + HEADER_SIZE
    for i in range(4):
        image += expand(rom, src, sizes[i * 2])
        src += sizes[i * 2 + 1]
    if len(image) != IMAGE_SIZE:
        raise SystemExit(f"the game image is {len(image):#x} bytes, not {IMAGE_SIZE:#x}")
    return bytes(image)


def main():
    rom = open(sys.argv[1], "rb").read()
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise SystemExit(f"{sys.argv[1]} is not Excitebike 64 (USA) (Rev 1), SHA-1 {ROM_SHA1}")
    image = unpack(rom)
    out = bytearray(rom[:IMAGE_ROM]) + image
    with open(sys.argv[2], "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
