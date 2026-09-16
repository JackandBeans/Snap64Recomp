# What has been verified, and what has not

The rule of this project's documentation is that nothing is claimed before
it has been checked. This page is the record: what the port has been built
and run on, what the release suite puts each build through and what it
reported, and what has only been read from the code. Anything not listed
here should be assumed untried.

## Status


* Built on one Windows machine (Windows 11, MSVC 2019, an AMD Radeon RX
  9060 XT), and, since 1.0.1, as a Linux build under WSL with Clang that
  has been played on a Steam Deck in both of its modes
  ([STEAM-DECK.md](STEAM-DECK.md)). Players have run it on an NVIDIA
  GeForce RTX 4070 Ti Super (issue #13), an Intel UHD 630 (issue #15) and
  a Deck through Proton; no Linux desktop has reported yet. A report from
  any machine not on that list is welcome, good or bad.
* **Buildable from a clean checkout, in two steps beyond `git clone`.**
  `python tools/fetch_deps.py` fetches the vendored trees (SDL,
  DirectX-Headers, RT64's third-party trees) at the recorded upstream commits
  and verifies them; the recompiled game and the recompiler's inputs
  (`RecompiledFuncs/`, `RecompiledPatches/`, the ROM) are generated under WSL
  from your own cartridge dump. A second checkout built this way, on this
  machine, on 2026-09-02 (`BUILDING.md`, "What a clean checkout is missing").
* No CI and no installer. The release archive is the ZIP that `cpack`
  writes (`BUILDING.md`, step 13), after the headless suite in
  `tools/release_check.py` has passed on it ("What has been verified").
* Version `1.0.8`, typed once in `CMakeLists.txt` and shown in the title
  bar, the log banner, the credits line, the executable's file properties and
  the ZIP's name. `CHANGELOG.md` says what each release changed.
* Licensed under the GPLv3 (`LICENSE`); `NOTICE.md` lists every third-party
  component. No file in the tree carries the game's bytes: the menu font is
  cut from the game's own sprites in memory at run time, and the audio
  microcode is recompiled from the builder's ROM at build time (`NOTICE.md`,
  "Material derived from the game").

## Verified, and not


* Verified in the sense that I have play-tested the entire game
  on the one machine above: every course from the Beach to Rainbow Cloud,
  and every course again with Widescreen on (the Beach also frame by
  frame, for the edge fault 1.0.1 fixed), Oak's evaluations, the report,
  the album, the Gallery and a Snap Station print, with the port's own
  screens and hotkeys along the way; and the
  renderer, audio, saving, the Graphics and Sound pages and the hotkeys
  listed here all come from the code as it stands
  (`src/settings.cpp`, `src/settings.h`, `src/input.cpp`, `src/main.cpp`).
* `tools/release_check.py` is the headless suite a release build is put
  through: the executable is windowed, the log reaches a pipe or `snap64.log`,
  the Beach replay runs under the player's own conditions with captured
  frames that are real pictures, the diagnostic replay produces its usual
  pacing and coherence numbers with no crash report, the recorded run to
  Oak's evaluation scores every photo with the scorer's healthy signature and
  exports the photos it shows, the Options screen's Graphics and Sound rows
  stage from the harvested font with no character missing, the settings file
  is valid, a first start with no ROM copies the chosen dump in (byte-swapped
  or not) and a Cancel starts nothing, and the archive carries everything it
  must; `--only station` puts the Snap Station print
  through both relaunches and checks the sheets, and a first start with no
  ROM is put through the chooser's three cases, and the scoring replay is
  run again at three times the console's speed and the Beach replay at
  half, the same photos and the same reading reached in the expected time.
  On the 1.0.8 executable (SHA-256 beginning `9963a410`), run without
  diagnostics in the environment, the suite passed 26 of 26 checks in 1103
  seconds, and the station's 5 of 5 in 488; the 1.0.7 executable had
  passed the earlier 25 of 25 in 825 and the station's 5 in 487, 1.0.6 the
  22 of 22 before that in 781 and 487, 1.0.5 the same in 781 and 489, 1.0.4
  in 781 and 487, 1.0.3, 1.0.2 and 1.0.1 in 781 and 491 each, and 1.0.0 in
  781 and 489 on a cold shader cache. The suite opens the game window for
  each run and takes about nineteen minutes, plus eight for the station. There is no CI run, and no
  build on any other machine is recorded in this repository. Anything not
  listed here should be assumed untried.
* The photo export (P, the controller's Back button, `photos/`) is checked
  by `SNAP_PHOTO_AUTOEXPORT` on an input replay that reaches Oak's check,
  not by hand: saving from the keyboard and from the controller has not been
  tried.
* The recompiled game is generated from a specific decompilation build; the
  chain of tools and inputs is spelled out in [BUILDING.md](../BUILDING.md), including one
  stale input on my machine that must be regenerated before the
  recompiled code is.
