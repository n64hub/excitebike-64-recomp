// The riders' shadows on the ground, between the game's frames. shadow_render_caster (the decomp's src/game/shadow.c)
// draws each rider and bike from above into a 32x32 texture, and shadow_draw lays it in black over the ground under
// the rider: shadow_build_mesh takes the ground's triangles within a metre of the rider (height_collect_ground_tris),
// clips them to that 2 m square, places them from the rider (render_draw_race draws them under the bike's matrix, a
// translation to the rider) and maps the square onto the texture. Its triangles, and how many there are, change from
// frame to frame as the ground's enter and leave the square, and each frame keeps no more than 32 vertices, so
// a clipped square over more of the ground's triangles than that lost some.
//
// RT64 pairs a draw with the last frame's by what it draws, its triangle count among it, so a shadow whose count
// changed was drawn where the game's frame left it while its bike moved on between frames: it lagged behind the bike
// and caught up on the game's next frame. Where the count held, its matrix moved on with the bike, carrying the last
// frame's triangles over ground of another shape (down into a dune's crest, up off a slope's foot), and where RT64
// took the old triangles' vertices for the new ones' and moved each from one to the other, the shadow tore into
// stair-stepped pieces.
//
// So each shadow is a grid of 9 by 9 vertices across the square instead, every vertex set on the ground under it,
// the highest of the ground's triangles there (each one where the nearest set beside it is, where none is under it),
// in the same place in the grid every frame. Under a matrix group of its own, one for each shadow in each view, RT64
// pairs it with the last frame's whatever its ground, and moves each vertex from its last place on the ground to its
// new one: the shadow goes with its bike and over the ground between frames. A rider that jumps further than a
// shadow moves in a frame (back on the track after a crash) takes a new group, so its shadow isn't swept across. On
// the game's own frames the grid draws the ground as the triangles did, to within the sag of a crease between two of
// its vertices, a quarter metre apart.
//
// In frame dumps of a Kyoto race and a Desert race at 120 Hz, RT64 paired 266 of the 1755 shadow draws and 246 of the
// 1221 with the last frame's, and with the grid 1750 of 1755 and 1219 of 1229 (each shadow's first draw has none).
// The red and yellow hexagons that cross the shadows in the stadiums are the stadium lights' lens flare
// (patches/sun.c), drawn over the scene after them, not the shadows.

#include "patches.h"

#include "common.h"
#include "constants.h"
#include "structs.h"
#include "rt64_extended_gbi.h"

#define gCheatFlags (*(s32 *)0x800C2BC0)
#define gRiderAlpha ((s32 *)0x800C2CCC)
#define gCullMode (*(s32 *)0x800E22EC)
#define gGfxFrameIndex (*(s32 *)0x800E3A50)
#define gRiders ((Rider *)0x800E4160)
#define gGroundTris ((GroundTri *)0x800E8A78)
#define gGroundTriCount (*(s32 *)0x800E9708)
#define sShadowTextures (*(u8 **)0x800E9C7C)
#define gLightningFlash (*(s32 *)0x800F1E88)

void height_collect_ground_tris(s32 x0, s32 z0, s32 x1, s32 z1, s32 hy);

#define SHADOW_SLOTS 16
#define SHADOW_HALF 1000
#define GRID_CELLS 8
#define GRID_SIDE (GRID_CELLS + 1)
#define GRID_STEP ((2 * SHADOW_HALF) / GRID_CELLS)
// Rows of the grid loaded at once: 3 rows of 9, 27 of the RSP's 32 vertices, draw the two rows of cells between them.
#define LOAD_ROWS 3
// A rider moved further than this in a frame (mm) is somewhere else, not on its way.
#define SHADOW_JUMP 8000
// The shadows' matrix groups: this, with the view and slot below and a generation above them.
#define SHADOW_GROUP_ID 0x53480000
#define SHADOW_ABS(x) (((x) < 0) ? -(x) : (x))

static Vtx sGridVtx[2][SHADOW_SLOTS][GRID_SIDE * GRID_SIDE];
static u8 sGridValid[SHADOW_SLOTS];
static u8 sGeneration[SHADOW_SLOTS];
static u8 sHasLast[SHADOW_SLOTS];
static Vec3i sLastCenter[SHADOW_SLOTS];

// The height of the highest ground triangle under (x, z), all relative to the rider, or HEIGHT_NONE.
static s32 ground_height(s32 x, s32 z) {
    s32 best = HEIGHT_NONE;
    s32 i;

    for (i = 0; i < gGroundTriCount; i++) {
        const Vec *v = gGroundTris[i].v;
        f32 x0 = v[0].x, z0 = v[0].z;
        f32 e1x = v[1].x - x0, e1z = v[1].z - z0;
        f32 e2x = v[2].x - x0, e2z = v[2].z - z0;
        f32 det = (e1x * e2z) - (e2x * e1z);
        f32 px, pz, a, b, eps;
        s32 y;

        if ((det > -1.0f) && (det < 1.0f)) {
            continue;
        }
        px = x - x0;
        pz = z - z0;
        a = ((px * e2z) - (e2x * pz)) / det;
        b = ((e1x * pz) - (px * e1z)) / det;
        eps = 1.0f / 1024.0f;
        if ((a < -eps) || (b < -eps) || ((a + b) > (1.0f + eps))) {
            continue;
        }
        y = v[0].y + (s32)((a * (v[1].y - v[0].y)) + (b * (v[2].y - v[0].y)));
        if ((best == HEIGHT_NONE) || (y > best)) {
            best = y;
        }
    }
    return best;
}

RECOMP_PATCH void shadow_build_mesh(s32 slot) {
    s32 heights[GRID_SIDE * GRID_SIDE];
    s32 cx, cy, cz;
    s32 i, j, k, found, pass;
    Vtx *vtx;

    if ((slot < 0) || (slot >= SHADOW_SLOTS)) {
        return;
    }
    sGridValid[slot] = 0;
    if (slot < RIDER_COUNT) {
        cx = gRiders[slot].drawPos.x;
        cy = gRiders[slot].drawPos.y;
        cz = gRiders[slot].drawPos.z;
    } else {
        cx = gRiders[slot % RIDER_COUNT].drawBodyPos.x;
        cy = gRiders[slot % RIDER_COUNT].drawBodyPos.y;
        cz = gRiders[slot % RIDER_COUNT].drawBodyPos.z;
    }
    // The original's height for the search, gRiders[slot] past gRiders for a thrown rider's, kept as it is.
    height_collect_ground_tris(cx - SHADOW_HALF, cz - SHADOW_HALF, cx + SHADOW_HALF, cz + SHADOW_HALF,
                               gRiders[slot].drawPos.y + 1000);
    if (gGroundTriCount == 0) {
        return;
    }
    for (i = 0; i < gGroundTriCount; i++) {
        for (j = 0; j < 3; j++) {
            gGroundTris[i].v[j].x -= cx;
            gGroundTris[i].v[j].y -= cy;
            gGroundTris[i].v[j].z -= cz;
        }
    }

    found = 0;
    for (i = 0; i < GRID_SIDE; i++) {
        for (j = 0; j < GRID_SIDE; j++) {
            heights[(i * GRID_SIDE) + j] = ground_height((j * GRID_STEP) - SHADOW_HALF, SHADOW_HALF - (i * GRID_STEP));
            found += (heights[(i * GRID_SIDE) + j] != HEIGHT_NONE);
        }
    }
    if (found == 0) {
        return;
    }
    // A vertex with no ground under it (past the track's edge) takes a set neighbour's height, row and column first.
    for (pass = 0; (found < (GRID_SIDE * GRID_SIDE)) && (pass < (2 * GRID_SIDE)); pass++) {
        for (k = 0; k < (GRID_SIDE * GRID_SIDE); k++) {
            s32 r = k / GRID_SIDE, c = k % GRID_SIDE;
            s32 n = HEIGHT_NONE;

            if (heights[k] != HEIGHT_NONE) {
                continue;
            }
            if ((c > 0) && (heights[k - 1] != HEIGHT_NONE)) {
                n = heights[k - 1];
            } else if ((c < (GRID_SIDE - 1)) && (heights[k + 1] != HEIGHT_NONE)) {
                n = heights[k + 1];
            } else if ((r > 0) && (heights[k - GRID_SIDE] != HEIGHT_NONE)) {
                n = heights[k - GRID_SIDE];
            } else if ((r < (GRID_SIDE - 1)) && (heights[k + GRID_SIDE] != HEIGHT_NONE)) {
                n = heights[k + GRID_SIDE];
            }
            if (n != HEIGHT_NONE) {
                heights[k] = n;
                found++;
            }
        }
    }

    vtx = sGridVtx[gGfxFrameIndex & 1][slot];
    for (i = 0; i < GRID_SIDE; i++) {
        for (j = 0; j < GRID_SIDE; j++) {
            s32 x = (j * GRID_STEP) - SHADOW_HALF;
            s32 z = SHADOW_HALF - (i * GRID_STEP);
            s32 y = heights[(i * GRID_SIDE) + j];
            Vtx *v = &vtx[(i * GRID_SIDE) + j];

            v->v.ob[0] = x;
            y = (y == HEIGHT_NONE) ? 0 : y;
            v->v.ob[1] = (y < -0x7FFF) ? -0x7FFF : ((y > 0x7FFF) ? 0x7FFF : y);
            v->v.ob[2] = z;
            v->v.flag = 0;
            v->v.tc[0] = ((x + SHADOW_HALF) << 11) / (2 * SHADOW_HALF);
            v->v.tc[1] = ((-z + SHADOW_HALF) << 11) / (2 * SHADOW_HALF);
            v->v.cn[0] = v->v.cn[1] = v->v.cn[2] = v->v.cn[3] = 0xFF;
        }
    }

    if (sHasLast[slot] && ((SHADOW_ABS(cx - sLastCenter[slot].x) > SHADOW_JUMP) ||
                           (SHADOW_ABS(cy - sLastCenter[slot].y) > SHADOW_JUMP) ||
                           (SHADOW_ABS(cz - sLastCenter[slot].z) > SHADOW_JUMP))) {
        sGeneration[slot]++;
    }
    sLastCenter[slot].x = cx;
    sLastCenter[slot].y = cy;
    sLastCenter[slot].z = cz;
    sHasLast[slot] = 1;
    sGridValid[slot] = 1;
}

RECOMP_PATCH Gfx *shadow_draw(s32 slot, s32 unused, Gfx *gdl, s32 view) {
    Vtx *vtx;
    s32 row, r, c;

    if ((slot < 0) || (slot >= SHADOW_SLOTS) || !sGridValid[slot]) {
        return gdl;
    }
    vtx = sGridVtx[gGfxFrameIndex & 1][slot];
    gDPPipeSync(gdl++);
    gDPSetRenderMode(gdl++, 0x404A50, 0);
    gDPSetCombine(gdl++, 0xFFB3FF, 0xFF65FEFF);
    if (gLightningFlash == 0) {
        gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, gRiderAlpha[slot % RIDER_COUNT] * 97 / 256);
    } else {
        gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, gRiderAlpha[slot % RIDER_COUNT]);
    }
    gDPSetBlendColor(gdl++, 0, 0, 0, 1);
    gDPSetAlphaCompare(gdl++, G_AC_THRESHOLD);
    gDPSetTextureLUT(gdl++, G_TT_NONE);
    gDPLoadTextureBlock(gdl++, sShadowTextures + (slot << 10), G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                        G_TX_CLAMP, G_TX_CLAMP, 6, 6, G_TX_NOLOD, G_TX_NOLOD);
    // The grid faces up as the ground does whichever way its triangles wind, so it is drawn from both sides.
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);
    gEXEnable(gdl++);
    gEXMatrixGroupSimple(gdl++, SHADOW_GROUP_ID | (sGeneration[slot] << 8) | ((view & 0xF) << 4) | slot, G_EX_PUSH,
                         G_MTX_MODELVIEW, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                         G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR,
                         G_EX_EDIT_NONE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
    for (row = 0; row < (GRID_SIDE - 1); row += (LOAD_ROWS - 1)) {
        gSPVertex(gdl++, &vtx[row * GRID_SIDE], LOAD_ROWS * GRID_SIDE, 0);
        for (r = 0; r < (LOAD_ROWS - 1); r++) {
            for (c = 0; c < GRID_CELLS; c++) {
                s32 a = (r * GRID_SIDE) + c;
                gSP2Triangles(gdl++, a, a + GRID_SIDE, a + 1, 0, a + 1, a + GRID_SIDE, a + GRID_SIDE + 1, 0);
            }
        }
    }
    gEXPopMatrixGroup(gdl++, G_MTX_MODELVIEW);
    gSPSetGeometryMode(gdl++, gCullMode);
    gDPPipeSync(gdl++);
    gDPSetTextureLUT(gdl++, G_TT_RGBA16);
    gDPSetAlphaCompare(gdl++, G_AC_NONE);
    gDPPipeSync(gdl++);
    return gdl;
}
