// The Rumble Pak as the game finds it. The game probes each port with osPfsInitPak at boot and after its Controller Pak
// screen (pak.c's pak_probe_all), and starts a port's rumble with osMotorInit only when that probe fails as it does on
// a Rumble Pak: libultra's check of a Controller Pak's ID area reads back nothing it wrote there and answers
// PFS_ERR_DEVICE. N64ModernRuntime's osPfsInitPak (librecomp/src/pak.cpp) answers PFS_ERR_NOPACK for every port, so
// the game would never rumble. This file defines every function pak.cpp does, so the linker uses these and never pulls
// that object in: osPfsInitPak answers PFS_ERR_DEVICE for a port whose controller has a Rumble Pak
// (get_connected_device_info, src/main/main.cpp) and PFS_ERR_NOPACK for the others, and the rest answer
// PFS_ERR_NOPACK, as pak.cpp's do, since no Controller Pak is plugged in.
#include <cstdint>

#include "ultramodern/input.hpp"

#include "recomp.h"

static constexpr int32_t PFS_ERR_NOPACK = 1;
static constexpr int32_t PFS_ERR_DEVICE = 11;

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num);

// s32 osPfsInitPak(OSMesgQueue* mq, OSPfs* pfs, int channel)
extern "C" void osPfsInitPak_recomp(uint8_t* rdram, recomp_context* ctx) {
    const auto info = get_connected_device_info((int)ctx->r6);
    const bool rumble = info.connected_device == ultramodern::input::Device::Controller &&
        info.connected_pak == ultramodern::input::Pak::RumblePak;
    ctx->r2 = rumble ? PFS_ERR_DEVICE : PFS_ERR_NOPACK;
}

extern "C" void osPfsFreeBlocks_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void osPfsAllocateFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void osPfsDeleteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void osPfsFileState_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void osPfsFindFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void osPfsReadWriteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void osPfsChecker_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

// s32 osPfsNumFiles(OSPfs* pfs, s32* max_files, s32* files_used)
extern "C" void osPfsNumFiles_recomp(uint8_t* rdram, recomp_context* ctx) {
    MEM_W(0, ctx->r5) = 0;
    MEM_W(0, ctx->r6) = 0;
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void osPfsRepairId_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}
