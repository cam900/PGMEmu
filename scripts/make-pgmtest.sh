#!/usr/bin/env bash
# Builds one page of the PGMTest ROM, as a BIOS program, into build/tools/.
#
#   scripts/make-pgmtest.sh PAGE      # e.g. system_basics
#
# Prints the path of the pgm_p02s.u20 it built. PGMTest is a replacement for
# the BIOS program: run it with --bios <that directory> --bios ../ROMS/pgm.zip,
# so that the tiles and samples still come from the real BIOS. Needs the
# m68k-elf toolchain and SDCC, as PGMTest does. Kept free of bash-4 features:
# macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
page="${1:?usage: make-pgmtest.sh PAGE}"
source="$root/../PGMTest"
build="$root/build/tools/pgmtest-$page"

cmake -S "$source" -B "$build" \
  -DCMAKE_TOOLCHAIN_FILE="$source/cmake/m68k-elf.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPGM_PAGE="$page" > /dev/null
cmake --build "$build" > /dev/null
echo "$build/pgm/pgm_p02s.u20"
