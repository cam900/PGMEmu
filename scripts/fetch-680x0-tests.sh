#!/usr/bin/env bash
# Downloads the SingleStepTests 68000 suite into build/tools/680x0/, which the
# tests tagged [cpu-suite] read and skip without.
#
#   scripts/fetch-680x0-tests.sh
#
# About 200 MB of gzipped JSON, a file per instruction, from
# https://github.com/SingleStepTests/680x0 at a fixed commit, so that a result
# can be reproduced. Files already present are not fetched again. Kept free of
# bash-4 features: macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
commit="${SST_680X0_COMMIT:-e0d5ece9670205cc84a0101081837deb446f86a3}"
target="$root/build/tools/680x0"
base="https://raw.githubusercontent.com/SingleStepTests/680x0/$commit/68000/v1"

mkdir -p "$target"
names="$( curl -sSL "https://api.github.com/repos/SingleStepTests/680x0/contents/68000/v1?ref=$commit" \
  | python3 -I -c 'import json, sys; print("\n".join(e["name"] for e in json.load(sys.stdin) if e["name"].endswith(".json.gz")))' )"

for name in $names
do
  [[ -s "$target/$name" ]] && continue
  curl -sSL --fail -o "$target/$name.part" "$base/$name"
  mv "$target/$name.part" "$target/$name"
done
echo "$( ls "$target"/*.json.gz | wc -l | tr -d ' ' ) files in $target"
