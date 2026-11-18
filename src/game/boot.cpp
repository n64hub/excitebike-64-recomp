// The game's start, in place of its boot loader. On a console the boot loader (ROM 0x1000, run at 0x80000400) shows
// a splash screen, unpacks the game image from the BRSOHYES container at ROM 0x12230 (four SWD-packed segments: the
// game's code and data, the front-end code, the UsrTrk.c overlay and the rest of the data), copies it over itself to
// 0x80000400, and leaves the container's header at 0x803FFFC0 with 0xBEEF at 0x80000402 to say so (lib/excitebike64's
// docs/overview.md, Startup). The recomp never runs the boot loader: N64Recomp recompiled the unpacked image
// (tools/unpack_code.py, us.toml), so eb_entrypoint unpacks it into RDRAM, leaves the same header and magic, and starts
// the game's own entry. game_main reads them to tell it was started by the boot loader, and later unpacks the front-end
// code and UsrTrk.c from the container again after each race, at the same addresses, through the game's own code.
//
// The splash is the boot loader's only picture, and holding START on controller 1 at power-on, which skips it and
// opens the Controller Pak manager (0xDEAF at 0x80000406), is left out with it.

#include <cstdint>
#include <cstdio>
#include <span>

#include "recomp.h"
#include "librecomp/game.hpp"

#include "eb.h"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);

namespace {
    constexpr uint32_t container_rom = 0x12230;  // the cartridge's, in the PI's address space 0xB0012230
    constexpr uint32_t container_header_size = 0x28;
    constexpr gpr image_vram = (gpr)(int32_t)0x80000400;
    constexpr uint32_t image_size = 0xDC5B8;
    constexpr gpr header_vram = (gpr)(int32_t)0x803FFFC0;

    uint32_t read32(std::span<const uint8_t> rom, uint32_t at) {
        return (uint32_t)rom[at] << 24 | (uint32_t)rom[at + 1] << 16 | (uint32_t)rom[at + 2] << 8 | rom[at + 3];
    }

    void write32(uint8_t* rdram, gpr vram, uint32_t value) {
        MEM_W(0, vram) = (int32_t)value;
    }

    // SWD, John Brandwood's LZSS variant (his SWD32's SWDN64.ASM, Boost license), as the boot loader's SwdExpand and
    // tools/unpack_code.py unpack it: flag bits LSB first from each flag byte, fields read MSB first. Unpacks the
    // stream at rom[pos] into size bytes at vram and returns how many it wrote.
    uint32_t swd_expand(std::span<const uint8_t> rom, uint32_t pos, uint8_t* rdram, gpr vram, uint32_t size) {
        static constexpr struct { int bits; uint32_t base; } offsets[] = { { 5, 0x1 }, { 7, 0x21 }, { 9, 0xA1 }, { 10, 0x2A1 } };
        uint32_t flags = 1;
        uint32_t out = 0;
        auto bit = [&]() {
            if (flags == 1) {
                flags = rom[pos++] | 0x100;
            }
            uint32_t b = flags & 1;
            flags >>= 1;
            return b;
        };
        auto bits = [&](int n, uint32_t v) {
            for (int i = 0; i < n; i++) {
                v = (v << 1) | bit();
            }
            return v;
        };
        while (out < size) {
            if (!bit()) {
                MEM_B(out++, vram) = (int8_t)rom[pos++];
                continue;
            }
            uint32_t length;
            if (!bit()) {
                length = 2;
            } else if (uint32_t v = bits(2, 0)) {
                length = v + 2;
            } else if ((v = bits(4, 0)) != 0) {
                length = v + 5;
            } else if ((v = rom[pos++]) != 0) {
                length = v + 20;
            } else {
                break;
            }
            auto off = offsets[bits(2, 0)];
            int n = off.bits;
            uint32_t v = 0;
            if (n >= 8) {
                v = rom[pos++];
                n -= 8;
            }
            uint32_t from = out - (off.base + bits(n, v));
            for (uint32_t i = 0; i < length; i++, out++, from++) {
                MEM_B(out, vram) = MEM_B(from, vram);
            }
        }
        return out;
    }
}

void eb::unpack_game(uint8_t* rdram) {
    std::span<const uint8_t> rom = recomp::get_rom();
    uint32_t src = container_rom + container_header_size;
    gpr dst = image_vram;
    uint32_t total = 0;
    uint32_t packed_total = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t size = read32(rom, container_rom + 8 + i * 8);
        uint32_t packed = read32(rom, container_rom + 12 + i * 8);
        if (swd_expand(rom, src, rdram, dst, size) != size) {
            fprintf(stderr, "The game image's segment %d didn't unpack\n", i);
        }
        src += packed;
        dst += size;
        total += size;
        if (i > 0) {
            packed_total += packed;
        }
    }
    if (total != image_size) {
        fprintf(stderr, "The game image unpacked to 0x%X bytes, not 0x%X\n", total, image_size);
    }

    // The header the boot loader leaves: the container's own 0x28 bytes, the container's PI address, and the
    // resources', which follow the four segments.
    for (uint32_t i = 0; i < container_header_size; i += 4) {
        write32(rdram, header_vram + i, read32(rom, container_rom + i));
    }
    uint32_t container_pi = 0xB0000000 | container_rom;
    uint32_t s0_packed = read32(rom, container_rom + 12);
    write32(rdram, header_vram + 0x28, container_pi);
    write32(rdram, header_vram + 0x2C, container_pi + container_header_size + s0_packed + packed_total);

    // 0xBEEF in the entry's first lui, which game_main reads as the boot loader having started it.
    MEM_H(2, image_vram) = (int16_t)0xBEEF;
}

extern "C" void eb_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    eb::unpack_game(rdram);
    recomp_entrypoint(rdram, ctx);
}
