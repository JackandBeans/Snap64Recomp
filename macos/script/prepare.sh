#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/environment.sh"
[[ $(uname -m) == arm64 ]] || { echo 'This build targets Apple Silicon.' >&2; exit 1; }
ROM="${1:?Usage: prepare.sh /path/to/Pokemon-Snap-USA.z64}"
[[ "$(shasum -a 1 "$ROM" | cut -d ' ' -f1)" == edc7c49cc568c045fe48be0d18011c30f393cbaf ]] || { echo 'Expected the original USA big-endian ROM.' >&2; exit 1; }
mkdir -p "$WORK"
# Copy only into private ignored build inputs; never modify the supplied ROM.
if [[ ! "$ROM" -ef "$ROOT/pokemonsnap.z64" ]]; then cp "$ROM" "$ROOT/pokemonsnap.z64"; fi
cd "$ROOT"
python3 tools/fetch_deps.py
if [[ ! -d "$WORK/decomp/.git" ]]; then
  git clone https://github.com/ethteck/pokemonsnap.git "$WORK/decomp"
  git -C "$WORK/decomp" checkout 3a236dc20cd7f34dea188ef8bc33a31154f0ee97
fi
[[ $(git -C "$WORK/decomp" rev-parse HEAD) == 3a236dc20cd7f34dea188ef8bc33a31154f0ee97 ]] || { echo 'Unexpected decomp revision.' >&2; exit 1; }
if git -C "$WORK/decomp" apply --check "$ROOT/macos/decomp-build.patch"; then
  git -C "$WORK/decomp" apply "$ROOT/macos/decomp-build.patch"
else
  git -C "$WORK/decomp" apply --reverse --check "$ROOT/macos/decomp-build.patch"
fi
cp "$ROOT/pokemonsnap.z64" "$WORK/decomp/pokemonsnap.z64"
cargo install --git https://github.com/decompals/pigment64.git --rev 0fa6b5cf41bb8ec0cf6b82687076931a2e5552c5 --root "$WORK/toolchain" --locked
cd "$WORK/decomp"
uv sync
uv run configure.py --setup
cmake -S "$ALIAS/lib/N64ModernRuntime/N64Recomp" -B "$WORK/recompiler" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/recompiler" -j 8
cmake -S "$ALIAS/lib/rt64/src/contrib/spirv-cross" -B "$WORK/spirv-cross" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_ENABLE_TESTS=OFF
cmake --build "$WORK/spirv-cross" --target spirv-cross -j 8
mkdir -p "$ROOT/lib/rt64/src/contrib/spirv-cross/bin/arm64"
cp "$WORK/spirv-cross/spirv-cross" "$ROOT/lib/rt64/src/contrib/spirv-cross/bin/arm64/"
"$ROOT/macos/script/regenerate_game.sh"
