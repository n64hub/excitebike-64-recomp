# CLAUDE.md

A static recompilation of the N64 *Excitebike 64* (USA) (Rev 1) with N64Recomp, N64ModernRuntime, RecompFrontend and
RT64, from the chasmlol/excitebike64 decompilation. Almost all of it is written by AI agents, so this file is what the
next one needs: the rules, where things are, and the facts about the game the code can't say on its own.

## What lives where

This repo is the game: its recompilation, the patches over it, the desktop build and its web page.

- `syms/`: the symbols N64Recomp reads, written from the decomp's tables of USA Rev 1 (`tools/gen_syms.py`, whose
  header says how), never edited by hand: the game image as one section, its functions named as the decomp names them.
- `us.toml`: N64Recomp's configuration, with the game's hooks; `tools/recompile.sh` unpacks the game image from the
  ROM (`tools/unpack_code.py`) and writes `RecompiledFuncs/` with it.
- `patches/` and `patches.toml`: MIPS C against `lib/excitebike64`'s and `lib/ultralib`'s headers, recompiled into
  PatchesLib, where a patch to the game's C goes (`patches/Makefile`'s header says how they build). `ui_funcs.h`,
  `patch_helpers.h` and `recompui_event_structs.h` are RecompFrontend's.
- `src/`: RecompFrontend's main (`src/main/main.cpp`, with the GameEntry), the code section and patches it gives the
  runtime (`src/main/register_overlays.cpp`, `src/main/register_patches.cpp`), the game's start in place of its boot
  loader (`src/game/boot.cpp`), the settings (`src/game/config.cpp`), libultra functions the runtime leaves out
  (`src/game/libultra.cpp`) and the Rumble Pak beside the Controller Pak (`src/game/rumble_pak.cpp`), the race's
  widescreen culling, HUD, sky, particles and riders (`src/game/widescreen.cpp`, `src/game/hud.cpp`,
  `src/game/sky.cpp`, `patches/particles.c`, `patches/riders.c`, and the Desert's ground in `patches/desert.c`), the
  stadium lights' glows out to a wide window's sides (`patches/sun.c`), the main menu's sponsor logo across a wide
  window (`src/game/menu_logo.cpp`), the fades across it too (`src/game/wipe.cpp`), the finish's checkerboard over the
  whole view (`src/game/checkerboard.cpp`), the General tab's Draw Distance (`src/game/draw_distance.cpp`), the boot's
  logos and notices kept at 4:3 in it (`src/game/splash.cpp`), the track editor out to its edges
  (`src/game/editor.cpp`), and a stack trace on a crash, on Windows and Linux (`src/main/crash_trace.cpp`).
- `lib/`: `excitebike64` (the decomp, for `syms/` and the patches' headers), `ultralib` (the libultra the decomp builds
  against, for its headers), upstream `rt64`, `N64ModernRuntime` and `RecompFrontend` at the commits
  N64Recomp-browser's web forks are built on, and `N64Recomp-browser` at a release tag. The web support every recomp's
  page shares is developed and documented there; `tools/build_n64recomp.sh` and `CMakeLists.txt` apply its `patches/`
  to the other three, and `CMakeLists.txt` then this game's own changes to them (`runtime-patches/`, its README lists
  them).
- `docs/web.md`: the web page: `tools/build_web.sh`, its prebuilt shader list (`src/rt64_shaders.txt`,
  `tools/shader_plan.json`, `tools/record_shaders.sh`), its play plans (`tools/play_plan*.json`) and the survey they
  share (`tools/shader_survey.js`).
- Comments beside the code, for how and why: each file's header. Read the header of the file you change and update
  it in the same commit.

## Rules

- No ROMs, ROM-derived assets or extracted game data in git, CI or PRs (`tools/ci/check_repo.py` checks). Nothing from
  the decomp's build is committed but `syms/`. The icon (`icons/icon.svg`, the launcher's thumbnail and the program's
  icon by `tools/icon.py`) is drawn for the project from plain shapes: no logo, character or picture from the game.
- The desktop releases are built where the ROM is, by `tools/release.sh` (its header says how), and published to
  GitHub Releases with their notes in `releases/`.
- The scope is the game as it plays, plus what a web page needs (RT64 interpolation to the display's rate, resolution
  Auto), plus modern enhancements: each an option with an Original value that plays as the console does, none
  changing how the game plays. USA Rev 1 only: the version the decomp matches byte for byte.
- The decomp has no license; it is read and its tables used like a licensed repo's, and credited.
- Write for a stranger, in the present tense: nothing about a contributor's machine, servers, paths or accounts, and
  nothing about where anyone keeps ROMs.
- GPL-3.0, as the recompilations it follows.

## How it fits together

- **The ROM.** 16 MB, CIC-6103. The decomp's `docs/overview.md` describes the game: its threads, game states, data,
  each source file, and what Rev 1 changes from the LodgeNet build it was decompiled from.
- **The code.** A boot loader (ROM 0x1000, run at 0x80000400) shows a splash, unpacks the game image from the BRSOHYES
  container at ROM 0x12230 (four SWD-packed segments) and copies it over itself to 0x80000400. The recomp never runs
  it: N64Recomp reads the unpacked image (`tools/unpack_code.py`, ROM 0x1000 of `build/excitebike64.us.code.z64`), and
  `src/game/boot.cpp`'s entry unpacks it into RDRAM, leaves the header the boot loader leaves at 0x803FFFC0 and 0xBEEF
  at 0x80000402, and starts the game's own entry. During a race the front-end code (0x80073640 on) and the UsrTrk.c
  overlay are given back to the heap, and after it the game unpacks them again from the container to the same
  addresses, through its own code, so they are plain code to the runtime.
- **libultra.** 2.0K, every object's address in the decomp's `libultra_map.json`. Its exception handler
  (exceptasm.o), which the decomp's function table splits elsewhere, is listed by libultra's names so N64Recomp leaves
  it out (`tools/gen_syms.py`).
- **Microcodes.** Graphics: F3DLX2.Rej and F3DEX2.NoN xbus 2.08. The game reads rspboot from the cartridge's IPL3 (ROM
  0xB70). Sound: none; the game mixes on the CPU and gives libultra's audio interface its buffers, paced by its FIFO
  full, which the runtime never sets: `main.cpp`'s `eb_ai_status` sets it for the game's two sound loops.
- **Saves.** The cartridge's 16 Kbit EEPROM: `main.cpp` declares `SaveType::Eep16k`. The Controller Pak (time-trial
  ghosts, user tracks a player copies there) is the runtime's, in port 1 (`main.cpp`'s `controller_pak_ports`):
  `saves/controllerpak_1.mpk`.
- **Threads** (`main.cpp` names them): 0 libultra's PI and VI managers, 1 the idle thread and the scheduler (by
  priority), 3 the game's main at priority 10, 5 the scheduler's client.
- **Scratch memory.** The game uses the framebuffer it isn't drawing and the Z buffer as scratch memory: a race's load
  copies the track's models into the spare framebuffer (tubegfx.c's `tube_build_dlists`), and the height grids, the
  map and the editor's save work in the Z buffer. After each race frame its CPU reads the Z buffer back, for the
  stadium lights' glows and flares (sun.c's `sun_check_occlusion`) and the sky's height (`patches/sky.c` keeps that at
  the whole view), so RT64's Render to RAM is on, copied a list late (`main.cpp`): the late copy keeps every word the
  CPU wrote after the list ended, so it keeps off that scratch. RT64 takes the spare framebuffer's copy for
  something the CPU wrote after the race's first frame filled it black, unless `RT64_HASH_FILLED_FRAMEBUFFERS` is on
  (`CMakeLists.txt`): a flash of noise across the top of the screen in the black before the title.
