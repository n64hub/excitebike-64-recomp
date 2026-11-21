// The race's particles out to the window's edges: dust, smoke, splashes, exhaust puffs, mud, sparkles and sparks past
// the 4:3 frame's sides in a wide window, where the track is drawn (src/game/widescreen.cpp). particles_draw (the
// decomp's src/game/mud2.c, copied here as it is but for the changes below) projects each particle on the CPU and
// skips one whose screen x is outside the screen's 0 to gScreenWidth, killing it with one view, so a rider's dust at
// the window's side went where the old frame's edge was. It draws each with gSPTextureRectangle, whose x is 10.2 fixed
// point in 12 bits, 0 to 1023.75, so nothing it draws can start left of the screen. And it draws them under the
// view's scissor, the screen's width, which RT64 widens to the window for triangles but not for rectangles: a dust
// cloud drawn across the old frame's side was cut off straight down it.
//
// So for a view RT64 widens to the window (eb::view_widened), the bound reaches as far past each side as the window
// shows, eb_particle_margin's pixels of the screen (src/game/widescreen.cpp); the particles are drawn with RT64's
// gEXTextureRectangle, whose coordinates are signed 16 bits, at the same place and size, the sparks too, a pixel each
// (the game's gDPFillRectangle, which is 10 bits unsigned); and under an extended scissor as far out, the view's own
// rows high, the game's put back after them. With no margin (Original, a window no wider than 4:3, split screens side
// by side) all three are the game's own.

#include "patches.h"

// The decomp's types and constants, without its prototypes and globals (functions.h declares __osMotorAccess as
// ultralib's doesn't): the three functions this calls are declared here, the globals defined below.
#include "common.h"
#include "constants.h"
#include "structs.h"
#include "rt64_extended_gbi.h"

void mtx3i_from_angles(s32 *m, s32 ax, s32 ay, s32 az);
void vec3i_mul_mtx3i(s32 *v, s32 *m, s32 *out);
s32 project_to_screen(s32 x, s32 y, s32 z, s32 *sx, s32 *sy, s32 *sz, s32 *w, s32 mtx, s32 view);

// The game's data by address (the decomp's versions/us_rev1/symbols.txt), most of it past the image syms/'s data
// symbols end at.
#define gNumViews (*(s32 *)0x800C2ED4)
#define gSurfaceTable ((Surface *)0x800C99C0)
#define gNumScreens (*(s32 *)0x800E21B8)
#define gPauseMenu (*(s32 *)0x800E40B0)
#define gRiders ((Rider *)0x800E4160)
#define gCamPos ((Vec3i *)0x800E8A30)
#define sParticles (*(Particle **)0x800E9BB4)
#define sExhaustPuffTimers ((s32 *)0x800E9BC8)
#define sClodTextures (*(TexSet **)0x800E9C0C)
#define sSplashTextures (*(TexSet **)0x800E9C10)
#define sSparkleTextures (*(TexSet **)0x800E9C30)
#define sExhaustTextures (*(TexSet **)0x800E9C3C)
#define sPuffTextures (*(TexSet **)0x800E9C40)
#define gCurTrack_4 (*(Cfg **)0x800F0704)
#define gScreenWidth (*(volatile s32 *)0x800F1D8C)
#define gScreenHeight (*(volatile s32 *)0x800F1D90)
#define gViewRects ((ViewRect (*)[4])0x800E2038)

// gViewRects' views: each view's rectangle in pixels, by the views' count, the full views while the race's 3D draws.
typedef struct ViewRect {
    s32 x, y, w, h;
} ViewRect;

// How far past each side of the screen view `view` shows in the window, in the screen's pixels (widescreen.cpp).
s32 eb_particle_margin(s32 view);

static inline s32 project(Vec3f *p, s32 view, s32 mtx, s32 *sx, s32 *sy, s32 *sz, s32 *w) {
    if (gCurTrack_4->f2C == TRACK_ENV_STADIUM) {
        return project_to_screen(p->x * 1000.0f * 0.0625f, p->y * 1000.0f * 0.0625f, p->z * 1000.0f * 0.0625f, sx, sy,
                             sz, w, mtx, view);
    } else {
        return project_to_screen(p->x * 1000.0f * 0.0625f - (gCamPos[view].x >> 4),
                             p->y * 1000.0f * 0.0625f - (gCamPos[view].y >> 4),
                             p->z * 1000.0f * 0.0625f - (gCamPos[view].z >> 4), sx, sy, sz, w, mtx, view);
    }
}

// The game's test of a particle's place on the screen, as far past its sides as the window shows.
#define OFF_SCREEN(sx, sy) ((sx) < -margin || gScreenWidth + margin < (sx) || (sy) < 0 || gScreenHeight < (sy))

// A particle's sprite: the game's rectangle, or with a margin the same one with signed coordinates.
static Gfx *particle_rect(Gfx *gdl, s32 margin, s32 ulx, s32 uly, s32 lrx, s32 lry, s32 dsdx, s32 dtdy) {
    if (margin != 0) {
        gEXTextureRectangle(gdl++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, ulx, uly, lrx, lry, G_TX_RENDERTILE, 0, 0, dsdx,
                            dtdy);
    } else {
        gSPTextureRectangle(gdl++, ulx, uly, lrx, lry, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
    }
    return gdl;
}

RECOMP_PATCH Gfx *particles_draw(Gfx *gdl, s32 view, s32 dyn) {
    s32 mtx[9];
    s32 ofs[3];
    s32 out[3];
    Vec3f pos;
    s32 sx, sy, sz, w;
    s32 i, size, grow, height, dsdx, dtdy, alpha, lastFrame;
    s32 aux; /* a particle's surface, or the smoke's fade with distance */
    Rider *rider;
    s32 margin;
    ViewRect *rect;

    if (gNumScreens >= 4) {
        return gdl;
    }

    margin = eb_particle_margin(view);
    if (margin != 0) {
        rect = &gViewRects[gNumViews - 1][view];
        gEXEnable(gdl++);
        gEXPushScissor(gdl++);
        gEXSetScissor(gdl++, G_SC_NON_INTERLACE, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, -margin, rect->y,
                      gScreenWidth + margin, rect->y + rect->h);
    }

    gDPPipeSync(gdl++);
    gDPSetColorDither(gdl++, G_CD_NOISE);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetTextureLUT(gdl++, G_TT_NONE);
    gDPSetCombine(gdl++, 0x30B261, 0xFF67FFFF);
    gDPSetRenderMode(gdl++, 0x404A50, 0);
    gSPSetGeometryMode(gdl++, G_ZBUFFER);
    gDPSetDepthSource(gdl++, G_ZS_PRIM);
    gDPLoadTextureBlock(gdl++, sPuffTextures->tex[0].data, G_IM_FMT_IA, G_IM_SIZ_8b, sPuffTextures->tex[0].width * 8,
                        sPuffTextures->tex[0].height, 0, G_TX_CLAMP, G_TX_CLAMP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);

    for (i = 0; i < PARTICLE_COUNT; i++) {
        if (sParticles[i].life >= 0 && sParticles[i].type == PARTICLE_DUST) {
            if (project(&sParticles[i].pos, view, dyn, &sx, &sy, &sz, &w) != 0) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (OFF_SCREEN(sx, sy)) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (w < 5001 && w != 0) {
                size = 15000 / w;
                if (gNumViews == 2) {
                    size = size * 2 / 3;
                } else if (gNumViews >= 3) {
                    size /= 2;
                }
                if (size > 48) {
                    size = 48;
                }
                if (sParticles[i].vel.y > 0.0f) {
                    grow = ((sParticles[i].maxLife - sParticles[i].life) << 10) / sParticles[i].maxLife + 0x80;
                    if (grow > 0x400) {
                        grow = 0x400;
                    }
                    if (gPauseMenu == 0) {
                        sParticles[i].vel.y -= 0.005f;
                    }
                } else {
                    grow = ((sParticles[i].maxLife - sParticles[i].life) << 9) / sParticles[i].maxLife + 0x100;
                    if (grow > 0x200) {
                        grow = 0x200;
                    }
                }
                size = size * grow / 256;
                if (size < 3) {
                    size = 3;
                }
                dsdx = (sPuffTextures->tex[0].width << 13) / size;
                aux = sParticles[i].surface;
                height = size * 2 / 3;
                alpha = 0xFF - gSurfaceTable[aux].color[3];
                alpha = (((sParticles[i].life << 7) / sParticles[i].maxLife + 0x7F) * alpha) >> 8;
                dtdy = (sPuffTextures->tex[0].height << 10) / height;
                if (alpha != 0) {
                    gDPSetPrimColor(gdl++, 0, 0, (gSurfaceTable[aux].color[0] * sParticles[i].r) >> 8,
                                    (gSurfaceTable[aux].color[1] * sParticles[i].g) >> 8, (gSurfaceTable[aux].color[2] * sParticles[i].b) >> 8,
                                    alpha);
                    gDPSetPrimDepth(gdl++, sz - 0x40, 0);
                    gdl = particle_rect(gdl, margin, (sx - size / 2) << 2, (sy - height / 2) << 2, (sx + size / 2) << 2,
                                        (sy + height / 2) << 2, dsdx, dtdy);
                }
            }
        }
    }

    for (i = 0; i < PARTICLE_COUNT; i++) {
        if (sParticles[i].life >= 0 && sParticles[i].type == PARTICLE_SMOKE) {
            if (project(&sParticles[i].pos, view, dyn, &sx, &sy, &sz, &w) != 0) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (OFF_SCREEN(sx, sy)) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (w != 0) {
                grow = ((sParticles[i].maxLife - sParticles[i].life) * 0x2800) / sParticles[i].maxLife + 0x500;
                size = 15000 / w;
                if (grow > 0x400) {
                    grow = 0x400;
                }
                size = size * grow / 256;
                if (size < 20) {
                    size = 20;
                }
                if (size > 100) {
                    size = 100;
                }
                dsdx = (sPuffTextures->tex[0].width << 13) / size;
                alpha = (sParticles[i].life * 0xC0) / sParticles[i].maxLife + 0x3F;
                height = size * 2 / 3;
                aux = 0xFF - (w * 0xFF) / 15000;
                dtdy = (sPuffTextures->tex[0].height << 10) / height;
                if (aux < 0) {
                    aux = 0;
                }
                if (aux > 255) {
                    aux = 255;
                }
                alpha = (alpha * aux) >> 8;
                if (alpha != 0) {
                    gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, alpha);
                    gDPSetPrimDepth(gdl++, sz - 0x40, 0);
                    gdl = particle_rect(gdl, margin, (sx - size / 2) << 2, (sy - height / 2) << 2, (sx + size / 2) << 2,
                                        (sy + height / 2) << 2, dsdx, dtdy);
                }
            }
        }
    }

    for (i = 0; i < PARTICLE_COUNT; i++) {
        if (sParticles[i].life >= 0 && sParticles[i].type == PARTICLE_SPLASH) {
            if (project(&sParticles[i].pos, view, dyn, &sx, &sy, &sz, &w) != 0) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (OFF_SCREEN(sx, sy)) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (w != 0) {
                size = 7500 / w;
                if (gNumViews >= 3) {
                    size /= 2;
                }
                if (size > 48) {
                    size = 48;
                }
                grow = ((sParticles[i].maxLife - sParticles[i].life) << 10) / sParticles[i].maxLife + 0x100;
                if (grow > 0x400) {
                    grow = 0x400;
                }
                size = size * grow / 256;
                if (size < 3) {
                    size = 3;
                }
                dsdx = (sSplashTextures->tex[0].width << 13) / size;
                height = size * 2 / 3;
                dtdy = (sSplashTextures->tex[0].height << 10) / height;
                gDPSetPrimColor(gdl++, 0, 0, 255, 255, 255, (sParticles[i].life * 245) / sParticles[i].maxLife + 20);
                grow = (u32)(i * 123 + sParticles[i].maxLife - sParticles[i].life) % sSplashTextures->count;
                gDPLoadTextureBlock(gdl++, sSplashTextures->tex[grow].data, G_IM_FMT_IA, G_IM_SIZ_8b, sSplashTextures->tex[grow].width * 8,
                                    sSplashTextures->tex[grow].height, 0, G_TX_CLAMP, G_TX_CLAMP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);
                gDPSetPrimDepth(gdl++, sz - 0x40, 0);
                gdl = particle_rect(gdl, margin, (sx - size / 2) << 2, (sy - height / 2) << 2, (sx + size / 2) << 2,
                                    (sy + height / 2) << 2, dsdx, dtdy);
            }
        }
    }

    for (i = 0; i < 2 * 8; i++) { /* two puffs per bike */
        if (sExhaustPuffTimers[i] != 0) {
            grow = ((15 - sExhaustPuffTimers[i]) * sExhaustTextures->count) / 15;
            if (grow < 0) {
                grow = 0;
            }
            if ((u32)grow >= sExhaustTextures->count) {
                grow = sExhaustTextures->count - 1;
            }
            gDPLoadTextureBlock(gdl++, sExhaustTextures->tex[grow].data, G_IM_FMT_IA, G_IM_SIZ_8b, sExhaustTextures->tex[grow].width * 8,
                                sExhaustTextures->tex[grow].height, 0, G_TX_CLAMP, G_TX_CLAMP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);
            rider = &gRiders[i / 2];
            /* behind the exhaust, moving back as the puff ages */
            mtx3i_from_angles(mtx, rider->drawRot[0], rider->drawRot[1], rider->drawRot[2]);
            ofs[0] = 100;
            ofs[1] = 700;
            ofs[2] = (sExhaustPuffTimers[i] - 15) * 40 - 650;
            vec3i_mul_mtx3i(ofs, mtx, out);
            pos.x = (f32)(out[0] + rider->drawPos.x) / 1000.0f;
            pos.y = (f32)(out[1] + rider->drawPos.y) / 1000.0f;
            pos.z = (f32)(out[2] + rider->drawPos.z) / 1000.0f;
            if (project(&pos, view, dyn, &sx, &sy, &sz, &w) != 0) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (OFF_SCREEN(sx, sy)) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (w != 0) {
                size = 10000 / w;
                if (gNumViews >= 2) {
                    size /= 2;
                }
                grow = ((15 - sExhaustPuffTimers[i]) << 8) / 15;
                grow += 0x100;
                size = size * grow / 256;
                if (size > 48) {
                    size = 48;
                }
                if (size < 3) {
                    size = 3;
                }
                if (gRiders[i / 2].engine.overheated != 0) {
                    size = size * 3 / 2;
                    grow = (sExhaustPuffTimers[i] * 63) / 15 + 0xC0;
                    gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, grow);
                } else {
                    if (gRiders[i / 2].input & INPUT_Z) {
                        size = size * 3 / 2;
                    }
                    grow = (sExhaustPuffTimers[i] * 127) / 15 + 0x80;
                    gDPSetPrimColor(gdl++, 0, 0, 0x60, 0x60, 0x80, grow);
                }
                dsdx = (sExhaustTextures->tex[0].width << 13) / size;
                height = size * 2 / 3;
                dtdy = (sExhaustTextures->tex[0].height << 10) / height;
                gDPSetPrimDepth(gdl++, sz - 0x40, 0);
                gdl = particle_rect(gdl, margin, (sx - size / 2) << 2, (sy - height / 2) << 2, (sx + size / 2) << 2,
                                    (sy + height / 2) << 2, dsdx, dtdy);
            }
        }
    }

    for (i = 0; i < PARTICLE_COUNT; i++) {
        if (sParticles[i].life >= 0 && sParticles[i].type == PARTICLE_MUD) {
            if (project(&sParticles[i].pos, view, dyn, &sx, &sy, &sz, &w) != 0) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (OFF_SCREEN(sx, sy)) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else {
                gDPLoadTextureBlock(gdl++, sClodTextures->tex[i & 3].data, G_IM_FMT_IA, G_IM_SIZ_8b,
                                    sClodTextures->tex[i & 3].width * 8, sClodTextures->tex[i & 3].height, 0, G_TX_CLAMP, G_TX_CLAMP, 5,
                                    5, G_TX_NOLOD, G_TX_NOLOD);
                if (w != 0) {
                    size = 15000 / w;
                    size /= 6;
                    if (gNumViews >= 3) {
                        size /= 2;
                    }
                    if (size > 24) {
                        size = 24;
                    }
                    grow = ((sParticles[i].maxLife - sParticles[i].life) << 9) / sParticles[i].maxLife + 0x100;
                    if (grow > 400) {
                        grow = 400;
                    }
                    size = size * grow / 256;
                    if (size < 3) {
                        size = 3;
                    }
                    dsdx = (sClodTextures->tex[0].width << 13) / size;
                    height = size * 2 / 3;
                    dtdy = (sClodTextures->tex[0].height << 10) / height;
                    aux = sParticles[i].surface;
                    gDPSetPrimColor(gdl++, 0, 0, (gSurfaceTable[aux].color[4] * sParticles[i].r) >> 8,
                                    (gSurfaceTable[aux].color[5] * sParticles[i].g) >> 8, (gSurfaceTable[aux].color[6] * sParticles[i].b) >> 8,
                                    (sParticles[i].life << 7) / sParticles[i].maxLife + 0x40);
                    gDPSetPrimDepth(gdl++, sz - 0x40, 0);
                    gdl = particle_rect(gdl, margin, (sx - size / 2) << 2, (sy - height / 2) << 2, (sx + size / 2) << 2,
                                        (sy + height / 2) << 2, dsdx, dtdy);
                }
            }
        }
    }

    lastFrame = -1;
    if (gNumViews == 1) {
        gDPSetPrimColor(gdl++, 0, 0, 255, 255, 255, 255);
        for (i = 0; i < PARTICLE_COUNT; i++) {
            if (sParticles[i].life >= 0 && sParticles[i].type == PARTICLE_SPARKLE) {
                if (project(&sParticles[i].pos, view, dyn, &sx, &sy, &sz, &w) != 0) {
                    if (gNumViews == 1) {
                        sParticles[i].life = -1;
                    }
                } else if (OFF_SCREEN(sx, sy)) {
                    if (gNumViews == 1) {
                        sParticles[i].life = -1;
                    }
                } else {
                    /* the sparkle's frame: 0-2 then back down */
                    alpha = (sParticles[i].maxLife - sParticles[i].life) / 2;
                    if (alpha >= 3) {
                        alpha = 5 - alpha;
                    }
                    if (alpha >= 0) {
                        if (lastFrame != alpha) {
                            lastFrame = alpha;
                            gDPLoadTextureBlock(gdl++, sSparkleTextures->tex[alpha].data, G_IM_FMT_IA, G_IM_SIZ_8b,
                                                sSparkleTextures->tex[alpha].width * 8, sSparkleTextures->tex[alpha].height, 0, G_TX_CLAMP, G_TX_CLAMP,
                                                5, 5, G_TX_NOLOD, G_TX_NOLOD);
                        }
                        if (w != 0) {
                            size = 30000 / w;
                            size /= 6;
                            if (gNumViews >= 3) {
                                size /= 2;
                            }
                            if (size > 24) {
                                size = 24;
                            }
                            if (size < 15) {
                                size = 15;
                            }
                            dsdx = (sSparkleTextures->tex[alpha].width << 13) / size;
                            height = size * 2 / 3;
                            dtdy = (sSparkleTextures->tex[alpha].height << 10) / height;
                            gDPSetPrimDepth(gdl++, sz - 0x40, 0);
                            gdl = particle_rect(gdl, margin, (sx - size / 2) << 2, (sy - height / 2) << 2,
                                                (sx + size / 2) << 2, (sy + height / 2) << 2, dsdx, dtdy);
                        }
                    }
                }
            }
        }
    }

    gDPPipeSync(gdl++);
    gDPSetRenderMode(gdl++, 0x442230, 0);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    for (i = 0; i < PARTICLE_COUNT; i++) {
        if (sParticles[i].life >= 0 && sParticles[i].type == PARTICLE_SPARK) {
            if (project(&sParticles[i].pos, view, dyn, &sx, &sy, &sz, &w) != 0) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else if (OFF_SCREEN(sx, sy)) {
                if (gNumViews == 1) {
                    sParticles[i].life = -1;
                }
            } else {
                w = 0x7F - (w << 7) / 2000;
                if (w < 0) {
                    w = 0;
                }
                if (w > 127) {
                    w = 127;
                }
                gDPSetPrimColor(gdl++, 0, 0, w + 0x80, w + 0x80, 0, 255);
                gDPSetPrimDepth(gdl++, sz - 0x100, 0);
                if (margin != 0) {
                    /* the pixel as a rectangle, which the primitive colour fills whatever the tile holds */
                    gEXTextureRectangle(gdl++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, sx << 2, sy << 2, (sx + 1) << 2,
                                        (sy + 1) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
                } else {
                    gDPFillRectangle(gdl++, sx, sy, sx + 1, sy + 1);
                }
            }
        }
    }

    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetCombine(gdl++, 0xFFFFFF, 0xFFFCF87C);
    gDPSetTextureLUT(gdl++, G_TT_RGBA16);
    gDPSetDepthSource(gdl++, G_ZS_PIXEL);
    gDPSetColorDither(gdl++, G_CD_MAGICSQ);
    if (margin != 0) {
        gEXPopScissor(gdl++);
    }
    return gdl;
}
