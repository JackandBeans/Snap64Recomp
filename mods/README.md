# mods/

Nothing ships here. The runtime this port is built on (N64ModernRuntime's
librecomp) scans this folder at start-up for `.nrm` mod containers, the same
format the other N64 recompilation projects use, and loads them: a mod found
for the first time is enabled unless its own manifest says otherwise, and
`mods.json` beside this folder, in the executable's folder, lists the enabled
ones and is the way to turn one off (`mod_config/` holds each mod's own
settings file). A mod must target the game id `pokemonsnap`. Writing one
starts from the template and the symbol files named in `docs/MODS.md`.

A mod downloaded as a zip (Thunderstore's Manual Download) can go here as it
is: at the next start the port takes the `.nrm` out of it and renames the
zip `.zip.installed`. Or drop the `.nrm` or the zip on the game window; a box
says what was installed, and it loads at the next start.

The game's Options screen has a Mods page (Esc, or Select on a pad, opens the
Options anywhere): it lists the mods in this folder, turns each one on or off,
orders them, shows each one's details and options, and installs new ones from a
file picker, and `mods.json` records it; a change takes effect at the next
start. No mod is bundled and none is endorsed. Mods run inside the
recompiled game with the game's own memory and the port's patches, so a mod can
change anything; the port's promise of console behaviour by default holds only
for a `mods/` folder that is empty.

Content for the vendored runtime and renderer is the player's own and under its
own terms. Assets taken from another game's data are not something this project
will host, link, or help install.
