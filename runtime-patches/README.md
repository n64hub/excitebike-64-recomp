# runtime-patches

This game's own changes to the submodules, applied on top of N64Recomp-browser's patches, in order of name, by
`apply.cmake` here when `CMakeLists.txt` configures and when `tools/build_n64recomp.sh` runs. A new change is a new file
per submodule, `<submodule>-<n>-<what>.patch`, numbered after that submodule's highest, with a line here.

- `N64ModernRuntime-0001-no-message-for-an-unset-event.patch`: an event no queue was set for sends nothing, as
  libultra's dispatch does. The game sets no queue for OS_EVENT_SP (its scheduler waits for the DP), and the runtime
  sent each RSP task's message to a null queue and crashed.
- `rt64-0001-vi-pixel-aspect.patch`: `VI::pixelAspect()`, the shape of a framebuffer's pixel on the TV, which the
  window's and Manual's aspect ratios are divided by. The game draws its races into a 512x240 framebuffer, which was
  shown at 2.13:1 and widened from there; it is 4:3 now.
- `rt64-0002-frame-dump-shown-shape.patch`: `RT64_FRAME_DUMP` writes each frame at the shape it is shown, its target's
  columns averaged by that pixel aspect, so a dump of the race in a 4:3 window is 4:3 as the window is, not 2.13:1.
