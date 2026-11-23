// The Desert's ground out to the window's edges. The Desert draws no track models: its ground is a height map drawn as
// patches of 5x5 vertices in three rings of detail round the camera (the decomp's src/game/desgfx.c, copied here as it
// is but for the changes below). des_calc_lod_grid marks the cells of a 32x32 grid each ring draws, by where a cell
// lies from the camera: ahead of it, and no further across than half its distance ahead, plus 360 units (22.5 m) for
// the finest ring and 720 for the coarser ones, room for a cell's own size and for the ground the view sees below the
// camera close to it. That is the 4:3 view's width, so in a wider one the dunes past it were never drawn and the sky
// showed where they should be, with the cacti and rocks (fractal_get_visible_objects, which lets through 90 degrees, as
// far across as ahead) still standing over it.
//
// So the half-width, both its slope and its room, is widened as the stadium's and the outdoor tracks' are
// (src/game/widescreen.cpp's eb_cull_scale, by the view's width over the game's and, with Aspect Ratio Expand, the
// window's aspect ratio over 4:3), which eb_desert_cull_scale gives in 1024ths. With the slope alone, a 32:9 window
// still showed sky in its bottom corners, where the ground is close to the camera.
//
// The game drops a cell whose middle is behind the camera and more than 360 units across from it outright: at 4:3 its
// front edge, at most a cell's half diagonal (354 units) ahead, is never in the view. A wider and taller view's bottom
// corners reach it, and with a heading near one of the grid's axes and the camera in the right part of its cell a whole
// cell beside the camera went missing for a moment: a sky-blue wedge in a bottom corner, whose top was the cell's front
// edge. So a cell from 359 units behind is dropped by the same half-width as one ahead, its room less half as far
// across as it is behind, and only one further behind outright. Over 40000 headings, places, pitches and slopes the
// game's rule hid a cell the view saw at about 1 in 1000 at 16:9 and 1 in 70 at 32:9, and this one at none; every
// frame of a 70 s first-person ride showed the wedge 12 times at 32:9 and once at 16:9 by the game's rule, never by
// this one.
//
// A wider view needs more patches than the game keeps. des_update_patches takes each cell's patch from a cache of 250
// (DES_NUM_PATCHES), and one not used for a frame is freed only the frame after, so a turning view holds two frames'
// worth. Counted by des_calc_lod_grid's own rules over 3000 places and headings, turning 0x600 and moving 1400 units
// a frame: 182 at 4:3, 256 at 16:9, 325 at 21:9, 479 at 32:9, and 704 with every cell ahead of the camera, however
// wide. Once the cache is used up the game stops taking patches, the coarser rings last, so the far dunes went first.
// So des_init takes a cache of 768 (DES_POOL_SIZE) from the game's heap, which the front end's code is given back to
// during a race, and the game's 250 if the heap hasn't room; des_update_view and des_update_patches use what it took.
// The free list is this file's, since the game's (sDesFreePatches) holds 250; des_free frees the cache as the game's.
// At most 15 blocks hold the patches a frame draws by the same count, under sDesBlockMtx's 32.

#include "patches.h"

// The decomp's types and constants, without its prototypes and globals: the functions this calls are declared here,
// the globals defined below.
#include "common.h"
#include "constants.h"
#include "structs.h"

#define DES_POOL_SIZE 768

void *res_load(ResSet *set, s32 id);
void *mem_alloc_dbg(s32 size, u32 flags, s32 owner, s32 line);
void mem_fill(u8 *dst, u8 value, s32 len);
void fractal_init(u8 *map);
void fractal_reset(void);
void fractal_generate_step(s32 steps);
void fractal_update_tiles(s32 camX, s32 camZ);
s32 fractal_get_visible_objects(s32 x, s32 z, s32 yaw, s32 range, DesertObj *out, s32 max);
void divmod_floor(s32 num, s32 den, s32 *q, s32 *r);
void mtx_from_angles_pos_scale(MtxS *m, s32 rx, s32 ry, s32 rz, s32 tx, s32 ty, s32 tz, s32 scale);
void mtx_mul(s16 *a, s16 *b, s16 *out);
void des_build_patch(s32 index, s32 x, s32 z, s32 lod);
Gfx *des_build_patch_dl(s32 seams);
void des_calc_lod_grid(void);
void des_update_patches(void);

// How much wider than the game's the view is, in 1024ths (src/game/widescreen.cpp).
s32 eb_desert_cull_scale(void);

// The game's data in its image, by name (syms/'s data symbols).
extern s16 gCosTable[];
extern DesObjModel sDesObjModels[];
extern s32 sDesTerrainTexResId;
extern s32 sDesObjTexResId;
extern s32 sDesArrowTexResId;
extern s32 sDesArrowModelResId;
extern s32 sDesMarkerAnimResId;
extern s32 D_800D0BC8;
extern s32 sStrDesgfxC[];
extern s32 sDesArrowAngle;
extern s32 sDesMarkerFrame;

// And past the image, by address (the decomp's versions/us_rev1/symbols.txt).
#define gFarClip (*(s32 *)0x800C2D30)
#define gGfxFrameIndex (*(s32 *)0x800E3A50)
#define gRaceFrame (*(s32 *)0x800E8070)
#define gResourceSet (*(ResSet **)0x800E80C8)
#define gObjectTexSet (*(TexSet **)0x800E81BC)
#define gTerrainTexSet (*(TexSet **)0x800E81DC)
#define gCamYaw ((s32 *)0x800E89A0)
#define gCamPos ((Vec3i *)0x800E8A30)
#define sDesLodCellX (*(s32 *)0x800EB160)
#define sDesLodCellZ (*(s32 *)0x800EB164)
#define sDesObjects ((DesertObj *)0x800EB168)
#define sDesArrowModel (*(PrimModel **)0x800EB328)
#define D_800EA98C (*(s32 *)0x800EB32C)
#define gDesertHeightMap (*(s8 **)0x800EB330)
#define sDesPatchGridOrigin ((s32 *)0x800EB338)
#define sDesPatchDLs ((Gfx **)0x800EB348)
#define sDesBlockMtx ((Mtx (*)[32])0x800EB388)
#define sDesFreePatchCount (*(s32 *)0x800EC388)
#define sDesBlockCount (*(s32 *)0x800EC38C)
#define sDesLodGrid ((u8 *)0x800EC390)
#define sDesCamBlockOffX (*(s32 *)0x800EC790)
#define sDesCamBlockOffZ (*(s32 *)0x800EC794)
#define sDesObjMtx ((Mtx **)0x800EC798)
#define sDesArrowTex (*(TexSet **)0x800EC7A4)
#define sDesPatchGrid ((s16 (*)[0x400])0x800EC7A8)
#define sDesObjectCount (*(s32 *)0x800EDFA8)
#define sDesMarkerMtx ((Mtx *)0x800EDFB0)
#define sDesCamCellX (*(s32 *)0x800EE030)
#define sDesCamCellZ (*(s32 *)0x800EE034)
#define sDesCamX (*(s32 *)0x800EE038)
#define sDesCamY (*(s32 *)0x800EE03C)
#define sDesCamZ (*(s32 *)0x800EE040)
#define gDesBlockX ((s32 *)0x800EE048)
#define gDesBlockZ ((s32 *)0x800EE0C8)
#define sDesLodOffX (*(s32 *)0x800EE148)
#define sDesPatches (*(DesPatch **)0x800EE14C)
#define sDesLodOffZ (*(s32 *)0x800EE150)
#define sDesLod0ShiftX (*(s32 *)0x800EE550)
#define sDesArrowMtx (*(Mtx **)0x800EE554)
#define sDesLod1ShiftX (*(s32 *)0x800EE558)
#define sDesLod0ShiftZ (*(s32 *)0x800EE55C)
#define sDesLod2ShiftX (*(s32 *)0x800EE560)
#define sDesLod1ShiftZ (*(s32 *)0x800EE564)
#define sDesLod2ShiftZ (*(s32 *)0x800EE568)
#define sDesSeamFlags ((u8 *)0x800EE570)
#define sDesMarkerAnim (*(TexSet **)0x800EE970)
#define sDesCamBlockX (*(s32 *)0x800EE974)
#define sDesCamBlockZ (*(s32 *)0x800EE978)

// The patches des_init took, and the free ones' indices.
static s32 sPoolSize;
static s32 sFreePatches[DES_POOL_SIZE];

/* Loads the desert: textures, object models, the patch cache, the height map and its tiles, the 16 seam display
   lists, the arrow and the checkpoint marker. */
RECOMP_PATCH void des_init(void) {
    s32 i;

    gFarClip = 0x4000;
    gTerrainTexSet = res_load(gResourceSet, sDesTerrainTexResId);
    for (i = 0; sDesObjModels[i].res != NULL; i++) {
        sDesObjModels[i].model = (PrimModel *)res_load(gResourceSet, *sDesObjModels[i].res);
    }
    gObjectTexSet = res_load(gResourceSet, sDesObjTexResId);
    // The wider cache, or the game's.
    sPoolSize = DES_POOL_SIZE;
    sDesPatches = mem_alloc_dbg(DES_POOL_SIZE * sizeof(DesPatch), 0, (s32)sStrDesgfxC, 0xe9);
    if (sDesPatches == NULL) {
        sPoolSize = DES_NUM_PATCHES;
        sDesPatches = mem_alloc_dbg(DES_NUM_PATCHES * sizeof(DesPatch), 0, (s32)sStrDesgfxC, 0xe9);
    }
    for (i = 0; i < sPoolSize; i++) {
        sDesPatches[i].life = 0;
    }
    gDesertHeightMap = mem_alloc_dbg(DESERT_MAP_SIZE * DESERT_MAP_SIZE, 0, (s32)sStrDesgfxC, 0xed);
    fractal_init((u8 *)gDesertHeightMap);
    fractal_reset();
    for (i = 0; i < 16; i++) {
        sDesPatchDLs[i] = des_build_patch_dl(i);
    }
    // The decomp's mem_fill_2, mem_fill under another name.
    mem_fill((u8 *)sDesPatchGrid, 0xFF, 3 * 0x400 * sizeof(s16));
    mem_fill((u8 *)sDesPatchGridOrigin, 0, 3 * sizeof(s32));
    sDesFreePatchCount = sPoolSize;
    for (i = sPoolSize - 1; i >= 0; i--) {
        sFreePatches[i] = i;
    }
    sDesObjMtx[0] = mem_alloc_dbg(DES_MAX_OBJECTS * sizeof(Mtx), 0, (s32)sStrDesgfxC, 0xfe);
    sDesObjMtx[1] = mem_alloc_dbg(DES_MAX_OBJECTS * sizeof(Mtx), 0, (s32)sStrDesgfxC, 0xff);
    sDesArrowTex = (TexSet *)res_load(gResourceSet, sDesArrowTexResId);
    sDesArrowModel = (PrimModel *)res_load(gResourceSet, sDesArrowModelResId);
    sDesArrowMtx = mem_alloc_dbg(2 * sizeof(Mtx), 0, (s32)sStrDesgfxC, 0x104);
    sDesMarkerAnim = (TexSet *)res_load(gResourceSet, sDesMarkerAnimResId);
    D_800EA98C = (s32)res_load(gResourceSet, D_800D0BC8);
}

/* A frame of the desert: generates more terrain, picks the patches of each ring around the camera, makes the
   matrices of the blocks they lie in and of the visible objects, the arrow and the marker. */
RECOMP_PATCH void des_update_view(s32 view) {
    s32 i;
    s32 j;
    Mtx rot;
    Mtx place;

    sDesMarkerFrame = gRaceFrame / 4;
    fractal_generate_step(100);
    sDesCamX = gCamPos[0].x;
    sDesCamY = gCamPos[0].y;
    sDesCamZ = gCamPos[0].z;
    divmod_floor(sDesCamX, DES_BLOCK_MM, &sDesCamBlockX, &sDesCamBlockOffX);
    divmod_floor(sDesCamZ, DES_BLOCK_MM, &sDesCamBlockZ, &sDesCamBlockOffZ);
    fractal_update_tiles(sDesCamX, sDesCamZ);
    des_calc_lod_grid();
    des_update_patches();
    sDesBlockCount = 0;
    for (i = 0; i < sPoolSize; i++) {
        if (sDesPatches[i].visible != 0) {
            for (j = 0; j < sDesBlockCount; j++) {
                if (gDesBlockX[j] == sDesPatches[i].blockX && gDesBlockZ[j] == sDesPatches[i].blockZ) {
                    sDesPatches[i].block = j;
                    break;
                }
            }
            if (j == sDesBlockCount) {
                gDesBlockX[j] = sDesPatches[i].blockX;
                gDesBlockZ[j] = sDesPatches[i].blockZ;
                mtx_from_angles_pos_scale((MtxS *)&sDesBlockMtx[gGfxFrameIndex][j], 0, 0, 0,
                                          (sDesPatches[i].blockX - sDesCamBlockX) * DES_BLOCK_MM - sDesCamBlockOffX + 64000,
                                          -sDesCamY,
                                          (sDesPatches[i].blockZ - sDesCamBlockZ) * DES_BLOCK_MM - sDesCamBlockOffZ + 64000,
                                          0x100);
                sDesPatches[i].block = sDesBlockCount;
                sDesBlockCount++;
            }
        }
    }
    sDesObjectCount = fractal_get_visible_objects(sDesCamX, sDesCamZ, gCamYaw[0], 128000, sDesObjects, DES_MAX_OBJECTS);
    for (i = 0; i < sDesObjectCount; i++) {
        mtx_from_angles_pos_scale((MtxS *)&sDesObjMtx[gGfxFrameIndex][i], 0, 0, 0, sDesObjects[i].x - sDesCamX,
                                  sDesObjects[i].y - sDesCamY, sDesObjects[i].z - sDesCamZ, 0x10);
    }
    if (sDesArrowAngle >= 0) {
        /* the arrow turned to the checkpoint, tilted, at the bottom left of the view */
        mtx_from_angles_pos_scale((MtxS *)&rot, 0, 0, sDesArrowAngle, 0, 0, 0, 0x10);
        mtx_from_angles_pos_scale((MtxS *)&place, -0x1000, 0, 0, -70, -55, 300, 10);
        mtx_mul((s16 *)&rot, (s16 *)&place, (s16 *)&sDesArrowMtx[gGfxFrameIndex]);
    }
    mtx_from_angles_pos_scale((MtxS *)&sDesMarkerMtx[gGfxFrameIndex], 0, 0, gCamYaw[0], 0, 0, 1000, 0x100);
}

#define G sDesLodGrid

/* Fills the 32x32 grid of patch cells around the camera with the rings that cover each (bit r: ring r), from the
   distance ahead and the field of view, keeps one ring per place, and marks in sDesSeamFlags where a ring meets a
   coarser one. */
RECOMP_PATCH void des_calc_lod_grid(void) {
    s32 i, j, camX, camZ, idx, parent;
    s32 x, z, side, ahead;
    s32 cosYaw, sinYaw, yaw;
    s32 cell = 4000;
    // The half-width's widening, in 1024ths (1024 the game's own), and its room for each ring widened with it.
    s32 scale = eb_desert_cull_scale();
    s32 room0 = (360 * scale) >> 10;
    s32 room1 = (720 * scale) >> 10;

    camX = sDesCamX >> 4;
    camZ = sDesCamZ >> 4;
    sDesLodCellX = camX / cell;
    sDesLodOffX = camX - sDesLodCellX * cell;
    yaw = (u16)gCamYaw[0];
    yaw >>= 4;
    cosYaw = gCosTable[yaw];
    sinYaw = gCosTable[(yaw - 0x400) & 0xFFF];
    if (sDesLodOffX < 0) {
        sDesLodOffX += cell;
        sDesLodCellX -= 1;
    }
    sDesLodCellZ = camZ / cell;
    sDesLodOffZ = camZ - sDesLodCellZ * cell;
    if (sDesLodOffZ < 0) {
        sDesLodOffZ += cell;
        sDesLodCellZ -= 1;
    }
    idx = 0;
    for (i = 0; i < DES_GRID_SIZE; i++) {
        for (j = 0; j < DES_GRID_SIZE; j++) {
            G[idx] = 0;
            x = ((j - 15) * 8000 - 4000) >> 4;
            z = (i * 8000 - 0x1E460) >> 4;
            x -= sDesLodOffX % 500;
            z -= sDesLodOffZ % 500;
            ahead = (cosYaw * z - sinYaw * x) >> 14;
            side = (cosYaw * x + sinYaw * z) >> 14;
            if (ahead < 5000) G[idx] = 3;
            if (ahead < 4000) G[idx] |= 4;
            if (i < 8 || i >= 24 || j < 8 || j >= 24) G[idx] &= 0xFB;
            if (ahead < 1500) G[idx] &= 0xF1;
            if (ahead >= -359 && side >= -359 && side < 360 && ahead < 360) {
                G[idx] |= 1;
            } else if (ahead < -359) {
                G[idx] = 0;
            } else {
                // Half as far across as ahead, widened with the view; behind the camera (ahead from -359), the room
                // less half as far across as behind, where a cell's front edge reaches into the view's bottom corners.
                ahead = ((ahead / 2) * scale) >> 10;
                if (side < 0) {
                    if (side + room0 < -ahead) G[idx] &= 0xFE;
                    if (side + room1 < -ahead) G[idx] &= 0xF1;
                } else {
                    if (ahead < side - room0) G[idx] &= 0xFE;
                    if (ahead < side - room1) G[idx] &= 0xF1;
                }
            }
            idx++;
        }
    }
    sDesLodOffX /= 500;
    sDesLodOffZ /= 500;
    sDesLod0ShiftX = sDesLodOffX & 1;
    sDesLod0ShiftZ = sDesLodOffZ & 1;
    sDesLod1ShiftX = (sDesLodOffX & 2) >> 1;
    sDesLod1ShiftZ = (sDesLodOffZ & 2) >> 1;
    sDesLod2ShiftX = (sDesLodOffX & 4) / 4;
    sDesLod2ShiftZ = (sDesLodOffZ & 4) / 4;
    for (i = 2; i < 32; i += 2) {
        for (j = 2; j < 32; j += 2) {
            s32 idx1, idx2;
            parent = ((i / 2 + 8) << 5) + 8 + j / 2;
            idx = ((i - sDesLod0ShiftZ) << 5) + (j - sDesLod0ShiftX);
            idx1 = ((i - sDesLod1ShiftZ) << 5) + (j - sDesLod1ShiftX);
            idx2 = ((i - sDesLod2ShiftZ) << 5) + (j - sDesLod2ShiftX);
            if (G[idx] & G[idx + 1] & G[idx + 32] & G[idx + 33] & 1) G[parent] &= 0xFD;
            if (G[idx1] & G[idx1 + 1] & G[idx1 + 32] & G[idx1 + 33] & 2) G[parent] &= 0xFB;
            if (G[idx2] & G[idx2 + 1] & G[idx2 + 32] & G[idx2 + 33] & 4) G[parent] &= 0xF7;
            if (G[parent] & 2) { G[idx] &= 0xFE; G[idx + 1] &= 0xFE; G[idx + 32] &= 0xFE; G[idx + 33] &= 0xFE; }
            if (G[parent] & 4) { G[idx1] &= 0xFD; G[idx1 + 1] &= 0xFD; G[idx1 + 32] &= 0xFD; G[idx1 + 33] &= 0xFD; }
            if (G[parent] & 8) { G[idx2] &= 0xFB; G[idx2 + 1] &= 0xFB; G[idx2 + 32] &= 0xFB; G[idx2 + 33] &= 0xFB; }
        }
    }
    mem_fill(sDesSeamFlags, 0, 0x400);
    for (i = 2; i < 32; i += 2) {
        for (j = 2; j < 32; j += 2) {
            parent = (((i >> 1) + 8) << 5) + 8 + (j >> 1);
            idx = (i - sDesLod0ShiftZ) * 32 + (j - sDesLod0ShiftX);
            if (G[parent + 32] & 2) {
                if (G[idx + 32] & 1) sDesSeamFlags[idx + 32] |= 0x10;
                if (G[idx + 33] & 1) sDesSeamFlags[idx + 33] |= 0x10;
            }
            if (G[parent - 32] & 2) {
                if (G[idx] & 1) sDesSeamFlags[idx] |= 0x20;
                if (G[idx + 1] & 1) sDesSeamFlags[idx + 1] |= 0x20;
            }
            if (G[parent + 1] & 2) {
                if (G[idx + 1] & 1) sDesSeamFlags[idx + 1] |= 0x40;
                if (G[idx + 33] & 1) sDesSeamFlags[idx + 33] |= 0x40;
            }
            if (G[parent - 1] & 2) {
                if (G[idx] & 1) sDesSeamFlags[idx] |= 0x80;
                if (G[idx + 32] & 1) sDesSeamFlags[idx + 32] |= 0x80;
            }
            idx = (i - sDesLod1ShiftZ) * 32 + (j - sDesLod1ShiftX);
            if (G[parent + 32] & 4) {
                if (G[idx + 32] & 2) sDesSeamFlags[idx + 32] |= 1;
                if (G[idx + 33] & 2) sDesSeamFlags[idx + 33] |= 1;
            }
            if (G[parent - 32] & 4) {
                if (G[idx] & 2) sDesSeamFlags[idx] |= 2;
                if (G[idx + 1] & 2) sDesSeamFlags[idx + 1] |= 2;
            }
            if (G[parent + 1] & 4) {
                if (G[idx + 1] & 2) sDesSeamFlags[idx + 1] |= 4;
                if (G[idx + 33] & 2) sDesSeamFlags[idx + 33] |= 4;
            }
            if (G[parent - 1] & 4) {
                if (G[idx] & 2) sDesSeamFlags[idx] |= 8;
                if (G[idx + 32] & 2) sDesSeamFlags[idx + 32] |= 8;
            }
        }
    }
}

#undef G

static inline void divmod(s32 v, s32 d, s32 *q, s32 *r) {
    *q = v / d;
    *r = v - *q * d;
    if (*r < 0) {
        *r += d;
        *q -= 1;
    }
}

/* Assigns a cached patch to each cell of each ring that the LOD grid marks, building the patches that moved or
   are new (until the cache runs out), and sets each patch's seams. */
RECOMP_PATCH void des_update_patches(void) {
    s32 camCellX, rx, camCellZ, rz, ringX, ringZ;
    s32 i, j, k, patch, slot, patchX, patchZ, rowDist, colDist;

    divmod(sDesCamX >> 4, 500, &camCellX, &rx);
    divmod(sDesCamZ >> 4, 500, &camCellZ, &rz);

    for (i = 0; i < DES_NUM_RINGS; i++) {
        j = 1 << i;
        sDesPatchGridOrigin[i] = ((camCellZ / j) * 0x20 + camCellX / j) & 0x3ff;
    }
    sDesCamCellX = camCellX;
    sDesCamCellZ = camCellZ;

    for (i = 0; i < sPoolSize; i++) {
        if (sDesPatches[i].life > 0)
            sDesPatches[i].life--;
        sDesPatches[i].visible = 0;
    }

    for (i = 0; i < DES_NUM_RINGS; i++) {
        for (j = 0; j < DES_GRID_SIZE * DES_GRID_SIZE; j++) {
            patch = sDesPatchGrid[i][j];
            if (patch != -1 && sDesPatches[patch].life <= 0)
                sDesPatchGrid[i][j] = -1;
        }
    }

    sDesFreePatchCount = 0;
    for (i = 0; i < sPoolSize; i++) {
        if (sDesPatches[i].life <= 0) {
            sFreePatches[sDesFreePatchCount++] = i;
        }
    }

    for (i = 0; i < DES_NUM_RINGS; i++) {
        divmod(sDesCamX >> 4, 500 << i, &ringX, &rx);
        divmod(sDesCamZ >> 4, 500 << i, &ringZ, &rz);
        slot = sDesPatchGridOrigin[i];
        patchZ = (ringZ - 16) * (500 << i);
        for (j = 0; j < 0x20; j++) {
            patchX = (ringX - 16) * (500 << i);
            for (k = 0; k < 0x20; k++) {
                if ((sDesLodGrid[j * 32 + k] >> i) & 1) {
                    s32 t = sDesPatchGrid[i][slot];
                    if (t == -1 || sDesPatches[t].x != patchX || sDesPatches[t].z != patchZ) {
                        if (sDesFreePatchCount == 0)
                            goto out;
                        patch = sFreePatches[--sDesFreePatchCount];
                        sDesPatchGrid[i][slot] = patch;
                        des_build_patch(patch, patchX, patchZ, i);
                    } else {
                        patch = t;
                        sDesPatches[patch].life = 2;
                    }
                    sDesPatches[patch].visible = 1;
                    rowDist = j - 16;
                    if (rowDist < 0)
                        rowDist = -rowDist;
                    colDist = k - 16;
                    if (colDist < 0)
                        colDist = -colDist;
                    sDesPatches[patch].near = (colDist + rowDist < 7) ? 1 : 0;
                } else {
                    sDesPatchGrid[i][slot] = -1;
                }
                slot = (slot + 1) & 0x3ff;
                patchX += 500 << i;
            }
            patchZ += 500 << i;
        }
    }
out:
    for (i = 0; i < sPoolSize; i++)
        sDesPatches[i].seams = 0;

    for (i = 0; i < 2; i++) {
        slot = sDesPatchGridOrigin[i];
        for (j = 0; j < DES_GRID_SIZE * DES_GRID_SIZE; j++) {
            patch = sDesPatchGrid[i][slot];
            if (patch != -1) {
                u8 x = sDesSeamFlags[j];
                sDesPatches[patch].seams = (i == 0) ? (x >> 4) : (x & 0xf);
            }
            slot = (slot + 1) % 0x400;
        }
    }
}
