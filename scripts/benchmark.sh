#!/usr/bin/env bash
# Measures how fast the emulator runs each game headless and unthrottled.
#
#   scripts/benchmark.sh [FRAMES]     # 3600 frames, a minute of emulated time, if not given
#
# The BIOS alone, then every image in roms/, each from power-up. Games whose
# protection is not emulated yet run all the same, stuck where they wait for
# it, so their numbers say little until it is. Needs a release build. Kept
# free of bash-4 features: macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
frames="${1:-3600}"
cli="$root/build/release/src/pgm_cli/pgmemu-cli"
sources=( --bios "$root/../ROMS/pgm.zip" --rom-dir "$root/roms" )

"$cli" "${sources[@]}" --benchmark pgm --frames "$frames" 2> /dev/null
for image in "$root"/roms/*.pgm; do
  "$cli" "${sources[@]}" --benchmark "$( basename "$image" .pgm )" --frames "$frames" 2> /dev/null || true
done
