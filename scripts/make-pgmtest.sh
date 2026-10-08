#!/usr/bin/env bash
# Builds one page of PGMTest, the test ROM in the MiSTer core's testroms/, as a
# BIOS program, into build/tools/.
#
#   scripts/make-pgmtest.sh PAGE      # e.g. system_basics
#   scripts/make-pgmtest.sh --all     # every page, for the regression suite
#
# Prints the path of the pgm_p02s.u20 it built. PGMTest is a replacement for
# the BIOS program: run it with --bios <that directory> --bios ../ROMS/pgm.zip,
# so that the tiles and samples still come from the real BIOS.
#
# The pages are those the regression suite was recorded with: testroms/ as git
# tree TESTROMS_TREE, the same object in the core's repository at
# MiSTer-devel and in wickerwaka's, where it was first, whatever either has
# checked out. It is extracted from the checkout beside this one
# (../Arcade-IGSPGM_MiSTer) into build/tools/testroms, so nothing is built
# inside it, and scripts/testroms.patch is applied to the copy:
# system_basics.c writes the palette in a layout the headers no longer have,
# and does not compile. Needs the m68k-elf toolchain and SDCC, as the core's
# Makefile does. Kept free of bash-4 features: macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
page="${1:?usage: make-pgmtest.sh PAGE | --all}"
core="$root/../Arcade-IGSPGM_MiSTer"
copy="$root/build/tools/testroms"
TESTROMS_TREE=4550f1e3a359b86236aa1dd09d2fa57a2ce7fe7c

if [[ ! -d "$core" ]]; then
  echo "error: no MiSTer core checkout at $core" >&2
  echo "       (git clone https://github.com/MiSTer-devel/Arcade-IGSPGM_MiSTer.git beside this one)" >&2
  exit 1
fi
if ! git -C "$core" cat-file -e "$TESTROMS_TREE" 2> /dev/null; then
  echo "error: the core's checkout lacks testroms/ as $TESTROMS_TREE; fetch its whole history" >&2
  exit 1
fi

rm -rf "$copy"
mkdir -p "$copy"
git -C "$core" archive "$TESTROMS_TREE" | tar xf - -C "$copy"
patch --quiet -d "$copy" -p1 < "$root/scripts/testroms.patch"

build_page() {
  local out="$root/build/tools/pgmtest-$1/pgm"
  make -C "$copy" PAGE="$1" BUILD_DIR="build/$1" > /dev/null
  mkdir -p "$out"
  cp "$copy/build/$1/pgm/pgm_p02s.u20" "$out/"
  echo "$out/pgm_p02s.u20"
}

if [[ "$page" == "--all" ]]; then
  for file in "$copy"/pages/*.c; do
    build_page "$( basename "$file" .c )"
  done
else
  build_page "$page"
fi
