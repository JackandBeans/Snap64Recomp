# Mods

Snap64 Recomp loads mods the way the other N64 recompilations do: an `.nrm`
file in the `mods/` folder next to the executable, built with N64Recomp's
mod tool from code that hooks, follows or replaces the game's own functions.
The loader has been in the port since 1.0.0; what this page adds is the kit
for writing one, and what the port does and does not do with mods today.

## Installing a mod

Put the `.nrm` in `mods/` beside `Snap64Recomp.exe` (or the Linux binary)
and start the port. A mod is enabled the first time it is found unless its
own manifest says otherwise. The game's Options screen has a **Mods** row
(it stands where the stock Return row was; B returns), and Esc or a pad's
Select opens the same screen anywhere: it lists
every mod in the folder, six to a screen, with On or Off beside each, and
A turns the selected one on or off. The change is written to `mods.json`
at once and takes effect the next time the game starts, which the help
line says under the mod's own description. `mods.json` beside the folder is
the same list in a file. Each mod's own settings go in `mod_config/`. The
log names every mod it opens and every one it loads, and says why one did
not: a mod built for another game, a version older than the mod asks for,
a missing dependency.

No mod ships with the port and none is endorsed. A mod runs inside the
recompiled game, with the game's memory and the port's patches, and can
change anything; the port's promise of console behaviour by default holds
for an empty `mods/` folder. Content made from another game's data is not
something this project hosts, links or helps install.

## Writing a mod

The kit is two repositories of their own, written and tested against this
release. Neither is public at the time of 1.0.9; this page says what they
hold, and will link them when they are up.

* Snap64RecompModTemplate, a working mod to start from:
  an example hook behind an option, the MIPS build (clang and lld, with
  the decompilation's headers), the manifest, and the modding headers.
* Snap64RecompSyms:
  the game's function and variable names with their addresses, which the
  mod tool resolves a mod against. Generated from the decompilation's ELF
  by this repository's `tools/gen_reference_syms.py`; the functions file
  is the same one the port's own patches are built with.

The template's README has the tools and the steps. In short: `make` builds
`build/mod.elf`, and `RecompModTool mod.toml build` turns it into the
`.nrm`. `RECOMP_HOOK("name")` runs your code before a game function,
`RECOMP_HOOK_RETURN("name")` after it, and `RECOMP_PATCH` replaces one; the
names are the decompilation's ([ethteck/pokemonsnap](https://github.com/ethteck/pokemonsnap)),
and the symbol files list every one the tool knows. Options declared in
`mod.toml` are read with `recompconfig.h`; `recomp_printf` writes to
`snap64.log`; the collections of `recompdata.h` hold what a mod keeps
between calls.

## What the port does not have yet

* Ordering, and each mod's options, in the game: the Mods page turns a mod
  on or off; the order is `mods.json`'s, and a mod's options are the file
  under `mod_config/` until the page grows a row for them.
* The mod UI library the other recompilations offer (recompui), which lets
  a mod draw its own menus. A mod that imports it will not load here yet;
  the loader names the missing import. Their data library, `recompdata.h`
  (the hashmaps, hashsets and slotmaps a mod keeps between calls), the port
  does provide, with the same names and meanings (`src/mod_data_api.cpp`);
  the template carries the header.
* A place to find mods. The other recompilations use Thunderstore; when
  there is something to list, this port will ask for a community there.

## Texture packs

RT64's texture replacement packs go in `texture_packs/`, an `.rtz` archive
or a folder with an `rt64.json`, and are read once at start-up
([the manual](MANUAL.md#mods-and-texture-packs)). No pack exists for this
game yet; the format is RT64's `TEXTURE-PACKS.md`.
