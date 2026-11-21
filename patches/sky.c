// The race's sky down the whole of each view, as on a stadium track. After each race frame the game reads the Z buffer
// back (sky_scan_zbuffer) to find the lowest row the sky still shows in, every 16th row and 8th column of the view,
// and draws the sky only down to there. With RT64's Render to RAM on (src/main/main.cpp: the stadium lights' flares
// read the Z buffer too), that copy is the 4:3 frame's: in a wider window the track reaches the window's sides
// (src/game/widescreen.cpp), where it may lie lower than anywhere the scan looks, and the sky would stop above it
// there, leaving the sides between them with what earlier frames drew. So every view's sky reaches its bottom, as it
// did with no Z buffer in RAM for the scan to find, and the track draws over it.

#include "patches.h"

#define gNumViews (*(s32 *)0x800C2ED4)
#define sSkyPrevClipHeight ((s32 *)0x800CA1F4)
#define gViewRects ((ViewRect (*)[4])0x800E2038)
#define sSkyClipHeight ((s32 *)0x800E9880)

typedef struct ViewRect {
    s32 x, y, w, h;
} ViewRect;

RECOMP_PATCH void sky_scan_zbuffer(s32 frame) {
    s32 i;
    s32 layout = gNumViews - 1;

    for (i = 0; i < gNumViews; i++) {
        sSkyClipHeight[i] = gViewRects[layout][i].h;
        sSkyPrevClipHeight[i] = sSkyClipHeight[i];
    }
}
