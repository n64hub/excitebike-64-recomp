// A ponytail's first frames in a race. ponytail_init (the decomp's src/game/ponytail.c) allocates a rider's ponytail
// matrices, three bones for each of the two frame buffers (0x180 bytes), and leaves them as the heap had them; only
// ponytail_update writes them, the frame buffer's half, from rider_update_pose. render_draw_race poses a far CPU rider
// only two frames in four (one screen, body detail below high), and draws its ponytail every frame from the half of
// the frame buffer it draws into. So where the riders first show posed on one frame and skipped on the next, as the
// race's intro cuts to the starting gate, the second frame drew a ponytail from the half nothing had written yet: a
// polygon across much of the view, in the rider's colours, for one frame.
//
// So the first ponytail_update after a rider's ponytail is set up writes its matrices into both halves, and every one
// after it into its frame buffer's half alone, as the game's. ponytail_free, which frees them, marks the rider's
// ponytail as new again; both are the decomp's (src/game/ponytail.c) but for that.

#include "patches.h"

#define gGfxFrameIndex (*(s32 *)0x800E3A50)
#define gRiderGfx ((u8 *)0x800DCA70)
#define RIDER_GFX_SIZE 0xA80
#define PONYTAIL_MTX_SIZE 0xC0
#define MAX_RIDERS 8

void ponytail_simulate(s32 idx, void *skel);
void mtx_from_bones(void *bones, void *mtx, s32 n);
void mem_free(void *p);

// Whether each rider's ponytail matrices have both halves written since they were allocated.
static u8 sPonytailBothWritten[MAX_RIDERS];

RECOMP_PATCH void ponytail_update(s32 idx, void *skel, void *bones, void *mtx) {
    ponytail_simulate(idx, skel);
    mtx_from_bones(bones, mtx, 3);
    if ((idx >= 0) && (idx < MAX_RIDERS) && !sPonytailBothWritten[idx]) {
        u8 *other = (u8 *)mtx + ((gGfxFrameIndex == 0) ? PONYTAIL_MTX_SIZE : -PONYTAIL_MTX_SIZE);
        mtx_from_bones(bones, other, 3);
        sPonytailBothWritten[idx] = 1;
    }
}

RECOMP_PATCH void ponytail_free(s32 idx) {
    u8 *entry = gRiderGfx + idx * RIDER_GFX_SIZE; // 0xA48 nodes, 0xA4C mtx, 0xA50 bones, 0xA58 a58

    if (*(void **)(entry + 0xA58) != NULL) {
        mem_free(*(void **)(entry + 0xA58));
    }
    if (*(void **)(entry + 0xA50) != NULL) {
        mem_free(*(void **)(entry + 0xA50));
    }
    if (*(void **)(entry + 0xA4C) != NULL) {
        mem_free(*(void **)(entry + 0xA4C));
    }
    if (*(void **)(entry + 0xA48) != NULL) {
        mem_free(*(void **)(entry + 0xA48));
    }
    *(void **)(entry + 0xA58) = NULL;
    *(void **)(entry + 0xA50) = NULL;
    *(void **)(entry + 0xA4C) = NULL;
    *(void **)(entry + 0xA48) = NULL;
    if ((idx >= 0) && (idx < MAX_RIDERS)) {
        sPonytailBothWritten[idx] = 0;
    }
}
