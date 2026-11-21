// The Rumble Pak beside the Controller Pak. Port 1 holds a Controller Pak, the runtime's (librecomp/src/pak.cpp, turned
// on by the GameEntry's controller_pak_ports in src/main/main.cpp), for the Time Trial ghosts and the tracks a player
// copies there. The game probes each port with osPfsInitPak at boot and after its Controller Pak screen (pak.c's
// pak_probe_all and pak_probe_port), and starts a port's rumble with osMotorInit only when that probe fails as it does
// on a Rumble Pak (PFS_ERR_DEVICE or PFS_ERR_ID_FATAL), as a console's port holds one or the other. The runtime answers
// PFS_ERR_NOPACK in a port without a Controller Pak, so the game would never rumble.
//
// Two hooks in us.toml, on the instruction after each probe's osPfsInitPak call, run eb_pak_probed with its answer and
// the port: a port with a Controller Pak or none gets the Rumble Pak too when its controller has one
// (get_connected_device_info, src/main/main.cpp), started as the game starts it (osMotorInit into gRumblePfs[port],
// the motor stopped, the port's bit in gRumblePakMask). Both probes clear that bit before they call osPfsInitPak and
// set no bit after an answer of 0 or PFS_ERR_NOPACK, so the bit stays. The game's rumble reads only gRumblePakMask and
// gRumblePfs, and its Controller Pak code only sPakValidMask and sPakFs, so each pak works as it does alone.
#include <cstdint>

#include "ultramodern/ultra64.h"

#include "recomp.h"

namespace {
// pak.c's data, USA Rev 1 (the decomp's versions/us_rev1/symbols.txt).
constexpr int32_t gSiMesgQueue = (int32_t)0x800E2568;
constexpr int32_t gRumblePakMask = (int32_t)0x800E2580;
constexpr int32_t gRumblePfs = (int32_t)0x800E2780;
constexpr int32_t sizeof_OSPfs = 0x68;
constexpr int32_t PFS_ERR_NOPACK = 1;
}

extern "C" void eb_pak_probed(uint8_t* rdram, int32_t answer, int32_t port) {
    if ((answer != 0 && answer != PFS_ERR_NOPACK) || port < 0 || port >= 4) {
        return;
    }
    gpr pfs = (gpr)(gRumblePfs + port * sizeof_OSPfs);
    if (osMotorInit(rdram, (gpr)gSiMesgQueue, pfs, port) != 0) {
        return;
    }
    __osMotorAccess(rdram, pfs, 0); // MOTOR_STOP
    MEM_W(0, (gpr)gRumblePakMask) = MEM_W(0, (gpr)gRumblePakMask) | (1 << port);
}
