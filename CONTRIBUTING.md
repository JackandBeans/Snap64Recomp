# Contributing

Bug reports, pull requests and mods are all welcome. Most of the releases
so far were made of what players reported, so a report is the most useful
thing you can send: open an issue with the files the template asks for. A
mod needs nothing from me: [docs/MODS.md](docs/MODS.md) and the
[mod template](https://github.com/JackandBeans/Snap64RecompModTemplate)
are the way in. If you are not sure whether a change fits, ask under
[Discussions](https://github.com/JackandBeans/Snap64Recomp/discussions)
first; I would rather talk it over than turn a finished pull request away.

For code, these are the things that keep the port what it is:

* **The cartridge's behaviour is the default.** Anything that changes how
  the game looks, sounds or plays is opt-in and off until the player turns
  it on, and the README's tables get a row for it. A fix for something the
  console itself got wrong (see Cutscene Fix) is still opt-in.
* **No game data.** No ROM, no extracted assets, no save files with
  someone else's photos, in the repository or in an issue.
* **Patches are the game's own functions, changed.** `patches/src` holds
  copies of decompiled functions from the
  [Pokémon Snap decompilation](https://github.com/ethteck/pokemonsnap)
  with the port's changes; a new patch replaces a function by name and says
  in a comment what it changes and why (`patches/README.md`).
* **Comments say what is true**, not what was intended; a build that ships
  has its version bumped, its suite run and its archive checked
  (`BUILDING.md`, "To cut a release").
* **Say how a change was made.** I made this port with Claude Code
  (README, "How it was made"), and each commit made that way names the
  model in a `Co-Authored-By` trailer, except the first week's, before the
  trailer was in use. A contribution made with an AI tool is welcome on the
  same terms: name the tool in the trailer.

Build instructions are in `BUILDING.md`. Build with your change, run the
automated suite, `tools/release_check.py`, and say in the pull request what
it reported.
Everything is GPLv3, and a contribution is offered under the same terms.
