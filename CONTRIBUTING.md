# Contributing

Bug reports are the most useful thing: open an issue with the files the
template asks for. For code, the ground rules that shaped the port:

* **The cartridge's behaviour is the default.** Anything that changes how
  the game looks, sounds or plays is opt-in and off until the player turns
  it on, and the README's tables get a row for it. A fix for something the
  console itself got wrong (see Cutscene Fix) is still opt-in.
* **No game data, ever.** No ROM, no extracted assets, no save files with
  someone else's photos, in the repository or in an issue.
* **Patches are the game's own functions, changed.** `patches/src` holds
  copies of decompiled functions from the
  [Pokémon Snap decompilation](https://github.com/ethteck/pokemonsnap)
  with the port's changes; a new patch replaces a function by name and says
  in a comment what it changes and why (`patches/README.md`).
* **Comments say what is true**, not what was intended; a build that ships
  has its version bumped, its suite run and its archive checked
  (`BUILDING.md`, "To cut a release").
* **Say how a change was made.** This port was written with Claude
  (README, "How it was made"), and every commit that was names the model
  in a `Co-Authored-By` trailer; a contribution made with an AI tool is
  welcome on the same terms: say so in the trailer, and read what it
  wrote before you send it.

Build instructions are in `BUILDING.md`; the automated suite is
`tools/release_check.py`, and a pull request should say what it reported.
Everything is GPLv3, and a contribution is offered under the same terms.
