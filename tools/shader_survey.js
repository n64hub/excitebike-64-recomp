// Evaluated in the page by N64Recomp-browser's tools/record_shaders.mjs (tools/shader_plan.json) and play_page.mjs
// (tools/play_plan.json): reads where the game is from RDRAM, so a plan presses on through the menus by what the game
// shows rather than by fixed times, and puts the shader plan's race on the track it records. The addresses are USA Rev
// 1's (syms/, the decomp's versions/us_rev1/symbols.txt). It offers
//
//   clock()          the game's frames (gFrameCounter, 0x800E2958), for a plan's ["clock", "clock()", 60]
//   state()          the game state running (gCurGameState, 0x800C3308, into gGameStates' four of 0x1C bytes): 0 the
//                    front end (its menus, and the title's attract race), 1 a race, 2 the editor, 3 Original
//                    Excitebike
//   screen()         the front end's screen (sCurScreen, 0x800F5164, into sScreens' 0x2C bytes a screen; the decomp's
//                    include/structs/shell.h): 14 the title, 8 the main menu, 6 Season's rounds, 9 rider select
//   racing()         whether a race is under way: the race state with its frame count (gRaceFrame, 0x800E8070) past
//                    the countdown's three seconds
//   setTrack(t)      makes the race rider select starts be on track t (the decomp's enum TrackId): gCurTrack
//                    (0x800F0704) to gTracks' entry (0x800CBB50, 0xA0 bytes a track) and the season's round to it too
//                    (gSeason, 0x800CB270: its cup's tracks[] at +8, its round at +0x64), since the game sets
//                    gCurTrack from the round as a season's race starts. So a shader plan reaches every one of
//                    Season's tracks from its one menu path. The special tracks have modes of their own, which the
//                    game sets up only through their menu's items, so a plan reaches them there (special()).
//   special(i)       opens every item of the special tracks menu (gSpecialTracksItems, 0x800CDCB0, 0x10 bytes an
//                    item, SCREEN_ITEM_LOCKED in its flags at +8) and puts its cursor (0x800F0718) on item i: 0
//                    Desert, 1 Stunt Course, 2 Original Excitebike, 3 Soccer, 4 Hill Climb, 5 Excite 3D
//   menu(i)          puts the main menu's cursor (0x800F0714) on item i: 0 Season, 3 Special Tracks
//   difficulty(d)    puts the difficulty popup's cursor (gGame.difficulty, 0x800F0B20) on d: 0 the tutorial, which
//                    Season starts in place of its rounds, 1-3 Novice to Expert
//   track()          the track gCurTrack is (its index in gTracks)
//   until(test, ms)  resolves true once test() is, polled every 100 ms, or false after ms
(() => {
  const FRAMES = 0x800E2958, GAME_STATE = 0x800C3308, GAME_STATES = 0x800C3298, CUR_SCREEN = 0x800F5164;
  const SCREENS = 0x800CDF7C, RACE_FRAME = 0x800E8070, CUR_TRACK = 0x800F0704, TRACKS = 0x800CBB50;
  const SEASON = 0x800CB270, SPECIAL_ITEMS = 0x800CDCB0, SPECIAL_CURSOR = 0x800F0718, MAIN_CURSOR = 0x800F0714;
  const DIFFICULTY = 0x800F0B20, CONT_COUNT = 0x800C326D;
  window.clock = () => n64r(FRAMES);
  window.state = () => (n64r(GAME_STATE) - GAME_STATES) / 0x1C;
  window.screen = () => (n64r(CUR_SCREEN) - SCREENS) / 0x2C;
  window.racing = () => state() === 1 && n64r(RACE_FRAME) > 240;
  const w16 = (a, v) => {
    const sh = (a & 2) ? 0 : 16;
    n64w(a & ~3, (n64r(a & ~3) & ~(0xFFFF << sh)) | ((v & 0xFFFF) << sh));
  };
  const r16 = (a) => (n64r(a & ~3) >>> ((a & 2) ? 0 : 16)) & 0xFFFF;
  window.setTrack = (track) => {
    n64w(CUR_TRACK, TRACKS + track * 0xA0);
    const season = n64r(SEASON);
    if (season !== 0) w16(n64r(season) + 8 + r16(season + 0x64) * 2, track);
    return `track ${track}`;
  };
  window.special = (i) => {
    for (let k = 0; k < 6; k++) w16(SPECIAL_ITEMS + k * 0x10 + 8, r16(SPECIAL_ITEMS + k * 0x10 + 8) & ~1);
    n64w(SPECIAL_CURSOR, i);
    return `special ${i}, controllers ${n64r(CONT_COUNT & ~3) >>> (8 * (3 - (CONT_COUNT & 3))) & 0xFF}`;
  };
  window.menu = (i) => {
    n64w(MAIN_CURSOR, i);
    return `main menu item ${i}`;
  };
  window.difficulty = (d) => {
    n64w(DIFFICULTY, d);
    return `difficulty ${d}`;
  };
  window.track = () => (n64r(CUR_TRACK) - TRACKS) / 0xA0;
  window.until = (test, ms) => new Promise((resolve) => {
    const from = Date.now();
    const poll = () => {
      if (test()) resolve(true);
      else if (Date.now() - from > ms) resolve(false);
      else setTimeout(poll, 100);
    };
    poll();
  });
  return `survey: state ${state()} screen ${screen()}`;
})()
