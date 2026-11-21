// The race's riders, and the marker over the rider one place ahead, out to the window's edges: past the 4:3 frame's
// sides in a wide window, where the track is drawn (src/game/widescreen.cpp). Each is the decomp's (src/game/render.c),
// copied here as it is but for the changes below.
//
// render_get_rider_lod chooses each rider's level of detail in each view before the riders are drawn, and hides one
// whose place on the screen, with a margin of 90 units about it, is off the view's rectangle: the bike, the rider, its
// shadow and its marker (render_draw_place_markers draws one only over a rider that isn't hidden). So a rider riding
// into the window's side vanished at the old frame's edge, half of it already past it, and came back as the camera
// turned it in again. render_draw_place_markers then skips a marker more than 50 pixels off the screen, and draws it
// with gSPTextureRectangle, whose x is 10.2 fixed point in 12 bits, 0 to 1023.75, so none can start left of the
// screen, under the view's scissor, the screen's width, which RT64 widens to the window for triangles but not for
// rectangles: a marker across the old frame's side was cut off straight down it.
//
// So for a view RT64 widens to the window (eb::view_widened), both bounds reach as far past each side as the window
// shows, eb_view_margin's pixels of the screen (src/game/widescreen.cpp), and the markers are drawn with RT64's
// gEXTextureRectangle, whose coordinates are signed 16 bits, at the same place and size, under an extended scissor as
// far out, the view's own rows high, the game's put back after them, as patches/particles.c draws the particles. With
// no margin (Original, a window no wider than 4:3, split screens side by side) both are the game's own.

#include "patches.h"

// The decomp's types and constants, without its prototypes and globals (as patches/particles.c).
#include "common.h"
#include "constants.h"
#include "structs.h"
#include "rt64_extended_gbi.h"

s32 project_to_screen(s32 x, s32 y, s32 z, s32 *sx, s32 *sy, s32 *sz, s32 *w, s32 mtx, s32 view);
Gfx *render_set_viewport(Gfx *gdl, s32 projMtx, s32 view, s32 loadProj);

// The game's data by address (the decomp's versions/us_rev1/symbols.txt).
#define gInsetViewActive (*(s32 *)0x800C2CC4)
#define gNumViews (*(s32 *)0x800C2ED4)
#define gLoDetailThresh ((s32 *)0x800C9E90)
#define gTrackSections (*(TrackModelList **)0x800C9F84)
#define gCamTargets ((s32 *)0x800C9F94)
#define gTracks 0x800CBB50 /* 0xA0 bytes each */
#define sMarkerImage (*(MarkerImage **)0x800DCA60)
#define gRiderGfx ((u8 *)0x800DCA70)
#define sPlaceTextures (*(u8 **)0x800E1E78)
#define gViewRects ((ViewRect (*)[4])0x800E2038)
#define gRiderViewLod ((u32 *)0x800E21C0)
#define gNumScreens (*(s32 *)0x800E21B8)
#define sRiderCamDist ((s32 *)0x800E2378)
#define sProjScaleX ((f32 *)0x800E23C0)
#define sProjScaleY ((f32 *)0x800E23D0)
#define gRiders ((Rider *)0x800E4160)
#define gCamPos ((Vec3i *)0x800E8A30)
#define gCurTrack (*(Cfg **)0x800F0704)
#define gScreenWidth (*(volatile s32 *)0x800F1D8C)
#define gScreenHeight (*(volatile s32 *)0x800F1D90)

// gRiderGfx's entries, 0xA80 bytes each, the place marker's palette at 0xA60 (render.c's PalEnt).
#define RIDER_GFX_SIZE 0xA80
#define RIDER_GFX_MARKER_PAL 0xA60

// gViewRects' views: each view's rectangle in pixels, by the views' count, the full views while the race's 3D draws.
typedef struct ViewRect {
    s32 x, y, w, h;
} ViewRect;

// The marker's image (render.c's TexInfo_2): its width in 8 texels, its height, its texels.
typedef struct MarkerImage {
    u8 pad[8];
    u8 w;
    u8 h;
    u8 pad2[2];
    void *tex;
} MarkerImage;

// How far past each side of the screen view `view` shows in the window, in the screen's pixels (widescreen.cpp).
s32 eb_view_margin(s32 view);

static inline s32 project(s32 x, s32 y, s32 z, s32 *sx, s32 *sy, s32 *sz, s32 *w, s32 dyn, s32 view) {
    if (gCurTrack->f2C == TRACK_ENV_STADIUM) {
        return project_to_screen(x >> 4, y >> 4, z >> 4, sx, sy, sz, w, dyn, view);
    }
    return project_to_screen((x - gCamPos[view].x) >> 4, (y - gCamPos[view].y) >> 4, (z - gCamPos[view].z) >> 4, sx,
                             sy, sz, w, dyn, view);
}

// The level of detail (RIDER_LOD_*) to draw a rider at (x, y, z) (mm) in a view: hidden when its section is not
// visible or it is off the view's rectangle, which reaches eb_view_margin's pixels past each side, else by its distance.
RECOMP_PATCH s32 render_get_rider_lod(s32 x, s32 y, s32 z, s32 view, s32 section, s32 dyn, s32 rider) {
    s32 sx, sy, sz, depth;
    s32 halfW, halfH, lodDist;
    s32 x0, x1, y0, y1;
    s32 margin;
    ViewRect *rect;

    if (gInsetViewActive != 0 && rider == 0) {
        return RIDER_LOD_HIGH;
    }
    if (!(gRiders[rider].camMode == CAM_MODE_FALL_VIEW && gCamTargets[view] == rider)) {
        if ((u32)gCurTrack->f2C < TRACK_ENV_DESERT && section != -1) {
            if (!((gTrackSections->e[section].mask >> (view * 2)) & 1)) {
                return RIDER_LOD_HIDDEN;
            }
        }
    }
    if (project(x, y, z, &sx, &sy, &sz, &depth, dyn, view) != 0) {
        return RIDER_LOD_HIDDEN;
    }
    if (depth <= 0) {
        return RIDER_LOD_HIDDEN;
    }
    if (gCurTrack->f2C == TRACK_ENV_DESERT && depth > 8000) {
        return RIDER_LOD_HIDDEN;
    }
    sRiderCamDist[rider] = depth;
    rect = &gViewRects[gNumViews - 1][view];
    margin = eb_view_margin(view);
    x0 = rect->x - margin;
    x1 = rect->w + rect->x + margin;
    y0 = rect->y;
    y1 = rect->h + y0;
    halfW = (sProjScaleX[view] * 90.0f) / depth;
    halfH = (sProjScaleY[view] * 90.0f) / depth;
    /* off the view's rectangle, with a margin of 90 units around the rider */
    if (sx + halfW < x0) {
        return RIDER_LOD_HIDDEN;
    }
    if (x1 < sx - halfW) {
        return RIDER_LOD_HIDDEN;
    }
    if (sy + halfH < y0) {
        return RIDER_LOD_HIDDEN;
    }
    if (y1 < sy - halfH) {
        return RIDER_LOD_HIDDEN;
    }
    if (((u32)gCurTrack - gTracks) / 0xA0 == TRACK_EXCITE_3D && gRiders[0].camMode == CAM_MODE_SIDE_VIEW) {
        return RIDER_LOD_HIGH;
    }
    lodDist = gLoDetailThresh[gNumScreens - 1];
    if (lodDist * 6 / 10 < depth) {
        return RIDER_LOD_LOW;
    }
    if (lodDist * 4 / 10 < depth) {
        return RIDER_LOD_MID;
    }
    return RIDER_LOD_HIGH;
}

// A marker's rectangle: the game's, or with a margin the same one with signed coordinates.
static Gfx *marker_rect(Gfx *gdl, s32 margin, s32 ulx, s32 uly, s32 lrx, s32 lry, s32 dsdx, s32 dtdy) {
    if (margin != 0) {
        gEXTextureRectangle(gdl++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, ulx, uly, lrx, lry, G_TX_RENDERTILE, 0, 0, dsdx,
                            dtdy);
    } else {
        gSPTextureRectangle(gdl++, ulx, uly, lrx, lry, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
    }
    return gdl;
}

// Over the rider one place ahead of each player: a marker in the rider's colour with its place number, as far past
// the screen's sides as the window shows.
RECOMP_PATCH Gfx *render_draw_place_markers(Gfx *gdl, s32 dyn) {
    s32 sx, sy, sz, sw;
    s32 markW, markH, markS, markT, digitW, digitH, digitS, digitT;
    s32 texW, texW8, texH, i, j, y, ofs;
    s32 gfxOfs;
    s32 margin;
    ViewRect *rect;
    Rider *rider;

    digitW = gScreenWidth / 16;
    digitH = gScreenHeight / 15;
    digitS = 0x8000 / digitW;
    digitT = 0x4000 / digitH;
    texW8 = sMarkerImage->w;
    markW = gScreenWidth * 24 / 320;
    markS = (texW8 << 13) / markW;
    texH = sMarkerImage->h;
    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    {
        Gfx *g = gdl++;
        g->words.w0 = 0xE200001C;
        g->words.w1 = 0x404A50;
    }
    gDPSetCombineMode(gdl++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTextureFilter(gdl++, G_TF_POINT);
    gDPSetDepthSource(gdl++, G_ZS_PRIM);
    texW = texW8 * 8;
    markH = gScreenHeight / 10;
    markT = (texH << 10) / markH;
    for (i = 0; i < gNumScreens; i++) {
        gdl = render_set_viewport(gdl, dyn, i, 0);
        margin = eb_view_margin(i);
        if (margin != 0) {
            rect = &gViewRects[gNumViews - 1][i];
            gEXEnable(gdl++);
            gEXPushScissor(gdl++);
            gEXSetScissor(gdl++, G_SC_NON_INTERLACE, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, -margin, rect->y,
                          gScreenWidth + margin, rect->y + rect->h);
        }
        for (j = 0; j < 8; j++) {
            gfxOfs = j * RIDER_GFX_SIZE;
            rider = &gRiders[j];
            if (rider->inactive != 0) continue;
            if (j >= 6) continue;
            if (!((gRiderViewLod[j] >> (i * 8)) & 0xF)) continue;
            if (rider->crashState != 0) continue;
            if (rider->race.place != gRiders[i].race.place - 1) continue;
            if (project(rider->drawPos.x, rider->drawPos.y + 2000, rider->drawPos.z, &sx, &sy, &sz, &sw, dyn, i) != 0) {
                continue;
            }
            if (sx < -50 - margin) continue;
            if (sy < -50) continue;
            if (sx > gScreenWidth + 50 + margin) continue;
            if (sy > gScreenHeight + 50) continue;
            ofs = -12;
            y = sy + gScreenHeight * ofs / 240;
            gDPSetPrimDepth(gdl++, sz, 0);
            gDPPipeSync(gdl++);
            gDPSetTextureLUT(gdl++, G_TT_RGBA16);
            gDPSetTextureImage(gdl++, G_IM_FMT_CI, G_IM_SIZ_16b, 1, sMarkerImage->tex);
            gDPSetTile(gdl++, G_IM_FMT_CI, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 7, 0, 0, 7, 0);
            gDPLoadSync(gdl++);
            gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, (s32)((u32)(texW * texH) >> 2) - 1, CALC_DXT_4b(texW));
            gDPPipeSync(gdl++);
            {
                Gfx *g = gdl++;
                g->words.w0 = ((u32)((texW >> 1) + 7) >> 3 << 9) | 0xF5400000;
                g->words.w1 = 0x1C070;
            }
            gDPSetTileSize(gdl++, G_TX_RENDERTILE, 0, 0, (texW - 1) << G_TEXTURE_IMAGE_FRAC,
                           (texH - 1) << G_TEXTURE_IMAGE_FRAC);
            gDPLoadTLUT_pal256(gdl++, gRiderGfx + RIDER_GFX_MARKER_PAL + gfxOfs);
            gdl = marker_rect(gdl, margin, (sx - markW / 2) * 4, (y - markH / 2) * 4, (sx + markW / 2) * 4,
                              (y + markH / 2) * 4, markS, markT);
            gDPPipeSync(gdl++);
            ofs = -7;
            y = sy + gScreenHeight * ofs / 240;
            gDPSetTextureLUT(gdl++, G_TT_NONE);
            ofs = rider->race.place;
            gDPLoadTextureBlock(gdl++, sPlaceTextures + (ofs << 9), G_IM_FMT_CI, G_IM_SIZ_8b, 32, 16, 0, G_TX_CLAMP,
                                G_TX_CLAMP, 5, 4, 0, 0);
            gdl = marker_rect(gdl, margin, (sx - digitW / 2) * 4, (y - digitH / 2) * 4, (sx + digitW / 2) * 4,
                              (y + digitH / 2) * 4, digitS, digitT);
        }
        if (margin != 0) {
            gEXPopScissor(gdl++);
        }
    }
    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetDepthSource(gdl++, G_ZS_PIXEL);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    return gdl;
}
