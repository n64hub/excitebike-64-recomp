// libultra functions the game links that N64Recomp leaves out (its reimplemented and ignored functions) and the runtime
// doesn't implement, so each call to one in RecompiledFuncs/ comes here.
#include <chrono>
#include <cstdint>

#include "recomp.h"
#include "librecomp/game.hpp"

// osPiReadIo(u32 devAddr, u32* data): one word of the cartridge by the PI's I/O. The game reads the RSP's boot code
// from the cartridge's IPL3 with it (ROM 0xB70, game_main), a word at a time, and its graphics tasks boot from that.
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t dev_addr = (uint32_t)ctx->r4 & 0x1FFFFFFF;
    recomp::do_rom_pio(rdram, ctx->r5, dev_addr);
    ctx->r2 = 0;
}

// The controller's accessory bus, which the game's own Controller Pak code (pak.c) calls. The Controller Pak is the
// runtime's (librecomp/src/pak.cpp), whose osPfs functions read and write its file without the bus, so these answer
// that no pak answers on it, and osPfsIsPlug reports the ports the runtime holds a Controller Pak in.
static constexpr int32_t PFS_ERR_NOPACK = 1;

extern "C" void __osPfsGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

// osPfsIsPlug(OSMesgQueue* mq, u8* pattern): a bit for each port with a pak in it.
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    MEM_BU(0, ctx->r5) = recomp::get_controller_pak_ports() & 0xF;
    ctx->r2 = 0;
}

// osViGetCurrentLine(): the line the VI is drawing, of NTSC's 262 lines a field, from the clock as a console's beam
// moves. The game's scheduler reads it at each retrace and before each swap, and puts a swap off while it is below
// line 10 (sched.c), so a constant there would stall it; on reset it waits for line 100 to pass three times.
extern "C" void osViGetCurrentLine_recomp(uint8_t* rdram, recomp_context* ctx) {
    using namespace std::chrono;
    constexpr int64_t field_ns = 1000000000 / 60;
    int64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    ctx->r2 = (int32_t)((now % field_ns) * 262 / field_ns);
}

// __osSpGetStatus(): the RSP's status register, which the main loop notes each frame (sSpStatus) and nothing reads
// for more: halted, as the RSP is between tasks.
extern "C" void __osSpGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    constexpr int32_t SP_STATUS_HALT = 0x1;
    ctx->r2 = SP_STATUS_HALT;
}
