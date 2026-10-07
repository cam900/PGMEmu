#!/usr/bin/env bash
# Builds a .pgm image for every MAME set PGMBuilder recognises, into roms/.
#
#   scripts/make-pgm.sh [ROMS_DIR]     # default: ../ROMS beside this checkout
#
# PGMBuilder is built from ../PGMBuilder into build/tools/PGMBuilder rather than
# taken from that checkout's own build: a binary there may predate the format
# version this emulator reads, and was found to on 2026-10-07. roms/ is ignored
# by git; images are never committed. Kept free of bash-4 features: macOS ships
# bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
sets="${1:-$root/../ROMS}"
builder_source="$root/../PGMBuilder"
builder_build="$root/build/tools/PGMBuilder"

if [[ ! -d "$builder_source" ]]; then
  echo "error: no PGMBuilder checkout at $builder_source" >&2
  exit 1
fi

cmake -S "$builder_source" -B "$builder_build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$builder_build" > /dev/null

mkdir -p "$root/roms"
"$builder_build/PGMBuilder" --input "$sets" --output "$root/roms" > "$root/build/tools/make-pgm.log" 2>&1 || true

count="$( find "$root/roms" -name '*.pgm' | wc -l | tr -d ' ' )"
echo "$count images in $root/roms (PGMBuilder's log: build/tools/make-pgm.log)"
