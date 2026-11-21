# The web page

The game plays in a browser: built with Emscripten on N64Recomp-browser's `frontend/` into `Excitebike64Recompiled.html`
(the launcher, the recomp's menus, SDL2's audio and keyboard, the ROM from Load ROM, and the saves kept in IndexedDB;
`lib/N64Recomp-browser/frontend/CMakeLists.txt`'s header says what each part does), drawn through WebGPU. The page is
built from your ROM like the desktop program, so it is never built in CI and never published with the game in it.

What is the game's own stays here: the `EMSCRIPTEN` branches of `CMakeLists.txt` that make the game a page,
`tools/build_web.sh`, the prebuilt shader list (`src/rt64_shaders.txt`, recorded by `tools/shader_plan.json`; the
list's header says how to record it again), the play plans (`tools/play_plan.json`, `tools/play_plan_pads.json`,
`tools/play_plan_replug.json`, `tools/play_plan_extended.json`) and the survey they share with the shader plan
(`tools/shader_survey.js`). The rest is N64Recomp-browser's: `docs/porting.md` (WebGPU and the shaders, prebuilt shader
lists, Emscripten), `docs/playing.md` (playing and measuring a page), `docs/forks.md` and `docs/findings.md`. The page is published at
[n64hub.com/excitebike-64](https://n64hub.com/excitebike-64/).

## Build it

On Linux with Docker, with the ROM at `roms/excitebike64.us.z64`:

```bash
tools/build_web.sh                  # [cpus] [cmake arguments]
```

It builds `build/web/Excitebike64Recompiled.html` with its `.js`, `.wasm` and `.data` (the recomp's fonts and menus,
nothing of the game) in `tools/docker/Dockerfile.web`, the image every N64Recomp-browser page is built in, with ccache
in `build/ccache-web`, so a rebuild after a patch takes a minute or two. The script's header says each step. Two
things differ from the desktop build:

- The patches library (`patches/`, built for MIPS) is recompiled by the native N64Recomp that `tools/recompile.sh`
  builds, not the runtime's, which Emscripten would build for the browser.
- RT64 builds its pipelines from `src/rt64_shaders.txt` (`RT64_PREBUILT_SHADER_LIST`), in sections: the boot's before
  the game starts, and a track's the first time it draws.

Serve `build/web` over HTTP from localhost (any static server; the page brings its own `coi_serviceworker.js` for the
headers threads need) and open `Excitebike64Recompiled.html` in Chrome or Firefox with WebGPU, then Load ROM.

## The plans

`tools/play_plan.json` (2-3 minutes) plays the game the way a player does, by the keyboard's defaults (Enter is
Start, Space A, WASD or the arrows the stick): Start through the splashes to the title and the main menu, A through
Season's menus to its first race, then a minute and a half of riding with the throttle held, steering each way, with
the pause menu opened and closed. Where the game is comes from its game state, its front end's screen and the race's
frame count, read from RDRAM by `tools/shader_survey.js` (its header has each), so the plan presses on when a screen
has come rather than by fixed times, and its waits in the race are on the race's own clock, 60 a second. Play it with
N64Recomp-browser's `tools/play_page.mjs` (its `docs/playing.md`).

`tools/play_plan_pads.json` plugs four stand-in pads in (N64Recomp-browser's `tools/gamepad_probe.js` as the preload)
and plays an Exhibition race of four players in a split screen of four views, each port's rider ridden by its own
pad, and checks that the game counts four controllers and that the pads rumbled as their riders landed hard or
crashed. Its header says what each step shows.

`tools/play_plan_replug.json`, played with the same preload and `--fault reopen`, plugs in two stand-in pads of one
model and plays a two-player race, each pad pulled out and plugged in again in turn while the other stays in: each
still plays its own rider and rumbles. It then binds the keyboard's P to Start beside Enter in the controls file, and
on the reopen (a player's next visit) P alone takes the title to the main menu.

`tools/play_plan_extended.json` is the long play: the pads plan's four-player race ridden for three and a half minutes,
each pad steering in turn.

## The shader list

`src/rt64_shaders.txt` holds the boot's section (the splashes, the title and the menus) and one for each track: Season's
twenty, the six special tracks (Original Excitebike's included) and the tutorial. The user track is left out, since
the editor builds it from the outdoor tracks' pieces. It is recorded by code with `tools/shader_plan.json`, a page load
a section: the plan reaches each of Season's tracks through Season's menus with the race put on that track, each
special track through its own menu item, and Soccer's with two stand-in pads in, since it takes two players (the list's
header says what each section plays and how to record it again). A play that draws what the list lacks prints the
shaders it missed (`play_page.mjs` counts the misses), which go into the section it was in.

## How it plays

The game draws its races into a 512x240 framebuffer and its menus into a 320x240 one, both shown at 4:3: the
framebuffer's pixel is as wide as the TV shows it (N64Recomp-browser's `docs/forks.md`). Render to RAM is on, copied
a list late: the game reads a race frame's Z buffer back for the stadium lights' glows and flares, and a frame one list
old does for that, so no list waits for the GPU (`src/main/main.cpp` says why).

It draws at the display's rate, RT64's frames between the game's, and at the canvas's size: on an idle host with a
desktop GPU (an RTX 2070), uncapped in Chrome, the regular plan presented 350 frames a second (300-360, held by the
uncapped rate of 360), with no shader the list lacked, no wait for one and no sound short. Each pad plays a port of its
own, its Rumble Pak with it, through a pad pulled out and plugged in again (`tools/play_plan_pads.json`,
`tools/play_plan_replug.json`).
