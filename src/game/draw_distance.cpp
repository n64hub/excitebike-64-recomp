// The General tab's Draw Distance in the stadium. arn_cull_models sets each stadium model's bits for a view from two
// distances ahead of the camera, per number of screens: past gLoDetailThresh (0x834 with one screen) a model is far, and
// arn_draw draws a far section, and arn_draw_far a far object, under the track's fog, whose colour in the stadium is
// black; past gVisibleThresh (0xFA0) it isn't drawn at all. So the jumps and walls across the stadium are black shapes
// against the lit stands behind them, which are a model of their own, and the farthest pieces are left out.
//
// With Far, us.toml's hook in arn_cull_models, after it has loaded both into its locals (the stack's 0x54, and $v1 for
// 0x58, which the branch's delay slot stores), raises both past the stadium's size: every model in view is drawn, lit,
// as the near ones are. The riders' level of detail (render.c) reads gLoDetailThresh itself and is left as it is.
// Original draws it all as a console does. Outdoor tracks draw by their own visibility tables (tube_cull_sections), not
// these distances: patches/tube_far.c adds what they leave out with Far, asking eb_draw_far.

#include <cstdint>

#include "recomp.h"

#include "eb.h"

namespace {
    // Past any stadium model's distance (the cull's units, a sixteenth of the world's).
    constexpr int32_t far_limit = 0x7FFF;
}

extern "C" void eb_draw_distance(uint8_t* rdram, recomp_context* ctx) {
    if (!eb::draw_far()) {
        return;
    }
    MEM_W(0x54, ctx->r29) = far_limit;
    ctx->r3 = far_limit;
}

// Draw Distance for patches/tube_far.c: 1 with Far ($v0).
extern "C" void eb_draw_far(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = eb::draw_far() ? 1 : 0;
}
