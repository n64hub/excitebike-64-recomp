// The track's lights glow out to the window's edges: a stadium's light banks past the 4:3 frame's sides in a wide
// window, where the track is drawn (src/game/widescreen.cpp), glowed only inside it. sun_update_screen_pos (the
// decomp's src/game/sun.c, copied here as it is but for the changes below) marks a light on the screen only within 50
// pixels of its sides, so a bank out in the window's side was drawn as its bare grid of bulbs, with no glow. And
// sun_draw_flare draws each glow under the view's scissor, which stopped one straddling the old frame's side straight
// down it, and an outdoor track's glow as a rectangle clamped to x 0, whose coordinates can't start left of the screen.
//
// So for the view RT64 widens to the window (one player's: the lights are drawn with one screen only), the bound reaches
// as far past each side as the window shows, eb_particle_margin's pixels of the screen (src/game/widescreen.cpp); the
// glows are drawn under an extended scissor as far out, the view's own rows high, the game's put back after them; and a
// rectangle glow is RT64's gEXTextureRectangle, whose coordinates are signed 16 bits, at the same place and size. With
// no margin (Original, a window no wider than 4:3) all three are the game's own.
//
// How much of a light shows is read from the Z buffer round it after the frame (sun_check_occlusion, left as it is),
// which RT64 copies back at the 4:3 frame's size: for a light out in the window's side it samples the frame's edge
// column at the light's rows, as the game already did for one within its 50 pixels, so a hill at the edge hides the
// lights behind it there too.

#include "patches.h"

#include "common.h"
#include "constants.h"
#include "structs.h"
#include "rt64_extended_gbi.h"

s32 project_to_screen(s32 x, s32 y, s32 z, s32 *sx, s32 *sy, s32 *sz, s32 *w, s32 mtx, s32 view);
s32 sky_get_sun_screen_pos(s32 *outx, s32 *outy, s32 mtx, s32 view);
s32 isqrt_u32(u32 r); // the decomp's isqrt_u32_2: syms/ names it isqrt_u32

// The game's data by address (the decomp's versions/us_rev1/symbols.txt).
#define gCheatFlags (*(s32 *)0x800C2BC0)
#define gScreenViewport (*(Vp *)0x800C2F28)
#define gCosTable ((s16 *)0x800C35A0)
#define sFlareColors ((u8 *)0x800CA184)
#define sFlareSizes ((s32 *)0x800CA190)
#define sSunList (*(FlareList **)0x800CA1A0)
#define gViewRects ((ViewRect (*)[4])0x800E2038)
#define gCullMode (*(u32 *)0x800E22EC)
#define sFlareTextures (*(TexSet **)0x800E9820)
#define sSunVtxBufs ((Vtx **)0x800E9828)
#define sSunVtxUsed ((s32 *)0x800E9830)
#define gSunGlare (*(s32 *)0x800E983C)
#define gCurTrack_4 (*(Cfg **)0x800F0704)
#define gScreenWidth (*(volatile s32 *)0x800F1D8C)
#define gScreenHeight (*(volatile s32 *)0x800F1D90)

// gViewRects' views: each view's rectangle in pixels, by the views' count.
typedef struct ViewRect {
    s32 x, y, w, h;
} ViewRect;

// How far past each side of the screen view `view` shows in the window, in the screen's pixels (widescreen.cpp).
s32 eb_particle_margin(s32 view);

RECOMP_PATCH void sun_update_screen_pos(s32 dyn, s32 view, s32 fb) {
    s32 sx, sy, sz, depth, depth2;
    s32 i;
    Flare *flare;
    u32 flags;
    s32 margin;

    if (sSunList != NULL) {
        margin = eb_particle_margin(view);
        for (i = 0; i < sSunList->count; i++) {
            flare = &sSunList->e[i];
            flags = flare->flags;
            flare->flags = flags & ~FLARE_ON_SCREEN;
            if (flags & FLARE_SUN) {
                if (sky_get_sun_screen_pos(&sx, &sy, dyn, view) == 0) {
                    flare->sz = 0x7FFF;
                    flare->flags |= FLARE_ON_SCREEN;
                    flare->sx = sx;
                    flare->sy = sy;
                }
            } else if (flags & FLARE_FIXED) {
                if (project_to_screen(0, 20, -300, &sx, &sy, &sz, &depth, dyn, 0) == 0) {
                    flare->sz = 0x7FFF;
                    flare->flags |= FLARE_ON_SCREEN;
                    flare->sx = sx;
                    flare->sy = sy;
                }
            } else {
                if (project_to_screen(flare->x, flare->y, flare->z, &sx, &sy, &sz, &depth2, dyn, view) == 0) {
                    flare->sx = sx;
                    flare->sy = sy;
                    flare->sz = sz;
                    flare->flags |= FLARE_ON_SCREEN;
                }
            }
            if (flare->sx < -50 - margin || flare->sy < -50 ||
                gScreenWidth + 50 + margin < flare->sx || gScreenHeight + 50 < flare->sy) {
                flare->flags &= ~FLARE_ON_SCREEN;
            }
        }
    }
}

static inline Vtx *allocVtx(s32 fb, s32 count) {
    s32 used = sSunVtxUsed[fb];
    if (used + count > SUN_NUM_VTX) {
        return NULL;
    }
    sSunVtxUsed[fb] = used + count;
    return &sSunVtxBufs[fb][used];
}

RECOMP_PATCH Gfx *sun_draw_flare(Gfx *gdl, u8 *dyn, s32 fb) {
    s32 i, j, k;
    Flare *flare;
    Vtx *ringVtx;
    TexEntry *tex;
    s32 alpha, b, stepX, stepY, x, y, angle, dx, dy, m, n, w;
    s32 texS;
    s32 texT;
    Vtx *quadVtx;
    s32 margin;
    ViewRect *rect;

    if (sSunList == NULL) {
        return gdl;
    }
    sSunVtxUsed[fb] = 0;
    if (gCheatFlags & CHEAT_NIGHT) {
        return gdl;
    }

    margin = eb_particle_margin(0);
    rect = &gViewRects[0][0];
    gDPPipeSync(gdl++);
    gDPSetTextureLUT(gdl++, G_TT_NONE);
    gSPClipRatio(gdl++, FRUSTRATIO_1);
    gSPClearGeometryMode(gdl++, G_ZBUFFER);
    gSPViewport(gdl++, &gScreenViewport);
    gSPMatrix(gdl++, dyn + 0x300, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    if (margin != 0) {
        gEXEnable(gdl++);
        gEXPushScissor(gdl++);
        gEXSetScissor(gdl++, G_SC_NON_INTERLACE, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, -margin, rect->y,
                      gScreenWidth + margin, rect->y + rect->h);
    } else {
        gDPSetScissor(gdl++, G_SC_NON_INTERLACE, rect->x, rect->y, rect->x + rect->w - 1, rect->y + rect->h - 1);
    }
    gDPPipeSync(gdl++);
    gDPSetRenderMode(gdl++, 0x404240, 0);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gSPTexture(gdl++, 0, 0, 0, 0, G_OFF);

    gSunGlare = 0;
    for (i = 0; i < sSunList->count; i++) {
        flare = &sSunList->e[i];
        if (flare->flags & FLARE_ON_SCREEN) { if (flare->flags & FLARE_SAMPLED) { if (flare->visible >= 0) {
            stepX = gScreenWidth / 2 - flare->sx;
            stepY = gScreenHeight / 2 - flare->sy;
            alpha = 0x80 - isqrt_u32(stepX * stepX + stepY * stepY);
            if (alpha >= 0) {
                alpha = alpha * flare->visible / 6;
                if (alpha != 0) {
                    alpha = alpha * 192 / 128;
                    if (i == 0) {
                        gSunGlare = alpha;
                    }
                    ringVtx = allocVtx(fb, 28);
                    if (ringVtx != NULL) {
                        x = flare->sx - gScreenWidth / 2;
                        y = flare->sy - gScreenHeight / 2;
                        stepX = x / 3;
                        stepY = y / 3;
                        x -= stepX;
                        y -= stepY;
                        for (j = 0; j < 4; j++) {
                            ringVtx[j * 7].v.ob[0] = x;
                            ringVtx[j * 7].v.ob[1] = -y;
                            ringVtx[j * 7].v.ob[2] = 50;
                            for (k = 0; k < 6; k++) {
                                b = -y;
                                angle = ((k * 2 + 1) << 15) / 6;
                                ringVtx[j * 7 + k + 1].v.ob[0] = x + SIN16(angle) * sFlareSizes[j] / 16384;
                                ringVtx[j * 7 + k + 1].v.ob[1] = b + COS16(angle) * sFlareSizes[j] / 16384;
                                ringVtx[j * 7 + k + 1].v.ob[2] = 50;
                            }
                            x -= stepX;
                            y -= stepY;
                            if (j & 1) {
                                x -= stepX;
                                y -= stepY;
                            }
                        }
                        gSPVertex(gdl++, ringVtx, 28, 0);
                        for (j = 0; j < 4; j++) {
                            gDPPipeSync(gdl++);
                            gDPSetPrimColor(gdl++, 0, 0, sFlareColors[j * 3], sFlareColors[j * 3 + 1],
                                            sFlareColors[j * 3 + 2], alpha);

                            for (k = 0; k < 3; k++) {
                                w = k * 2 + 2;
                                m = k * 2 + 1;
                                n = w % 6 + 1;
                                gSP2Triangles(gdl++, j * 7, j * 7 + w, j * 7 + m, 0, j * 7, j * 7 + n, j * 7 + w, 0);
                            }
                        }
                    }
                }
            }
        }
    }}}

    gDPPipeSync(gdl++);
    gDPSetCombine(gdl++, 0x11FE23, 0xFFFFF3F9);
    gDPSetColorDither(gdl++, G_CD_NOISE);
    gDPSetAlphaDither(gdl++, G_AD_NOISE);
    gDPSetRenderMode(gdl++, 0x404240, 0);
    gSPClearGeometryMode(gdl++, G_CULL_BOTH);
    gSPTexture(gdl++, 0x8000, 0x8000, 0, 0, G_ON);
    if (gCurTrack_4->f2C == TRACK_ENV_STADIUM) {
        gDPSetTexturePersp(gdl++, G_TP_PERSP);
    } else {
        gDPSetTexturePersp(gdl++, G_TP_NONE);
    }

    for (i = 0; i < sSunList->count; i++) {
        flare = &sSunList->e[i];
        if (flare->flags & FLARE_ON_SCREEN) { if (flare->flags & FLARE_SAMPLED) { if ((n = flare->visible) >= 0) {
            gDPPipeSync(gdl++);
            gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF);
            tex = &sFlareTextures->tex[n];
            if (gCurTrack_4->f2C == TRACK_ENV_STADIUM) {
                x = flare->sx - gScreenWidth / 2;
                y = gScreenHeight / 2 - flare->sy;
                stepY = tex->height - 1;
                dx = (SIN16((x + y) * 32) * stepY >> 14) * 2 / 3;
                dy = (COS16((x + y) * 32) * stepY >> 14) * 2 / 3;
                quadVtx = allocVtx(fb, 4);
                if (quadVtx != NULL) {
                    quadVtx[0].v.ob[0] = x + dx;
                    quadVtx[0].v.ob[1] = y + dy;
                    quadVtx[0].v.tc[0] = 0;
                    quadVtx[0].v.tc[1] = 0;
                    quadVtx[1].v.ob[0] = x + dy;
                    quadVtx[1].v.ob[1] = y - dx;
                    quadVtx[1].v.tc[0] = stepY << 6;
                    quadVtx[1].v.tc[1] = 0;
                    quadVtx[2].v.ob[0] = x - dx;
                    quadVtx[2].v.ob[1] = y - dy;
                    quadVtx[2].v.tc[0] = stepY << 6;
                    quadVtx[2].v.tc[1] = stepY << 6;
                    quadVtx[3].v.ob[0] = x - dy;
                    quadVtx[3].v.ob[1] = y + dx;
                    quadVtx[3].v.tc[0] = 0;
                    quadVtx[3].v.tc[1] = stepY << 6;
                    gSPVertex(gdl++, quadVtx, 4, 0);
                    gDPLoadTextureBlock(gdl++, tex->data, G_IM_FMT_IA, G_IM_SIZ_8b, tex->width * 8, tex->height, 0, G_TX_CLAMP,
                                        G_TX_CLAMP, 6, 6, G_TX_NOLOD, G_TX_NOLOD);
                    gSP2Triangles(gdl++, 0, 1, 2, 0, 0, 2, 3, 0);
                }
            } else {
                // A rectangle, clipped to the screen; with a margin, to as far past its sides as the window shows.
                texS = texT = 0;
                x = flare->sx - tex->width * 4;
                y = flare->sy - tex->height / 2;
                stepX = x + tex->width * 8;
                stepY = y + tex->height;
                if (x < -margin) {
                    texS = -margin - x;
                    x = -margin;
                }
                if (y < 0) {
                    texT = -y;
                    y = 0;
                }
                if (stepX >= gScreenWidth + margin) {
                    stepX = gScreenWidth + margin - 1;
                }
                if (stepY >= gScreenHeight) {
                    stepY = gScreenHeight - 1;
                }
                if (x < stepX && y < stepY) {
                    gDPLoadTextureBlock(gdl++, tex->data, G_IM_FMT_IA, G_IM_SIZ_8b, tex->width * 8, tex->height, 0, G_TX_CLAMP,
                                        G_TX_CLAMP, 6, 6, G_TX_NOLOD, G_TX_NOLOD);
                    if (margin != 0) {
                        gEXTextureRectangle(gdl++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, x << 2, y << 2, stepX << 2,
                                            stepY << 2, G_TX_RENDERTILE, texS << 5, texT << 5, 0x400, 0x400);
                    } else {
                        gSPTextureRectangle(gdl++, x << 2, y << 2, stepX << 2, stepY << 2, G_TX_RENDERTILE, texS << 5,
                                            texT << 5, 0x400, 0x400);
                    }
                }
            }
        }
    }}}

    if (margin != 0) {
        gEXPopScissor(gdl++);
        gDPSetScissor(gdl++, G_SC_NON_INTERLACE, rect->x, rect->y, rect->x + rect->w - 1, rect->y + rect->h - 1);
    }
    gSPSetGeometryMode(gdl++, gCullMode);
    gDPPipeSync(gdl++);
    gDPSetColorDither(gdl++, G_CD_MAGICSQ);
    gDPSetAlphaDither(gdl++, G_AD_DISABLE);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gSPMatrix(gdl++, dyn, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    return gdl;
}
