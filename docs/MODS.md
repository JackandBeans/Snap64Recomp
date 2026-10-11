# Mods

Snap64 Recomp loads mods the way the other N64 recompilations do: an `.nrm`
file in the `mods/` folder next to the executable, built with N64Recomp's
mod tool from code that hooks, follows or replaces the game's own functions.
The loader has been in the port since 1.0.0; what this page adds is the kit
for writing one, and what the port does and does not do with mods today.

A mod is N64 code only. The mod format has two ways to carry native code,
a declared native library (a `.dll`, `.so` or `.dylib`) and an
offline-recompiled `.offline.nrm` with its library beside it; the port
refuses both and does not take either out of a zip. Every mod goes through
the live recompiler. A mod is still code the game runs: it can change
anything in the game's memory, and nothing makes it a security sandbox,
here or in any recompilation. Install mods from people you trust, as
anywhere, and prefer ones whose source is published.

## Installing a mod

Drop the mod on the game window: the `.nrm` itself, or the `.zip` it came
in. A zip is how Thunderstore packages a mod; take Thunderstore's Manual
Download, as for the other recompilations, since no mod manager is needed.
The port takes every mod and texture pack out of the zip, wherever in it
they sit, checks that each is a mod for this game that this release can
run, and puts it in `mods/` (a texture pack in `texture_packs/`). A box
says what was installed, and why anything was not.

Or choose it from the game: Options > Mods > **Install Mods...** opens a
file picker, and what you pick is installed the same way.

Or put the `.nrm`, or the zip as it is, in `mods/` beside
`Snap64Recomp.exe` (or the Linux binary). A zip there is unpacked when the
game next starts, and renamed `.zip.installed` so it is not unpacked twice.

A mod loads when the game starts, so one installed during play waits for
a restart: Options > Mods > Restart Game. The Mods page lists it at
once, with the value NEW, and a mod that replaces one already there says
on its help line which version waits for that restart. A mod file this
release will not load is listed with the value Error and the reason.

If the game did not close normally last time and mods are on, the next
start asks whether to turn the mods off, so a mod that stops the
game can be turned off from outside it.

A mod is enabled the first time it is found unless its own manifest says
otherwise. The game's Options screen has a **Mods** row (it stands where
the stock Return row was; B returns), and Esc or a pad's Select opens the
same screen anywhere: it lists every mod in the folder, six to a screen,
with On or Off beside each, and Left or Right turns the selected one on or
off. The change is written to `mods.json` at once and takes effect the next
time the game starts; until then the page's last row reads "Restart the
game to apply". `mods.json` beside the folder is the same list in a file.
Each mod's own settings go in `mod_config/`. The log names every mod it
installs, opens and loads, and says why one did not: a mod built for
another game, a version older than the mod asks for, a missing dependency.

No mod ships with the port and none is endorsed. A mod runs inside the
recompiled game, with the game's memory and the port's patches, and can
change anything; the port's promise of console behavior by default holds
for an empty `mods/` folder. Content made from another game's data is not
something this project hosts, links or helps install.

## Writing a mod

Start from the template, which is a working mod. The kit is two
repositories of their own, public since 2026-09-20 and checked against the
released 1.0.9, which is the oldest release a mod built from them runs on:

* [Snap64RecompModTemplate](https://github.com/JackandBeans/Snap64RecompModTemplate):
  an example hook behind an option, the MIPS build (clang and lld, with
  the decompilation's headers), the manifest, and the modding headers.
* [Snap64RecompSyms](https://github.com/JackandBeans/Snap64RecompSyms):
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
between calls. The port's own `snap64.h` gives a mod the computer's clock
and calendar (`snap64_local_time`, `snap64_local_date`), which the
cartridge never had, the clock's zone (`snap64_utc_offset`, minutes east
of UTC), a line of its own on its details page
(`snap64_set_status`: the mod's id and the line; a `%T` in it is filled
with the clock as the page is read, so a time stays right while the page
is up; an empty line takes it away), and a notice over a course
(`snap64_notice`: the mod's id and a short line, centered at the top of
the picture in the film counter's row for four seconds once the HUD is
up, in the help face over a dark panel, fading in and out; a new one
replaces the old, and none outlives its scene). A mod that
imports them asks for
1.1.3 in `minimum_recomp_version`. A `thumb.png` or `thumb.dds` at the `.nrm`'s root is the
picture on the mod's details page, as in the other recompilations: list it
in `additional_files` in `mod.toml`. It is fitted into a square and drawn
from 256 pixels across, so 256 square is the size to make. A DDS may be
uncompressed RGBA or BGRA, or BC1, BC2, BC3 or BC7.

An option's `hidden_from` and `disabled_from` work on the Mods page: an
option another one hides leaves the list while that option has one of the
values named, and one it disables is gray, keeps its value and says why.
Upstream's runtime parsed these rules and then dropped them; the port's
copy keeps them (VENDORING.md).

A hook works on any function the symbol files name. Before 1.1.0 three
kinds failed: a hook on a function that calls the game's `memcpy`, on one
the port's own patches replace, and on one that waits a frame (the game
stopped when that process ended). A mod with such a hook should ask for
1.1.0 in `minimum_recomp_version`. The template also packs a mod for
Thunderstore: `tools/pack_thunderstore.py` makes the zip from `mod.toml`
and the page, changelog and icon in its `thunderstore/` folder.

A hook on a function the port itself intercepts -- `fx_draw`, `omGetMtx`,
`fx_createParticle`, `renPrepareCameraMatrix` and the rest of the list in
`tools/hook_funcs.py` -- runs from the port's own copy of that function
since 1.1.3, with the port's work in it kept. Before 1.1.3 such a hook
replaced the function with a plain copy of the cartridge's, and a hook on
`fx_draw` made every 2D effect step as the view turned. A `RECOMP_PATCH` of
one of those functions replaces the port's copy too, and its work goes with
it.

A matrix group of the renderer's extension that asks for interpolation (the
form the port's own patches use for the game's objects), written into the
display list while the sky is being drawn, stops the renderer's frames while
the game runs on; the port's log shows nothing. Until that is fixed, a mod
drawing with the sky should use the plain pushed group with no interpolation
(an id of zero and a second word of one), as the port's storm bolt does. The
see-through list, where the game's water and effects are, takes the
interpolating form.

## What the port does not have yet

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
or a folder with an `rt64.json`, and are read once at start-up. An `.rtz`,
or a zip carrying one, dropped on the window is put there
([the manual](MANUAL.md#mods-and-texture-packs)). No pack exists for this
game yet; the format is RT64's `TEXTURE-PACKS.md`.
