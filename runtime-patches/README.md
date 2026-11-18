# runtime-patches

This game's own changes to the submodules, applied on top of N64Recomp-browser's patches, in order of name, by
`apply.cmake` here when `CMakeLists.txt` configures and when `tools/build_n64recomp.sh` runs. A new change is a new file
per submodule, `<submodule>-<n>-<what>.patch`, numbered after that submodule's highest, with a line here.

- `rt64-0001-frame-dump-shown-shape.patch`: `RT64_FRAME_DUMP` writes each frame at the shape it is shown, its target's
  columns averaged by its pixel's shape on the TV (rt64-web's `VI::pixelAspect()`), so a dump of the race in a 4:3
  window is 4:3 as the window is, not 2.13:1.
