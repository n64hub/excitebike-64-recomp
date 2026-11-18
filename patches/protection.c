// The game's copy protection, answering as a genuine cartridge does. Each check reads what a console's CIC-6103 and
// its IPL3 leave behind (osCicId, two words of the IPL3 in RDRAM and two on the cartridge, the first words of the RSP's
// memories), and a failed one quietly spoils the game: words of RDRAM overwritten at random, a lag generator, a flag
// the race reads. The recomp has no IPL3 run before it and no RSP memory to read, so each says the cartridge is real.

#include "patches.h"

// osCicId is 6103's.
RECOMP_PATCH s32 cic_is_6103(void) {
    return 1;
}

// The IPL3's sw $t1, 0($at) at 0xA0000200 of RDRAM.
RECOMP_PATCH s32 cic_check_rdram_200(void) {
    return 1;
}

// The IPL3's li $t3, 0x17D7 at 0xA0000284 of RDRAM.
RECOMP_PATCH s32 cic_check_rdram_284(void) {
    return 1;
}

// 0x6C07 and 0x8965 at 0x574 and 0x578 of the cartridge's IPL3, read by the PI.
RECOMP_PATCH s32 main_check_cart_ipl3(void) {
    return 1;
}

// 0x17D7 at the start of the RSP's instruction memory.
RECOMP_PATCH s32 shell_check_sp_imem_magic(void) {
    return 1;
}

// 0xFFFFFFFF at the start of the RSP's data memory.
RECOMP_PATCH s32 shell_check_sp_dmem_magic(void) {
    return 1;
}
