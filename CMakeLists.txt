# Excitebike 64: Recompiled: the recompiled game (RecompiledFuncs/, from tools/recompile.sh), the patches over it
# (patches/, PatchesLib), and RecompFrontend's launcher and menus on RT64. The desktop program, and with Emscripten
# (emcmake cmake; tools/build_web.sh) the page, Excitebike64Recompiled.html, through lib/N64Recomp-browser/frontend
# (docs/web.md).
cmake_minimum_required(VERSION 3.20)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
endif()

project(Excitebike64Recompiled LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()

# Opt out of constexpr mutex constructor on windows to prevent vcruntime mismatch crashes
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
endif()

if (APPLE)
    enable_language(OBJC OBJCXX)
endif()

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

if (EMSCRIPTEN)
    # Threads are pthreads (SharedArrayBuffer), and the runtime ends a game thread by throwing. -msimd128 is the web's
    # -march=nehalem: wasm SIMD, which every browser with WebGPU has, for hlslpp's wasm backend and the
    # auto-vectorizer. -mtail-call is wasm's tail calls: with it the recompiled code's jumps to other functions are
    # return_calls (N64Recomp-web's RECOMP_TAIL_CALL), not calls that grow the stack (N64Recomp-browser's README, "What
    # the build needs").
    add_compile_options(-pthread -fwasm-exceptions -msimd128 -mtail-call)
    # With -msimd128 xxHash would take its NEON path through Emscripten's arm_neon.h, which it includes inside its
    # extern "C", where the C++ headers that reaches don't compile. XXH_HAS_INCLUDE is used for nothing else.
    add_compile_options("-DXXH_HAS_INCLUDE(x)=0")
    add_link_options(-pthread -fwasm-exceptions -mtail-call)
    # ccache when it is on PATH, hitting across checkouts (its header says how).
    include(${CMAKE_SOURCE_DIR}/lib/N64Recomp-browser/frontend/build_speed.cmake)
    # The shaders the page specializes at build time and builds before the game needs them, in sections: the boot's
    # before the first frame, the rest as the game first draws them (src/rt64_shaders.txt says how it was recorded).
    # Set whether or not the file is there, never behind an EXISTS: that is read once, at configure, so a build dir
    # configured before the list came kept building pages without one.
    set(RT64_PREBUILT_SHADER_LIST ${CMAKE_SOURCE_DIR}/src/rt64_shaders.txt)
else()
    # The desktop build's compiles go through ccache when it is on PATH, so a new build dir or another checkout of the
    # same code compiles only what changed since a build here. CCACHE_BASEDIR, the checkout, makes its paths relative
    # and CCACHE_NOHASHDIR leaves the build dir out of what a compile is hashed by: without both, clang-cl's compiles
    # in one checkout hit nothing of another's. Debug info goes in each object (/Z7), since /Zi's shared PDB can't be
    # cached, so a hit's names the checkout that compiled it first. Precompiled headers (RmlUi's) are off: ccache can't
    # cache a file that includes one.
    find_program(CCACHE_PROGRAM ccache)
    if (CCACHE_PROGRAM AND NOT CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env CCACHE_BASEDIR=${CMAKE_SOURCE_DIR} CCACHE_NOHASHDIR=1
            ${CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_C_COMPILER_LAUNCHER})
        set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
        foreach (flags C_FLAGS_DEBUG CXX_FLAGS_DEBUG C_FLAGS_RELWITHDEBINFO CXX_FLAGS_RELWITHDEBINFO)
            string(REPLACE "/Zi" "/Z7" CMAKE_${flags} "${CMAKE_${flags}}")
        endforeach()
    endif()
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
# No VI while RT64 is stalled in one list or screen update (N64Recomp-browser's docs/forks.md), on with Emscripten and
# here on the desktop too: gfxRetrace_Callback steps the game whatever its lists do, so a long stall let it run on and
# rewrite the list RT64 was still reading, and on Mesa's software Vulkan the picture stopped for good after 90 s.
set(ULTRAMODERN_VIS_WAIT_FOR_RENDERER ON)
# RT64 interpolates the RSP tasks the game draws a frame in as one frame (RT64_GAME_FRAME_WORKLOADS), on with
# Emscripten and here on the desktop too, so the desktop build draws as the page does.
set(RT64_GAME_FRAME_WORKLOADS ON)
# The frames RT64 draws between the game's pair its transforms as the page's do, with three options off on a desktop
# build and on with Emscripten (N64Recomp-browser's docs/forks.md): no pair across a camera cut (RT64_SKIP_CAMERA_CUTS),
# none between two places that swapped what they drew (RT64_SKIP_PLACE_SWAPS), none with a transform that collapses what
# it draws (RT64_SKIP_COLLAPSED_MATCHES). Without them the race's frames between drew the riders and their shadows
# partway from one place to another, 14 frames unlike both of their neighbours in a race's minute and a half of frame
# dumps, where with them there were 5.
set(RT64_SKIP_CAMERA_CUTS ON)
set(RT64_SKIP_PLACE_SWAPS ON)
set(RT64_SKIP_COLLAPSED_MATCHES ON)
# The race's sky is 2D strips whose texture the game scrolls as the camera turns (src/game/sky.cpp), so RT64 slides the
# texture in the frames between (RT64_INTERPOLATE_RECT_SCROLLS, off on a desktop build and on with Emscripten), or the
# sky stepped a game frame at a time against the track in front: in frame dumps of tools/frame_plan.txt at 16:9, 405
# steps in one place of the horizon over four minutes of races, and none with it.
set(RT64_INTERPOLATE_RECT_SCROLLS ON)
# A race's load copies the track's models into the framebuffer the game isn't showing, and the race's first frame clears
# it black (CLAUDE.md, Scratch memory). RT64 took that copy, still in RDRAM, for something the CPU wrote after the clear
# and put it over the black at the next VI, a frame of noise across the top of the screen in the black before the
# title. So RT64 hashes a framebuffer a list filled whole as the list ends
# (RT64_HASH_FILLED_FRAMEBUFFERS, off on a desktop build and on with Emscripten), and puts over the picture only what
# the CPU writes after.
set(RT64_HASH_FILLED_FRAMEBUFFERS ON)
# What lies past the framebuffer's sides counts only to them when RT64 judges what covers a frame's whole width
# (RT64_SCISSOR_INSIDE_FRAMEBUFFER, off on a desktop build and on with Emscripten): the main menu's sponsor logo draws
# under a scissor as wide as the window (src/game/menu_logo.cpp), and without it the menu's frame and items were drawn
# stretched to the window's width.
set(RT64_SCISSOR_INSIDE_FRAMEBUFFER ON)
# The game draws each rider from above into its shadow texture under F3DLX2.Rej (lib/excitebike64's src/game/shadow.c),
# through a projection that puts all of it past the near plane, which a Rej microcode draws, having no near plane to
# reject by. RT64 takes Rej for NoN there only with RT64_REJ_WITHOUT_NEAR_CLIP (off on a desktop build and on with
# Emscripten): without it every shadow texture stayed empty, and the ground drew no shadow under any bike.
set(RT64_REJ_WITHOUT_NEAR_CLIP ON)
# The game draws each section of a track with a matrix of its own that holds the camera, fogged under one render mode
# past a distance and plain under another nearer (lib/excitebike64's src/game/tubegfx.c), so RT64 left a section that
# crossed that distance, or came into view at the picture's side, unpaired, and drew it where it stood in the frames
# between while the next one moved: a light line across the track and up the canyon walls on Mountain Quarry.
# RT64_MOVE_UNPAIRED_WITH_CAMERA (off on a desktop build and on with Emscripten) draws it as holding still in the world,
# moved back by the step the race's paired sections agree on: in frame dumps of 70 s of that race, frames between with a
# new thin line of the sky's colour went from 45 to none.
set(RT64_MOVE_UNPAIRED_WITH_CAMERA ON)
# Original Excitebike, under Special Tracks, draws the NES picture as 8x8 rects of 16x8 tiles wrapped at 16 texels, the
# 16th of each never filled, and stretches it across a wide window. A texrect stretched so starts partway along a native
# pixel of the screen's square grid, and its first high-res columns took the texel before its first: every tile drew a
# green or black line of that 16th texel down its left edge, through the title's letters, the crowd, the track and the
# HUD (#62). RT64_RECT_OWN_PIXEL_GRID (off on a desktop build and on with Emscripten) steps a texrect's texture a native
# pixel at a time from its own first texcoords, as the console does. Stepped on a grid of the screen's pixels, it drifted
# from the rect, placed on whole pixels: the time HUD drew a line down each glyph's left at 1400x720 (#68).
set(RT64_RECT_OWN_PIXEL_GRID ON)
# The NES picture's playfield scrolls as a row of those rects under the scissor's right edge, and RT64's Fix LR with
# Scissor moved the last one's right edge onto the scissor's whenever it came within a pixel of it: that tile was drawn
# 9 pixels wide, then 7, and the frames between drew it stretched, a shimmer at the picture's top right (#87).
# RT64_FIX_LR_SPANNING_RECTS (off on a desktop build and on with Emscripten) moves an edge only for a rect that spans the
# scissor across: in frame dumps of the NES race, about 11000 stretched last tiles in 766 of 2258 frames went to none.
set(RT64_FIX_LR_SPANNING_RECTS ON)
# The place in the top right (3RD, 6TH) grows and shrinks as it changes, its glyphs drawn as rects that step over more
# texels a pixel than they cover. RT64 draws such a scaled rect upscaled (Upscale 2D), and its last native pixel at a
# high resolution stepped a pixel past the last texel the console samples, into the next glyph on the font page: a thin
# white line down each glyph's right and along its bottom while the place changed (#88).
# RT64_UPSCALED_RECT_LAST_TEXEL (off on a desktop build and on with Emscripten) keeps an upscaled rect to the texels
# the console samples.
set(RT64_UPSCALED_RECT_LAST_TEXEL ON)
# Up to four players, each a port of their own (src/main/main.cpp, get_connected_device_info): the game turns
# RecompFrontend's single-player mode off, and plays as single-player mode until players are assigned, as with
# Emscripten, so a player alone needn't assign their pad or keyboard first. And until then each pad plays a port of its
# own, in the order they were connected, the keyboard port 1 (RecompFrontend-web's option): four people with four pads
# race four riders without assigning them.
set(RECOMPINPUT_SINGLE_PLAYER_UNTIL_ASSIGNED ON)
set(RECOMPINPUT_PADS_IN_ORDER ON)
add_compile_definitions(HLSL_CPU)

if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # SDL 2.30's SDL_endian.h defines _m_prefetch, which clang declares as a builtin in prfchwintrin.h
    add_compile_definitions(__PRFCHWINTRIN_H)
endif()

# lib/rt64, lib/N64ModernRuntime and lib/RecompFrontend are upstream's, at the commits N64Recomp-browser's web forks
# are built on, with its patches/ applied and then this game's own changes to them (runtime-patches/), if
# tools/build_n64recomp.sh hasn't already. The desktop build takes them too: the code the patched N64Recomp writes uses
# the patched recomp.h (RECOMP_TAIL_CALL), and the web support in them is behind __EMSCRIPTEN__.
include(${CMAKE_SOURCE_DIR}/runtime-patches/apply.cmake)
eb_apply_patches()

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)

if (EMSCRIPTEN)
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64Recomp-browser/frontend ${CMAKE_BINARY_DIR}/web_frontend)
    # fmt 11.1 (N64Recomp's) leans on <cstdlib> arriving through other headers, which Emscripten's libc++ doesn't do.
    target_compile_options(fmt PRIVATE -include cstdlib)
endif()

target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)

# RecompiledFuncs: the game's code, from N64Recomp us.toml (tools/recompile.sh)
add_library(RecompiledFuncs STATIC)

target_compile_options(RecompiledFuncs PRIVATE
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-implicit-function-declaration
)
# A call to an undeclared function links to a trap in WebAssembly, so it is an error there (N64Recomp-web's funcs.h
# declares every function the recompiled code calls).
if (EMSCRIPTEN)
    target_compile_options(RecompiledFuncs PRIVATE -Werror=implicit-function-declaration)
endif()

target_include_directories(RecompiledFuncs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

file(GLOB FUNC_C_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.cpp)
target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

# PatchesLib: the patches (patches/), compiled for MIPS by patches/Makefile and recompiled by N64Recomp patches.toml
# (the runtime's own N64Recomp, target N64RecompCLI). Needs a clang with a MIPS backend and ld.lld
# (PATCHES_C_COMPILER, PATCHES_LD). EB_PATCHES=OFF is for CI only, which stops before linking: the game needs the
# patches for its enhancements.
option(EB_PATCHES "Build the patches library (the game needs it)" ON)
set(PATCHES_C_COMPILER clang CACHE STRING "clang with a MIPS backend, for patches/")
set(PATCHES_LD ld.lld CACHE STRING "ld.lld, for patches/")

if (EB_PATCHES)
    add_library(PatchesLib STATIC)

    target_compile_options(PatchesLib PRIVATE
        -fno-strict-aliasing
        -Wno-unused-variable
        -Wno-implicit-function-declaration
    )

    target_include_directories(PatchesLib PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    )

    target_sources(PatchesLib PRIVATE
        ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c
        ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c
    )

    # make is incremental, so this runs every build and relinks only when a patch source changed.
    add_custom_target(PatchesBin
        COMMAND ${CMAKE_COMMAND} -E env CC=${PATCHES_C_COMPILER} LD=${PATCHES_LD} make
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/patches
        BYPRODUCTS ${CMAKE_SOURCE_DIR}/patches/patches.elf
    )

    # The recompiler runs where the build does: for the page that is tools/recompile.sh's native N64Recomp, not the
    # runtime's target, which Emscripten would build for the browser.
    if (EMSCRIPTEN)
        set(EB_N64RECOMP ${CMAKE_SOURCE_DIR}/build/n64recomp/N64Recomp)
        # The patches call the game's functions: undeclared, a call links to a trap in WebAssembly.
        target_compile_options(PatchesLib PRIVATE -Werror=implicit-function-declaration
            "SHELL:-include ${CMAKE_SOURCE_DIR}/RecompiledFuncs/funcs.h")
    else()
        set(EB_N64RECOMP N64RecompCLI)
    endif()

    add_custom_command(OUTPUT
            ${CMAKE_SOURCE_DIR}/patches/patches.bin
            ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c
            ${CMAKE_SOURCE_DIR}/RecompiledPatches/recomp_overlays.inl
            ${CMAKE_SOURCE_DIR}/RecompiledPatches/funcs.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/RecompiledPatches
        COMMAND ${EB_N64RECOMP} patches.toml
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${CMAKE_SOURCE_DIR}/patches/patches.elf ${CMAKE_SOURCE_DIR}/patches.toml
    )

    add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.h
        COMMAND file_to_c ${CMAKE_SOURCE_DIR}/patches/patches.bin eb_patches_bin
            ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.h
        DEPENDS ${CMAKE_SOURCE_DIR}/patches/patches.bin
    )
endif()

add_executable(Excitebike64Recompiled)

target_sources(Excitebike64Recompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/src/main/main.cpp
    ${CMAKE_SOURCE_DIR}/src/main/crash_trace.cpp
    ${CMAKE_SOURCE_DIR}/src/main/register_overlays.cpp
    ${CMAKE_SOURCE_DIR}/src/game/config.cpp
    ${CMAKE_SOURCE_DIR}/src/game/boot.cpp
    ${CMAKE_SOURCE_DIR}/src/game/libultra.cpp
    ${CMAKE_SOURCE_DIR}/src/game/rumble_pak.cpp
    ${CMAKE_SOURCE_DIR}/src/game/widescreen.cpp
    ${CMAKE_SOURCE_DIR}/src/game/hud.cpp
    ${CMAKE_SOURCE_DIR}/src/game/checkerboard.cpp
    ${CMAKE_SOURCE_DIR}/src/game/editor.cpp
    ${CMAKE_SOURCE_DIR}/src/game/menu_logo.cpp
    ${CMAKE_SOURCE_DIR}/src/game/stats_card.cpp
    ${CMAKE_SOURCE_DIR}/src/game/sky.cpp
    ${CMAKE_SOURCE_DIR}/src/game/wipe.cpp
    ${CMAKE_SOURCE_DIR}/src/game/splash.cpp
    ${CMAKE_SOURCE_DIR}/src/game/draw_distance.cpp
)
if (EB_PATCHES)
    target_sources(Excitebike64Recompiled PRIVATE ${CMAKE_SOURCE_DIR}/src/main/register_patches.cpp)
    # register_patches.cpp includes the patches' section table, which the command above writes in the middle of a
    # build: without this edge an incremental build after a patch moved kept the old table.
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/main/register_patches.cpp PROPERTIES
        OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/RecompiledPatches/recomp_overlays.inl)
endif()

target_include_directories(Excitebike64Recompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/sse2neon
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/inc
    ${CMAKE_SOURCE_DIR}/lib/rt64/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/rhi
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/render
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompinput/include
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/include
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/nativefiledialog-extended/src/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# The launcher's thumbnail (icons/512.png, drawn by tools/icon.py from icons/icon.svg): no art from the game.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
    COMMAND file_to_c ${CMAKE_SOURCE_DIR}/icons/512.png icon_bytes ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
    DEPENDS ${CMAKE_SOURCE_DIR}/icons/512.png
)

target_sources(Excitebike64Recompiled PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c)

if (CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(Excitebike64Recompiled PRIVATE
        -march=nehalem
        -fno-strict-aliasing
        -fms-extensions
    )
else()
    target_compile_options(Excitebike64Recompiled PRIVATE
        -fno-strict-aliasing
        -fms-extensions
    )
endif()

if (MSVC)
    # Disable identical code folding, since this breaks mod function patching as multiple functions can get merged into one.
    target_link_options(Excitebike64Recompiled PRIVATE /OPT:NOICF)
endif()

if (WIN32)
    include(FetchContent)

    if (DEFINED ENV{SDL2_VERSION})
        set(SDL2_VERSION $ENV{SDL2_VERSION})
    else()
        set(SDL2_VERSION "2.30.3")
    endif()

    FetchContent_Declare(
        sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
    )
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(Excitebike64Recompiled PRIVATE
        ${sdl2_SOURCE_DIR}/include
    )
    target_link_directories(Excitebike64Recompiled PRIVATE
        ${sdl2_SOURCE_DIR}/lib/x64
    )

    add_custom_command(TARGET Excitebike64Recompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:Excitebike64Recompiled>)

    # A dev build (Debug, RelWithDebInfo) opens a console beside the window for its log; Release, which
    # tools/release.sh builds for players, is a windowed program only.
    set_target_properties(
        Excitebike64Recompiled
        PROPERTIES
            LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
            LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:CONSOLE"
            LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
    )

    target_sources(Excitebike64Recompiled PRIVATE ${CMAKE_SOURCE_DIR}/icons/app.rc)
    target_link_libraries(Excitebike64Recompiled PRIVATE SDL2 Winmm.lib Dbghelp.lib)
    add_compile_definitions(NOMINMAX)
endif()

if (APPLE)
    find_package(SDL2 REQUIRED)
    target_include_directories(Excitebike64Recompiled PRIVATE ${SDL2_INCLUDE_DIRS})

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)

    target_link_libraries(Excitebike64Recompiled PRIVATE ${CMAKE_DL_LIBS} Threads::Threads SDL2::SDL2)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED)
    add_compile_definitions("PLUME_SDL_VULKAN_ENABLED")
    add_compile_definitions("RT64_SDL_WINDOW_VULKAN")
    target_include_directories(Excitebike64Recompiled PRIVATE ${SDL2_INCLUDE_DIRS})

    find_package(Freetype REQUIRED)
    include_directories(${FREETYPE_LIBRARIES})
    target_link_libraries(Excitebike64Recompiled PRIVATE ${FREETYPE_LIBRARIES} SDL2::SDL2)

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)

    target_link_libraries(Excitebike64Recompiled PRIVATE "-latomic -static-libstdc++" ${CMAKE_DL_LIBS} Threads::Threads)
    # The window's icon, built in (SDL_SetWindowIcon in src/main/main.cpp).
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/window_icon_bmp.c ${CMAKE_CURRENT_BINARY_DIR}/window_icon_bmp.h
        COMMAND file_to_c ${CMAKE_SOURCE_DIR}/icons/icon.bmp window_icon_bmp
            ${CMAKE_CURRENT_BINARY_DIR}/window_icon_bmp.c ${CMAKE_CURRENT_BINARY_DIR}/window_icon_bmp.h
        DEPENDS ${CMAKE_SOURCE_DIR}/icons/icon.bmp file_to_c
    )
    target_sources(Excitebike64Recompiled PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/window_icon_bmp.c)
endif()

# dxc, for recompui's shaders
set(DXC_COMMON_OPTS "-I${PROJECT_SOURCE_DIR}/src")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_0" "-D DYNAMIC_RENDER_PARAMS")
set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_0" "-D DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
set(DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T cs_6_0")
set(DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T gs_6_0")
set(DXC_RT_OPTS "${DXC_COMMON_OPTS}" "-D" "RT_SHADER" "-T" "lib_6_3" "-fspv-target-env=vulkan1.1spirv1.4" "-fspv-extension=SPV_KHR_ray_tracing" "-fspv-extension=SPV_EXT_descriptor_indexing")

if (CMAKE_HOST_WIN32)
    set(DXC "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe")
elseif (CMAKE_HOST_APPLE)
    # Apple's binary is universal, so it'll work on both x86_64 and arm64
    set(DXC "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/arm64/dxc-macos")
elseif (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    set(DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/x64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc-linux")
else()
    set(DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/arm64/dxc-linux")
endif()

set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)
set(RECOMP_FRONTEND_RT64_PATH ${CMAKE_SOURCE_DIR}/lib/rt64)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/RecompFrontend)

if (EB_PATCHES)
    target_link_libraries(Excitebike64Recompiled PRIVATE PatchesLib)
endif()
target_link_libraries(Excitebike64Recompiled PRIVATE
    RecompiledFuncs
    recompui
    recompinput
    librecomp
    ultramodern
    rt64
)

if (EMSCRIPTEN)
    # The page (lib/N64Recomp-browser/frontend), with the menus' fonts and icons and the controller mappings in its .data
    # file. No ROM: the player picks theirs in the launcher, and it stays in the browser.
    n64recomp_browser_frontend_page(Excitebike64Recompiled)
    target_link_options(Excitebike64Recompiled PRIVATE
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/recompcontrollerdb.txt@/recompcontrollerdb.txt")
else()
    target_link_libraries(Excitebike64Recompiled PRIVATE nfd)
endif()

set_property(TARGET Excitebike64Recompiled PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
