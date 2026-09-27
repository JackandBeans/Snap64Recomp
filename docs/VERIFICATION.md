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
  a Deck through Proton; no Linux desktop has reported yet. Since 1.1.0 a
  macOS build is made and run on GitHub's virtual Mac on every release
  candidate (`BUILDING.md`, step 15): there it boots to the intro, plays the
  Beach with its photos scored and loads a mod, natively and under Rosetta
  2; no physical Mac has run it. A report from any machine not on that list
  is welcome, good or bad.
* **Buildable from a clean checkout, in two steps beyond `git clone`.**
  `python tools/fetch_deps.py` checks out the vendored trees, git submodules
  at recorded commits (RT64, the runtime, plume and N64Recomp on Snap64
  Recomp's forks), and verifies them; the recompiled game and the recompiler's inputs
  (`RecompiledFuncs/`, `RecompiledPatches/`, the ROM) are generated under WSL
  from your own cartridge dump. A second checkout built this way, on this
  machine, on 2026-09-02 (`BUILDING.md`, "What a clean checkout is missing").
* No installer. The Windows and Linux archives are the ZIP and tarball that
  `cpack` writes here (`BUILDING.md`, steps 13 and 14), after the headless
  suite in `tools/release_check.py` has passed on them ("What has been
  verified"). GitHub's machines build too: the Windows and Linux archives on
  every push and pull request (`.github/workflows/build.yml`, kept as
  artifacts), the macOS bundle on request (`macos.yml`), and the
  documentation's links on every push. From the release after 1.1.0 the
  published archives are GitHub's: `release.yml` builds all three and signs
  each with GitHub's build provenance, and the suite runs on those files
  here before they are published (`BUILDING.md`, "To cut a release").
* Version `1.1.0`, typed once in `CMakeLists.txt` and shown in the title
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
  Since 1.0.9 the key that opens the pages from anywhere is pressed in the
  title tape: the list opens over the title and closes again, the runner's
  own log line and the display list's say so, and nothing hangs or overruns.
  Since the fix that followed 1.0.9 the same run proves the music held with
  the screen: the host's line for each BGM player gives its own clock at
  the hold and at the release, and the check passes only when they are
  equal.
  On the 1.1.0 executable (SHA-256 beginning `3006d9b4`), run without
  diagnostics in the environment, the suite passed 29 of 29 checks in 1149
  seconds, and the station's 5 of 5 in 487; the 1.0.9 executable had passed
  28 of 28 in 1154 and the station's 5 in 488, the 1.0.8 executable had passed
  the 26 checks of its day in 1103 and the station's 5 in 488, 1.0.7 the earlier 25 of 25 in 825
  and 487, 1.0.6 the 22 of 22 before that in 781 and 487, 1.0.5 the same in 781 and 489, 1.0.4
  in 781 and 487, 1.0.3, 1.0.2 and 1.0.1 in 781 and 491 each, and 1.0.0 in
  781 and 489 on a cold shader cache. The suite opens the game window for
  each run and takes about nineteen minutes, plus eight for the station.
  GitHub's machines record two kinds of run. The build workflow's
  (BUILDING.md, step 14) configures, compiles and packages the tree and runs
  no game: on the 1.1.0 tree (run 36301759264, 2026-09-27) it made the
  Windows archive with Visual Studio 2022 in ten minutes and the Linux one
  with Clang on Ubuntu 24.04 in six, each with the same file list as the
  release's, and the Windows one passed the suite's package checks. The
  macOS workflow's, on GitHub's virtual Mac (step 15), passed 15 of 15
  checks natively and 10 of 10 through Rosetta 2 on the 1.1.0 candidate.
  The release workflow's first runs (2026-09-27, on the 1.1.0 tree with
  the workflow added): run 36342595034, labelled `dryrun1`, made and signed
  all three archives in ten minutes (Windows ten, Linux two, the Mac's
  build three and a half and its checks five more). On its first attempt
  the Mac's mod check through Rosetta 2 crashed (signal 11, just after the
  mod's first hook ran) where it had passed on the 1.1.0 candidate; the
  re-run of that job passed, and the cause is not known. Run 36344484310,
  labelled `rc1`, passed every job at the first attempt. In both, the
  package checks passed on all three archives and each signature verified,
  in the workflow and again on this machine with `gh attestation verify`.
  The release suite then ran on the Windows executables GitHub built with
  Visual Studio 2022, where every release so far was built here with 2019:
  on the `dryrun1` one, 25 of 29 checks in 1151 seconds, with 45 photos
  scored, the 3x run's 45 identical and slow motion at 1.98; the four that
  failed were the Options page and the three page checks, withheld because
  that label has a `y` the credits face cannot draw (a rule the build now
  enforces). Those four passed on the `rc1` executable, 4 of 4 in 175
  seconds, and the Snap Station check on it 5 of 5 in 488. The Linux ARM64
  tarball and the Flatpak are built by the same workflow (run 36348338742,
  2026-09-27, labelled `rc2`, in which all five archives were made, checked
  and signed), and no machine has played the game from either. The ARM64
  tarball has only been built and packed: its package checks pass and its
  executable's ELF header says AArch64. The Flatpak was installed on
  GitHub's Ubuntu machine and started with no ROM and no window: it named
  itself, named its data folder in the sandbox, and exited when its ROM
  chooser was answered Cancel; the same bundle, installed under WSL's
  Ubuntu 24.04 with Flatpak 1.14 on the release machine, did the same and
  wrote its log to that folder.
  No other machine's build or run is recorded in this repository. Anything
  not listed here should be assumed untried.
* The photo export (P, the controller's Back button, `photos/`) is checked
  by `SNAP_PHOTO_AUTOEXPORT` on an input replay that reaches Oak's check,
  not by hand: saving from the keyboard and from the controller has not been
  tried.
* The recompiled game is generated from a specific decompilation build; the
  chain of tools and inputs is spelled out in [BUILDING.md](../BUILDING.md), including one
  stale input on my machine that must be regenerated before the
  recompiled code is.
