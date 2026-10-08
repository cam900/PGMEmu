#!/usr/bin/env bash
# Downloads the SingleStepTests Z80 suite into build/tools/z80/, which the
# tests tagged [cpu-suite] read and skip without.
#
#   scripts/fetch-z80-tests.sh
#
# About 1.4 GB of JSON, a file per opcode, from
# https://github.com/SingleStepTests/z80 at a fixed commit, so that a result can
# be reproduced. Each file is gzipped as it arrives, which leaves about 250 MB.
# Files already present are not fetched again. Kept free of bash-4 features:
# macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
commit="${SST_Z80_COMMIT:-ebe1875d48f374bcfd4b505d8eb8ee751568b5f7}"
target="$root/build/tools/z80"
base="https://raw.githubusercontent.com/SingleStepTests/z80/$commit/v1"

mkdir -p "$target"
# The contents API lists at most 1000 entries and the suite has more, so the
# names come from the commit's tree.
names="$( curl -sSL "https://api.github.com/repos/SingleStepTests/z80/git/trees/$commit?recursive=1" \
  | python3 -I -c 'import json, sys; print("\n".join(e["path"][3:] for e in json.load(sys.stdin)["tree"] if e["path"].startswith("v1/") and e["path"].endswith(".json")))' )"

echo "$names" | while IFS= read -r name
do
  [[ -s "$target/$name.gz" ]] && continue
  curl -sSL --fail -o "$target/$name.part" "$base/${name// /%20}"
  gzip -9 -c "$target/$name.part" > "$target/$name.gz.part"
  mv "$target/$name.gz.part" "$target/$name.gz"
  rm "$target/$name.part"
done
echo "$( ls "$target"/*.json.gz | wc -l | tr -d ' ' ) files in $target"
