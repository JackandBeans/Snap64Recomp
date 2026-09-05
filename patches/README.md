# Game-side patches

Changes to Pokemon Snap's own code, compiled and recompiled separately from
the game and linked ahead of it, so a function here replaces the game's own of
the same name. **The ROM is never rebuilt**, so its layout, the `manual_funcs`
addresses in `pokemonsnap.us.toml`, the overlay section table and every
absolute address the port depends on all stay valid.

This is the mechanism N64Recomp provides for patching (`single_file_output`
plus link order), and the same approach shipped ports use to change game
behaviour.

## Building

Needs the decompilation (`~/pokemonsnap` under WSL) built at least once, since
the patch links against its symbols and is compiled with its copy of IDO.

```sh
# 1. Symbol files: what the patch links against, and what the recompiler
#    resolves calls into the game with. Both are tracked; rerun only when the
#    decomp's symbols change. Use the plain ELF that matches the ROM (see
#    BUILDING.md step 2 about the stale relocatable ELF).
python tools/gen_reference_syms.py ~/pokemonsnap/build/pokemonsnap.elf \
    patches/pokemonsnap.syms.toml patches/game_syms.ld

# 2. Compile and link the patch elf.
make -C patches DECOMP=$HOME/pokemonsnap

# 3. Recompile it to C.
~/N64Recomp/build/N64Recomp patches.toml

# 4. Build the port. CMake picks up RecompiledPatches/patches.c automatically.
cmake --build build-win --config Release --target Snap64Recomp --parallel
```

`build/` is generated and not tracked. `game_syms.ld` and
`pokemonsnap.syms.toml` are generated too, but tracked (names and addresses
only), so a checkout can build the patches without the decomp's ELF.

## How the pieces fit

- **IDO, not gcc.** The decomp builds with `tools/ido7.1/cc` and no MIPS gcc is
  installed, so the patch uses the same compiler as the code it replaces.
- **Calls stay unresolved on purpose.** `--emit-relocs` keeps a relocation on
  each call out of the patch, and the recompiler matches those relocations to
  the game's functions through `func_reference_syms_file`. Resolving them at
  link time instead bakes in an address it cannot attribute to anything, which
  fails with "No function found for jal target". Data symbols are the
  opposite: they are fixed locations, so `game_syms.ld` supplies them.
  `--noinhibit-exec` is needed because an unresolved call cannot satisfy
  R_MIPS_26's range.
- **`.recomp_patch` marks a replacement.** The recompiler rejects a function
  that shadows a game function without being marked, and one that is marked
  but matches nothing. IDO has no section attributes, so `patch.ld` places
  every function in the patch build into that section.
- **Link order decides the winner.** `RecompiledPatches/patches.c` compiles
  straight into the executable, so it is resolved before the linker reaches
  `recomp_funcs`. MSVC still needs `/FORCE:MULTIPLE` to accept the duplicate,
  since it pulls the containing object in for its other symbols.

## render_patch.c

`renPrepareModelMatrix` and `ren_func_80013C5C`, reproduced from
`src/sys/render.c` with one addition: a `gEXMatrixGroup` naming the `DObj` whose matrices follow. That is
what lets RT64 pair an object with itself between frames rather than inferring
identity from geometry, which cannot separate rows of identical vegetation
quads, tiled wall and sky segments, or two of the same Pokemon.

The other `renRenderModelNodeType*` and `renRenderModelType*` functions take
the same one-line addition when wanted; this one is the smallest and was done
first.

## The earlier route, and why it was abandoned

An earlier attempt at the same goal edited the decomp's `render.c` directly
and rebuilt the ROM (a diff of it was kept here as `render-matrix-tagging.patch`
until the release; it carried the decomp's own lines and is gone). It was
superseded by the patch mechanism above, which needs no rebuild; the record
of why is worth keeping: growing `.main` by 0x50 bytes moved
`.main_bss`, invalidated the `manual_funcs` addresses and the overlay table,
and left the original cartridge no longer matching the recompiled code.
