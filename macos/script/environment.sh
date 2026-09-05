#!/bin/bash
# Source this from a macOS helper. The alias accommodates vendor build rules
# that cannot quote spaces or parentheses in checkout paths.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KEY="$(printf %s "$ROOT" | shasum -a 256 | cut -c1-12)"
ALIAS="/tmp/snap64-macos-${UID}-${KEY}"
if [[ -e "$ALIAS" || -L "$ALIAS" ]]; then
  [[ -L "$ALIAS" && "$(readlink "$ALIAS")" == "$ROOT" ]] || { echo "Build alias collision: $ALIAS" >&2; exit 1; }
else
  ln -s "$ROOT" "$ALIAS"
fi
WORK="$ALIAS/.macos-work"
BUILD="$ALIAS/build-macos"
export PATH="$WORK/toolchain/bin:$PATH"
