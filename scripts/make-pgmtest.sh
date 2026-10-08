#!/usr/bin/env bash
# Builds one page of the PGMTest ROM, as a BIOS program, into build/tools/.
#
#   scripts/make-pgmtest.sh PAGE      # e.g. system_basics
#   scripts/make-pgmtest.sh --all     # every page, for the regression suite
#
# Prints the path of the pgm_p02s.u20 it built. PGMTest is a replacement for
# the BIOS program: run it with --bios <that directory> --bios ../ROMS/pgm.zip,
# so that the tiles and samples still come from the real BIOS. Needs the
# m68k-elf toolchain and SDCC, as PGMTest does. Kept free of bash-4 features:
# macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
page="${1:?usage: make-pgmtest.sh PAGE | --all}"
source="$root/../PGMTest"

build_page() {
  local build="$root/build/tools/pgmtest-$1"
  cmake -S "$source" -B "$build" \
    -DCMAKE_TOOLCHAIN_FILE="$source/cmake/m68k-elf.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPGM_PAGE="$1" > /dev/null 2>&1
  cmake --build "$build" > /dev/null
  echo "$build/pgm/pgm_p02s.u20"
}

if [[ "$page" == "--all" ]]; then
  for file in "$source"/src/pages/*.c; do
    name="$( basename "$file" .c )"
    build_page "$name"
  done
else
  build_page "$page"
fi
