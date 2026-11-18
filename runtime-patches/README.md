# runtime-patches

This game's own changes to the submodules, applied on top of N64Recomp-browser's patches, in order of name, by
`apply.cmake` here when `CMakeLists.txt` configures and when `tools/build_n64recomp.sh` runs. A new change is a new file
per submodule, `<submodule>-<n>-<what>.patch`, numbered after that submodule's highest, with a line here.

- `N64ModernRuntime-0001-no-message-for-an-unset-event.patch`: an event no queue was set for sends nothing, as
  libultra's dispatch does. The game sets no queue for OS_EVENT_SP (its scheduler waits for the DP), and the runtime
  sent each RSP task's message to a null queue and crashed.
