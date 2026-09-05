#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/environment.sh"
cd "$WORK/decomp"
uv run configure.py
ninja -j 8
mips-linux-gnu-ld -T undefined_syms.txt -T undefined_syms_auto.txt -Map build/pokemonsnap.relocs.map -T pokemonsnap.ld --emit-relocs -o build/pokemonsnap.relocs.elf
cp build/pokemonsnap.relocs.elf "$ROOT/"
cp "$ROOT/lib/rt64/include/rt64_extended_gbi.h" include/
cd "$ROOT"
"$WORK/recompiler/N64Recomp" pokemonsnap.us.toml
python3 tools/hook_funcs.py
python3 tools/gen_overlays.py pokemonsnap.relocs.elf
python3 macos/script/patch_audio.py
make -C "$ALIAS/patches" DECOMP="$WORK/decomp" -j8
"$WORK/recompiler/N64Recomp" patches.toml
