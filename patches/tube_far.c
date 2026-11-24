// The General tab's Draw Distance on the outdoor tracks (src/game/draw_distance.cpp has the stadium's). An outdoor track
// is drawn in sections, the height areas of its ground, and its objects (fences, banners, trees, cliff walls) go with
// the section under them. tube_cull_sections (the decomp's src/game/tubegfx.c) draws only the sections the track's
// PVS lists for the section under the camera, its first ones as far (tube_draw_far, under the fog, before the near
// ones) and the rest as near, less those behind the camera and, past 300 units (4.8 m) ahead, those beside the view.
// So a section, with all its objects, appears when the camera crosses into the section whose list first names it, and
// that can be well inside the fog: a slab of ground or a row of fences popping in ahead, or at a wide window's sides,
// which the 4:3 lists never thought of.
//
// With Far, tube_update_view (copied here as it is but for the call) adds after the game's cull every other section
// ahead of the camera, before the far clip (gFarClip, in the same units) and within the view by the game's own test
// (sCullSlope, which src/game/widescreen.cpp's hook in tube_cull_sections has widened with the view), as far: drawn
// under the fog with tube_draw_far's viewport, which shares the near one's depth range, so they sort with the rest.
// The objects and water on them follow their section as the game's do.
//
// The models' matrices come from a pool of TUBE_MTX_POOL (300) a frame, which every view shares, and getmtx draws
// nothing once it runs out, the near pass last. So the sections added are the nearest first, and only while the view's
// share of the pool (less 8 for its splashes) holds the game's own sections, objects and water and theirs. The Excite
// 3D side view, which draws the sections near the player's instead, and a camera off the ground (CAM_MODE_FALL_VIEW,
// where the game draws at most 16 sections) are left as the game has them. Original leaves it all as a console does.

#include "patches.h"
#include "common.h"
#include "constants.h"
#include "structs.h"

void tube_calc_fog_range(s32 view, s32 *fogNear, s32 *fogFar);
void tube_cull_sections(TrackModelList *sections, s32 view);

// Whether the General tab's Draw Distance is Far (src/game/draw_distance.cpp).
s32 eb_draw_far(void);

#define gFarClip (*(s32 *)0x800C2D30)
#define gNumViews (*(s32 *)0x800C2ED4)
#define gCosTable ((s16 *)0x800C35A0)
#define gTrackObjects (*(TrackModelList **)0x800C9F80)
#define gTrackSections (*(TrackModelList **)0x800C9F84)
#define gWaterModels (*(TrackModelList **)0x800C9F88)
#define sCullSlope (*(s32 *)0x800C9F8C)
#define gTracks 0x800CBB50 /* 0xA0 bytes each */
#define gGfxFrameIndex (*(s32 *)0x800E3A50)
#define gRiders ((Rider *)0x800E4160)
#define sTubeFogNear ((s32 *)0x800E8110)
#define sTubeFogFar ((s32 *)0x800E8120)
#define sTubeMtxCount (*(s32 *)0x800E8170)
#define sTubeMtxFrame (*(s32 *)0x800E81C0)
#define sSectionBounds (*(TrackBounds **)0x800E81B4)
#define gObjectFlags (*(u32 **)0x800E81D4)
#define gCamYaw ((s32 *)0x800E89A0)
#define gCamPos ((Vec3i *)0x800E8A30)
#define gGroundFound (*(s32 *)0x800E8EA8)
#define gCurTrack (*(u32 *)0x800F0704)

// The sections this file can count; a track with more is left as the game has it.
#define MAX_SECTIONS 1024
// Each view's matrices for the riders' splashes (tube_draw_water draws at most 8).
#define SPLASH_MTXS 8

// Per section: the matrices it costs (itself, its objects and its water), and, for those this adds, how far ahead its
// nearest corner is.
static u16 sCost[MAX_SECTIONS];
static u16 sAdded[MAX_SECTIONS];
static s32 sAddedDepth[MAX_SECTIONS];

// Counts what each section costs and what the sections the game drew for the view cost all told.
static s32 count_costs(TrackModelList *sections, s32 view) {
    u32 i;
    s32 used = 0;
    s32 visBit = TRACK_MODEL_VISIBLE(view);

    for (i = 0; i < sections->count; i++) {
        sCost[i] = 1;
    }
    for (i = 0; i < gTrackObjects->count; i++) {
        u32 section = gTrackObjects->e[i].mask >> 16;
        if (!(gObjectFlags[i] & OBJ_HIDDEN) && section < sections->count) {
            sCost[section]++;
        }
    }
    if (gWaterModels != NULL) {
        for (i = 0; i < gWaterModels->count; i++) {
            u32 section = gWaterModels->e[i].mask >> 16;
            if (section < sections->count) {
                sCost[section]++;
            }
        }
    }
    for (i = 0; i < sections->count; i++) {
        if (sections->e[i].mask & visBit) {
            used += sCost[i];
        }
    }
    return used;
}

// Adds as far the sections the game left out that the view sees, nearest first, while the view's share of the pool
// holds them (above).
static void add_far_sections(TrackModelList *sections, s32 view) {
    s32 visBit = TRACK_MODEL_VISIBLE(view);
    s32 camX, camZ, yaw, cosYaw, sinYaw;
    s32 budget, used, numAdded;
    u32 i;
    s32 j, k;

    if (sections->count > MAX_SECTIONS || gNumViews <= 0) {
        return;
    }
    if ((gCurTrack - gTracks) / 0xA0 == TRACK_EXCITE_3D && gRiders[0].camMode == CAM_MODE_SIDE_VIEW) {
        return;
    }
    if (gRiders[view].camMode == CAM_MODE_FALL_VIEW || gGroundFound == 0) {
        return;
    }
    budget = TUBE_MTX_POOL / gNumViews - SPLASH_MTXS;
    used = count_costs(sections, view);
    if (used >= budget) {
        return;
    }

    camX = gCamPos[view].x >> 4;
    camZ = gCamPos[view].z >> 4;
    yaw = gCamYaw[view] >> 4;
    cosYaw = gCosTable[yaw & 0xFFF];
    sinYaw = gCosTable[(yaw - 0x400) & 0xFFF];
    numAdded = 0;

    for (i = 0; i < sections->count; i++) {
        TrackModel *sec = &sections->e[i];
        TrackBounds *bounds = &sSectionBounds[i];
        s32 t, u, f, h, x, z, nearest;
        s32 d[4];

        if (sec->mask & visBit) {
            continue;
        }
        // The corners' depths ahead of the camera, as tube_cull_sections works them out.
        x = sec->x0;
        z = sec->z0;
        t = (cosYaw * (z - camZ) - sinYaw * (x - camX)) >> 14;
        x = sec->x1 - x;
        h = (-sinYaw * x) >> 14;
        f = (cosYaw * (sec->z1 - z)) >> 14;
        u = t + h;
        d[0] = t;
        d[1] = u;
        d[2] = t + f;
        d[3] = u + f;
        // Behind the camera.
        if (d[0] < 11 && d[1] < 11 && d[2] < 11 && d[3] < 11) {
            continue;
        }
        nearest = d[0];
        for (k = 1; k < 4; k++) {
            if (d[k] < nearest) {
                nearest = d[k];
            }
        }
        // Past the far clip.
        if (nearest >= gFarClip) {
            continue;
        }
        // Beside the view, by the game's own test.
        if (d[0] > 300 && d[1] > 300 && d[2] > 300 && d[3] > 300) {
            s32 y = bounds->x0;
            s32 u2;
            s32 slope[4];
            t = (cosYaw * (y - camX) + sinYaw * (bounds->z0 - camZ)) >> 14;
            y = bounds->x1 - y;
            h = (cosYaw * y) >> 14;
            u2 = t + h;
            f = (sinYaw * (bounds->z1 - bounds->z0)) >> 14;
            slope[0] = (t << 8) / d[0];
            slope[1] = (u2 << 8) / d[1];
            slope[2] = ((t + f) << 8) / d[2];
            slope[3] = ((u2 + f) << 8) / d[3];
            if ((slope[0] < -sCullSlope && slope[1] < -sCullSlope && slope[2] < -sCullSlope && slope[3] < -sCullSlope) ||
                (sCullSlope < slope[0] && sCullSlope < slope[1] && sCullSlope < slope[2] && sCullSlope < slope[3])) {
                continue;
            }
        }
        // Kept in order of depth.
        for (j = numAdded; j > 0 && sAddedDepth[j - 1] > nearest; j--) {
            sAdded[j] = sAdded[j - 1];
            sAddedDepth[j] = sAddedDepth[j - 1];
        }
        sAdded[j] = i;
        sAddedDepth[j] = nearest;
        numAdded++;
    }

    for (j = 0; j < numAdded; j++) {
        TrackModel *sec = &sections->e[sAdded[j]];
        if (used + sCost[sAdded[j]] > budget) {
            continue;
        }
        used += sCost[sAdded[j]];
        sec->mask |= TRACK_MODEL_VISIBLE(view) | TRACK_MODEL_FAR(view);
    }
}

/* Culls the sections for a view, then gives each object and water model the view bits of its section (whose index
   is in the high half of its mask). */
RECOMP_PATCH void tube_update_view(s32 view) {
    u32 i;
    TrackModel *model;

    tube_calc_fog_range(view, &sTubeFogNear[view], &sTubeFogFar[view]);
    tube_cull_sections(gTrackSections, view);
    if (eb_draw_far()) {
        add_far_sections(gTrackSections, view);
    }
    for (i = 0; i < gTrackObjects->count; i++) {
        model = &gTrackObjects->e[i];
        model->mask = (model->mask & 0xFFFF0000) | gTrackSections->e[model->mask >> 16].mask;
    }
    if (gWaterModels != NULL) {
        for (i = 0; i < gWaterModels->count; i++) {
            model = &gWaterModels->e[i];
            model->mask = (model->mask & 0xFFFF0000) | gTrackSections->e[model->mask >> 16].mask;
        }
    }
    if (gGfxFrameIndex != sTubeMtxFrame) {
        sTubeMtxCount = 0;
    }
    sTubeMtxFrame = gGfxFrameIndex;
}
