#!/usr/bin/env bash
# Generates RecompiledFuncs/ from roms/excitebike64.us.z64: the game image unpacked from the ROM
# (tools/unpack_code.py, into build/excitebike64.us.code.z64), then its code from the symbols in syms/ with N64Recomp
# (us.toml). The game mixes its sound on the CPU, so there is no audio microcode to recompile. RecompiledFuncs/ is
# wiped and written again, and the game's CMake configure needs it; a file that comes out the same as before keeps its
# old time, so a build recompiles only the ones that changed. N64Recomp is the one the runtime pins, built by
# tools/build_n64recomp.sh, which takes the arguments.
#
# Usage: tools/recompile.sh [cmake arguments, e.g. -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl]
set -euo pipefail
cd "$(dirname "$0")/.."
bash tools/build_n64recomp.sh "$@"
py=python3
command -v py > /dev/null && py="py -3"
mkdir -p build
$py -I tools/unpack_code.py roms/excitebike64.us.z64 build/excitebike64.us.code.z64
old="$(mktemp -d)"
trap 'rm -rf "$old"' EXIT
mkdir -p RecompiledFuncs
mv RecompiledFuncs "$old/"
mkdir RecompiledFuncs
build/n64recomp/N64Recomp us.toml > recomp.log 2>&1 || { tail -20 recomp.log; exit 1; }
for f in RecompiledFuncs/*; do
    prev="$old/$f"
    if cmp -s "$f" "$prev"; then touch -r "$prev" "$f"; fi
done
