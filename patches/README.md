# Decomp patches

Changes to the Pokemon Snap decompilation (`~/pokemonsnap` under WSL), kept
here because they belong to this port but live in another tree.

Apply from the decomp root:

```sh
git apply /mnt/c/Users/<you>/PokemonSnapRecomp/patches/render-matrix-tagging.patch
cp /mnt/c/Users/<you>/PokemonSnapRecomp/lib/rt64/include/rt64_extended_gbi.h include/
```

## render-matrix-tagging.patch

Gives RT64 real object identity for frame interpolation, the way ports built
for RT64 do it. Interpolation blends each frame's matrices with the previous
frame's and needs to know which object every matrix belongs to; a display list
does not say, and identity guessed from geometry cannot separate rows of
identical vegetation quads, tiled wall and sky segments, or two of the same
Pokemon. Every attempt to infer it renderer-side failed, and the failures are
recorded in the port's memory notes so they are not retried.

`renPrepareModelMatrix(Gfx** gfxPtr, DObj* dobj)` receives the owning object in
its **second** argument -- the first is a display-list pointer, which is what
earlier host-side hooks mistakenly read. The patch precedes every model matrix
with a `gEXMatrixGroup` carrying that DObj, under `G_EX_ORDER_LINEAR` so an
object's nth matrix pairs with its own nth matrix from the previous frame, and
enables the extended commands from `renPrepareCameraMatrix`, which runs ahead
of object rendering each frame.

Status: written, compiles and links. **Not yet shipped**, because rebuilding
the game grows `.main` by 0x50 bytes and that invalidates four things the port
currently depends on:

1. The ROM layout shifts, so the original `pokemonsnap.z64` no longer matches
   what the recompiled code expects. The port would have to load the rebuilt
   ROM, which means updating `SNAP_ROM_HASH` in `src/main.cpp`.
2. `.main_bss` moves with it, so the port's hardcoded addresses shift --
   `SNAP_SP_IMEM_OKAY` / `SNAP_SP_DMEM_OKAY` in `src/overlay_hook.cpp` and any
   other absolute bss address.
3. `manual_funcs` in `pokemonsnap.us.toml` are hardcoded vram addresses found
   by a prologue scan; they need re-scanning against the new ELF. N64Recomp
   fails on `manualfunc_80028DA4` until they are.
4. The overlay section table needs regenerating. `RecompiledFuncs/` already
   contains an N64Recomp-generated `recomp_overlays.inl`; whether it can
   replace the hand-generated `src/recomp_overlays.inl` (which currently wins
   on include order) is the open question there.

Rebuild pipeline once those are handled:

```sh
cd ~/pokemonsnap && ninja                     # checksum WILL fail: the ROM changed on purpose
mips-linux-gnu-ld --emit-relocs -T undefined_syms.txt -T undefined_syms_auto.txt \
    -Map build/pokemonsnap.relocs.map -T pokemonsnap.ld -o build/pokemonsnap.relocs.elf
~/N64Recomp/build/N64Recomp <toml with /mnt/c output paths>
python tools/hook_funcs.py                    # from the port root
cmake --build build-win --config Release --target PokemonSnapRecomp --parallel
```
