# mods/

Nothing ships here. The runtime this port is built on (N64ModernRuntime's
librecomp) scans this folder at start-up for `.nrm` mod containers, the same
format the other N64 recompilation projects use, and loads the ones enabled in
`mod_config/mods.json` next to it. A mod must target the game id
`pokemonsnap`.

There is no in-game mod manager in this release: what is enabled is decided by
`mods.json` (written by the runtime when it first scans the folder) or by a
mod's own manifest. No mod is bundled and none is endorsed. Mods run inside the
recompiled game with the game's own memory and the port's patches, so a mod can
change anything; the port's promise of console behaviour by default holds only
for a `mods/` folder that is empty.

Content for the vendored runtime and renderer is the player's own and under its
own terms. Assets taken from another game's data are not something this project
will host, link, or help install.
