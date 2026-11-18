"""Writes syms/excitebike64.us.syms.toml and syms/excitebike64.us.datasyms.toml, the symbols N64Recomp reads (us.toml),
from the decomp's tables of USA Rev 1 (lib/excitebike64/versions/us_rev1: functions.csv, every function of both code
images with its address and size, and symbols.txt, every name with its address). Nothing is built: the decomp's
tables are what its own build links by, checked byte for byte against the ROM there.

The game image is one section, .game: VRAM 0x80000400-0x800DC9B8, its code and initialized data, which the boot loader
unpacks from the ROM's container (tools/unpack_code.py). Its ROM offset is 0x1000, where build/excitebike64.us.code.z64
holds it for N64Recomp and where the runtime maps the section at boot (it maps what lies in the 1 MB after ROM 0x1000
at the entrypoint, and src/game/boot.cpp's entry puts the unpacked image there). The boot loader, which the recomp
never runs, is left out. The front-end code and the UsrTrk.c overlay in it are given back to the heap during a race
and unpacked again at the same address after it, so they are plain code here too.

Every function of the game image is listed but the linker's padding between objects (kind "padding"): the game's C,
libultra's and the hand-written asm, the sound code among the data included. libultra's exception handler
(exceptasm.o), which the decomp's table splits at other places and partly calls padding, is listed by the names in its
libultra_map.json, each running to the next: N64Recomp knows every one of them and leaves them out, since the runtime
does what they do. func_80000400 (asm/game_entry.s) is the game image's entry, which N64Recomp names
recomp_entrypoint.

Four of the game's functions are libultra's, compiled with the game's own compiler under names of the game's
(LIBULTRA_COPIES, from their comments in the decomp): they take libultra's names here, so that N64Recomp calls the
runtime's in their place, as it does for libultra's own. The game's copy of osJamMesg works the thread queues itself,
which the runtime keeps in its own way.

Usage: py -3 -I tools/gen_syms.py
"""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION = ROOT / "lib/excitebike64/versions/us_rev1"
OUT = ROOT / "syms/excitebike64.us"

ROM = 0x1000
VRAM = 0x80000400
END = 0x800DC9B8
ENTRYPOINT = 0x80000400

LIBULTRA_COPIES = {
    "dma_epi_start": "osEPiStartDma",
    "dma_pi_get_cmd_queue": "osPiGetCmdQueue",
    "dma_cart_rom_init": "osCartRomInit",
    "dma_jam_mesg": "osJamMesg",
}

HEADER = "# Written by tools/gen_syms.py from lib/excitebike64/versions/us_rev1: never edit by hand.\n"


def section(lines):
    return ("[[section]]\nname = \".game\"\nrom = 0x%08X\nvram = 0x%08X\nsize = 0x%X\n\n" % (ROM, VRAM, END - VRAM)
            + lines)


def exception_code():
    """libultra's exception handler (exceptasm.o) from libultra_map.json: its range, each object of libultra running
    to the next one's .text, and its functions by name, each running to the next."""
    objects = json.load(open(VERSION / "libultra_map.json"))["images"]["game"]["objects"]
    starts = sorted((int(o[".text"], 16), o["obj"]) for o in objects if ".text" in o)
    (start, end), = [(a, b) for (a, name), (b, _) in zip(starts, starts[1:]) if name.endswith("/exceptasm.o")]
    obj, = [o for o in objects if o["obj"].endswith("/exceptasm.o")]
    names = sorted((int(a, 16), n) for n, a in obj["functions"].items())
    ends = [a for a, _ in names[1:]] + [end]
    return (start, end), [(a, e - a, n) for (a, n), e in zip(names, ends)]


def main():
    (start, end), funcs = exception_code()
    for row in csv.DictReader(open(VERSION / "functions.csv", newline="")):
        if row["image"] != "game" or row["kind"] == "padding":
            continue
        addr = int(row["address"], 16)
        size = int(row["size"])
        if not VRAM <= addr < addr + size <= END:
            raise SystemExit(f"{row['name']} at {addr:#x} is outside the game image")
        if start <= addr < end:
            continue
        name = "recomp_entrypoint" if addr == ENTRYPOINT else LIBULTRA_COPIES.get(row["name"], row["name"])
        funcs.append((addr, size, name))
    funcs.sort()
    for (a, s, n), (b, _, m) in zip(funcs, funcs[1:]):
        if a + s > b:
            raise SystemExit(f"{n} overlaps {m}")

    data = []
    for line in open(VERSION / "symbols.txt"):
        if line.startswith("#") or not line.strip():
            continue
        name, image, addr, kind = line.split()[:4]
        addr = int(addr, 16)
        if image == "game" and kind == "data" and VRAM <= addr < END:
            data.append((addr, name))
    data.sort()

    OUT.parent.mkdir(exist_ok=True)
    body = "functions = [\n" + "".join(
        "    { name = \"%s\", vram = 0x%08X, size = 0x%X },\n" % (n, a, s) for a, s, n in funcs) + "]\n"
    (OUT.parent / (OUT.name + ".syms.toml")).write_text(HEADER + section(body), newline="\n")
    body = "symbols = [\n" + "".join(
        "    { name = \"%s\", vram = 0x%08X },\n" % (n, a) for a, n in data) + "]\n"
    (OUT.parent / (OUT.name + ".datasyms.toml")).write_text(HEADER + section(body), newline="\n")
    print(f"{len(funcs)} functions, {len(data)} data symbols")


if __name__ == "__main__":
    main()
