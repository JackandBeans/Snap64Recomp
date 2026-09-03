texture_packs/ -- HD texture packs for Snap64 Recomp

Nothing ships here. This folder exists so you know where a pack goes; the
port creates it empty on every start and scans it once, at start-up.

What a pack is
  RT64's replacement format: either a .rtz archive, or a folder that carries
  an rt64.json database next to its .dds or .png textures. The format is
  RT64's, documented in lib/rt64/TEXTURE-PACKS.md of the source tree
  (https://github.com/rt64/rt64). The tools that build a pack, texture_hasher
  and texture_packer, are in the same tree under src/tools/.

How packs load
  Every .rtz file and every folder with an rt64.json directly inside
  texture_packs/ is loaded in alphabetical order; where two packs replace the
  same texture, the later one wins. Loading happens once when the game
  starts and blocks until it is done, so there is no in-game toggle: the
  folder's contents are the switch. The log line "[SNAP-TEX] N texture
  pack(s) in texture_packs/: loaded" confirms what was found (snap64.log next
  to the executable, or the terminal you started from).

What this port does not do
  It bundles no pack, endorses none, and hosts no list of downloads. A pack
  made from another game's data is not something this project will help
  install: the port transforms the one cartridge you supply, and that is the
  whole of it.
