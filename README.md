# Excitebike 64: Recompiled

> **Made with AI.** Almost all of the code, documentation and tests in this repo were written by AI agents
> (Claude), directed and play-tested by a person. None of it is offered to the upstream projects it builds on, which
> don't take AI-written code.

A native PC port of the N64 *Excitebike 64* (USA) (Rev 1), made by static recompilation
([N64Recomp](https://github.com/N64Recomp/N64Recomp) + [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) +
[RT64](https://github.com/rt64/rt64), with [RecompFrontend](https://github.com/N64Recomp/RecompFrontend)'s launcher
and menus): the game's own MIPS code translated to C and run natively, with RT64 drawing it. The symbols and the
patches' headers come from the [chasmlol/excitebike64](https://github.com/chasmlol/excitebike64) decompilation. It is
not an emulator.

The repo holds no ROM, no asset from the game and no data extracted from it: you supply your own ROM, and nothing in
git or CI is made from one except the symbol names and addresses in `syms/`.

**Status:** it boots and plays: the splashes, the title, the menus, Season's races and the rest of its tracks (each
drawn as its page's shader list was recorded), Exhibition races of up to four players in a split screen, each on a
controller of their own with its Rumble Pak, and the pause menu, with sound. The season and the settings are kept
in the cartridge's EEPROM across a restart. On a window wider than 4:3, one player's race and two players' one above
the other reach the window's edges, sky and all, the HUD at its sides; side by side, three or four players and the
menus stay 4:3 in the middle, the menus' backdrop and its sponsor logo reaching the edges. Not played yet: the track
editor and Custom Tracks, and time trials' ghosts, which the game keeps on a Controller Pak that the runtime says isn't
there.

It plays in a browser too, at [n64hub.com/excitebike-64](https://n64hub.com/excitebike-64/) (`docs/web.md`), and the
desktop builds for Windows and Linux are on the [Releases](https://github.com/n64hub/excitebike-64-recomp/releases)
page.

## The ROM

*Excitebike 64* (USA) (Rev 1), as a big-endian `.z64`: 16 MiB, SHA-1 `c2fac422af135409c1b38569b309f9d452c81c35`.
Other regions and revisions are not supported.

## Building

You need that ROM at `roms/excitebike64.us.z64`, and Python 3 for `tools/unpack_code.py`. On Windows: LLVM's
clang-cl, CMake 3.31 and Ninja, and a clang with a MIPS backend plus ld.lld for `patches/`. On Linux: `clang lld
libsdl2-dev libgtk-3-dev libfreetype-dev`, CMake 3.31 and Ninja.

From the repo's root in a bash shell:

```sh
git -c core.longpaths=true submodule update --init --recursive   # paths past Windows' 260 characters
bash tools/recompile.sh -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl   # the game's code as C
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl \
      -DPATCHES_C_COMPILER=<MIPS clang> -DPATCHES_LD=<ld.lld>
ninja -C build Excitebike64Recompiled
```

With [ccache](https://ccache.dev) on `PATH` every compile goes through it, so a new build dir or another checkout
of the same code compiles only what changed since the last build on that computer (CMakeLists.txt says how).

On Linux the same lines take `clang` and `clang++` in place of `clang-cl`, and the system's `clang` and `ld.lld` for
`patches/`. `tools/recompile.sh` builds N64Recomp from the submodule the first time, unpacks the game's code from the
ROM (the cartridge holds it packed, and a boot loader unpacks it on a console), then translates it. Run
`build/Excitebike64Recompiled` (`.exe` on Windows) from the repo's root, pick the ROM with Load ROM, then Start Game.
`tools/release.sh`'s header says how a release is built.

`syms/` is written from the decomp's own tables of USA Rev 1 by `tools/gen_syms.py`, with nothing built: only a move
of `lib/excitebike64` needs it again.

## License

GPL-3.0 (`LICENSE`), as the recompilations it follows. The decompilation has no license; this project reads it and
uses its tables as it would a licensed repo's, and credits it here.
