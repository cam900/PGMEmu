#!/usr/bin/env bash
# Downloads the SingleStepTests ARM7TDMI suite into build/tools/arm7tdmi/,
# which the tests tagged [cpu-suite] read and skip without.
#
#   scripts/fetch-arm7tdmi-tests.sh
#
# The suite's binary files, a file per encoding, from
# https://github.com/SingleStepTests/ARM7TDMI at a fixed commit, so that a
# result can be reproduced; tests/cpu/Arm7SingleStepTest.cpp reads them as
# they are. Files already present are not fetched again. Kept free of bash-4
# features: macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
commit="${SST_ARM7TDMI_COMMIT:-e3097d88d428752736b949d94c094d5da50f0db6}"
target="$root/build/tools/arm7tdmi"
base="https://raw.githubusercontent.com/SingleStepTests/ARM7TDMI/$commit/v1"

mkdir -p "$target"
names="$( curl -sSL "https://api.github.com/repos/SingleStepTests/ARM7TDMI/git/trees/$commit?recursive=1" \
  | python3 -I -c 'import json, sys; print("\n".join(e["path"][3:] for e in json.load(sys.stdin)["tree"] if e["path"].startswith("v1/") and e["path"].endswith(".json.bin")))' )"

echo "$names" | while IFS= read -r name
do
  [[ -s "$target/$name" ]] && continue
  curl -sSL --fail -o "$target/$name.part" "$base/$name"
  mv "$target/$name.part" "$target/$name"
done
echo "$( ls "$target"/*.json.bin | wc -l | tr -d ' ' ) files in $target"
