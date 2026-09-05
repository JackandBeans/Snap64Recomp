# Release checklist for 1.0.0

Compiled 2026-09-04 from a six-lens audit of the repository plus the
automated suite's results on the rc2 build (18/18, station 5/5). Internal:
this file, like OVERNIGHT-REPORT-2026-09-03.md, is a working note kept under
`docs/dev/` (item 2.14), with `docs/dev/README.md` saying what it is.

**Status on 2026-09-05, before the push: verified.** The executable in the
archive (`Snap64Recomp.exe`, SHA-256 beginning ccf4cb28; the archive's
begins 4cffe0a9) went through the suite as the exact bytes that ship,
without SNAP_STATS in the environment and on a cold shader cache: 22 of 22
in 785 s (subsystem, stdio, attract, stats, score, settings, menu, package),
then `--only station` 5 of 5 in 489 s (the print relaunch at 400 s, 16 of
16 slots, the sheet, the final boot). The same code, one comment apart,
had passed identically the same day (22 of 22 in 782 s, station 5 of 5 in
489 s). The commits after this run touch `docs/dev/` only, which the
archive does not carry, so the archive and its checksum stand.

Legend: **decide** = only the author can settle it; **do** = mechanical, can
be done on request; **verify** = needs a person or another machine.

## 0. Status on 2026-09-04, after the release work

Done, on the 1.0.0 build: 1.1 (licenses/roboto.txt, NOTICE Assets table),
1.2 (/d1trimfile, the map filtered, the archive checked for user paths),
1.3 (README says GitHub Issues of the repository, which needs no URL),
1.4 (the runtime is linked in; the archive check refuses VCRUNTIME/MSVCP
imports), 1.5 (1.0.0 everywhere), 2.2, 2.4 (graphics_api + a caught
start), 2.5 (mutex), 2.6 (Manual kept), 2.8 (unmapped pads logged and
documented), 2.9 (README paragraph, version resource fields), 2.10
(CHANGELOG.md), 2.13 (all seven), 2.14 (docs/dev), 3.1, 3.2, 3.3, 3.4,
3.7, 3.8, and 1.6 as far as the machine can go (main fast-forwarded,
v1.0.0 tagged; the push is yours).

Decided and left as they are, with the reasoning: 2.1 the decomp-derived
patch sources stay -- every N64Recomp project ships modified copies of
decompiled functions as its patches, NOTICE.md now says so plainly, and
the game's data is what a takedown turns on, none of which is here; 2.3
the wordmark stays -- original art, a stylistic homage, with the
disclaimer in the README's first paragraph and the game's own copyright
line on the same screen; 2.7 the anti-aliased present filter stays the
default and is disclosed as the third non-console default, since raw
nearest pixels look wrong at every non-integer window size; 2.12 the
commit trailers stay (the author set that rule; the README's Status does
not dwell on it) and the author's email is theirs to change, not the
port's: PUBLISHING.md explains GitHub's private address; 2.15 the unnamed
2D content is a documented known limitation, not touched days before
release because pairing identity is where the earlier tearing bugs lived;
3.5 and 3.6 stay as documented gaps.

Added later on 2026-09-04, after the first tag and before any push, with
the tag moved to the new head: the executable's icon (`src/snap64.ico`
from `tools/icon_gen.py`, compiled in through `src/snap64.rc.in`; the
suite's `subsystem` check now looks for the icon resource), `.github/` (a
bug-report form and a pull-request template), `CONTRIBUTING.md`,
`.gitattributes` (the vendored trees marked for GitHub's language
statistics), `docs/dev/social-preview.png`, and repairs to README.md,
NOTICE.md and BUILDING.md, where the docs script's second run had
duplicated a 20-line block, the Assets table (three times over) and one
sentence. The suite on that build: 21/21 in 782 s, station 5/5 in 492 s,
the icon check passing in a separate `--only subsystem` run because the
full run had started before it was added.

Identity settled on 2026-09-04: author JackandBeans <snap64recomp@gmail.com>,
history rewritten with filter-repo before any push; README carries a byline,
a Thanks section and a copyright line; the logo is docs/logo.png.

Disclosure settled on 2026-09-04 (2.12 closed): README's "How it was
made" says the port was written by Claude models in Claude Code under the
author's direction, and how the commit trailers name the model;
CONTRIBUTING.md asks the same of contributors. The same day a second
filter-repo pass scrubbed the author's Windows username from 274
historical versions of five files (the recompiler's `.toml` inputs,
`patches/README.md`, this file and PUBLISHING.md), dropped the two unused
ELF dumps the first commit carried, and pruned the empty baseline root
commit; the tree at the tip did not change. The suite's three replays
are now tracked under `tools/replays/`, so the suite can be run from a
checkout.

Yours: 1.7 the clean-machine run, 2.11 screenshots, the push and the
release page (docs/dev/PUBLISHING.md walks through them), and the
decision on the email before the first push.

## 1. Things that would embarrass the release (must)

1.1 **Roboto attribution (do).** The printer's lettering bitmaps derive from
Roboto Regular (Apache 2.0). NOTICE.md does not mention it and the archive
carries no Apache licence text. Add `licenses/roboto.txt` (licence + notice),
add it to the CMake licence list, add a NOTICE.md row.

1.2 **Personal paths in the archive (do).** The linker map ships in the zip
and the executable embeds `C:\Users\<name>\...` through `__FILE__`. Stop
installing the `.map`, and add `/d1trimfile:<repo>\` (MSVC) so asserts and
logs carry relative paths. Rebuild and re-check with `strings`.

1.3 **Nowhere to report a bug (decide, then do).** README says "include
snap64.log in a bug report" but names no destination; there is no remote.
Needs the public repository URL, an Issues link, and a one-line contact.

1.4 **Visual C++ runtime (decide, then do).** The executable links the MSVC
runtime dynamically; a clean machine without the redistributable fails with a
missing DLL. Either link it statically (`CMAKE_MSVC_RUNTIME_LIBRARY
MultiThreaded`, rebuild, rerun the suite) or name the redistributable and its
link under "What you need".

1.5 **Version strings (do, last).** The version is generated for the binary,
but README.md and BUILDING.md spell "1.0.0-rc2" in prose in eight places.
Bump `project(VERSION 1.0.0)` and clear `SNAP_VERSION_PRERELEASE`, then grep
both documents for `rc2`/`rc1` and fix every one.

1.6 **The branch you publish (decide).** `main` is an empty baseline; all 306
commits are on `snap-port`. Fast-forward `main` to it or make `snap-port` the
default branch, then tag the exact commit the archive is built from
(`git tag -a v1.0.0`). Never push the backup folder.

1.7 **Clean-machine smoke test (verify).** Unpack the archive on a Windows PC
that has never built the port, with a ROM: does it launch, find its folder,
play, print. This one run settles 1.4, SmartScreen (2.9) and the driver
question (2.4).

## 2. Things a reader or player would notice (should)

2.1 **Decompiled game code as readable source (decide).** `patches/src/render_patch.c`
reproduces two of the game's functions line for line from the decompilation,
which carries no licence. Compiled patches are one thing; readable
reconstructed game logic in a public repository is what has drawn takedowns
elsewhere. Options: keep it and accept the exposure, or ship only the diff
against the decompilation (a patch file applied at build time) so the
game's code never sits in this repository.

2.2 **Trademark notice (do).** README names Nintendo, HAL and The Pokémon
Company; the game's own line names Nintendo, Creatures, GAME FREAK and HAL.
Name all five.

2.3 **Title badge (decide).** The "Recomp" wordmark is original art in the
Pokémon logo's style (bubble letters, yellow fill, dark outline). It reads as
official-adjacent. Keep with the disclaimer, or restyle.

2.4 **No graphics API choice, no failure path (do).** Always D3D12, RT64's
config file disabled, an uncaught exception when D3D12 setup fails. Add a
`graphics_api` setting (Vulkan already ships) and a caught, worded failure.

2.5 **Two copies at once (do).** No single-instance guard: a second launch
stalls 20 s on the log, then both write the same save. Add a named mutex.

2.6 **Frame Rate "Manual" collapses (do).** Editing any other Graphics row
while fps_mode is 2 silently saves it back as Display. Give the row a third
state or preserve the value through the mailbox round trip.

2.7 **Crisp scaling is on by default (decide).** `present_filter` defaults to
RT64's anti-aliased scaling, an enhancement the README's list of non-console
defaults omits. Default to nearest, or list it as a disclosed exception.

2.8 **Controllers (do).** Only the first SDL-recognised pad is opened and no
`gamecontrollerdb.txt` ships. Bundle the mappings file or document
`SDL_GAMECONTROLLERCONFIG`, and log pads seen but unmapped.

2.9 **Unsigned executable (do: a paragraph).** SmartScreen will say unknown
publisher; the Print feature relaunches the process twice, which some
antivirus heuristics dislike. Say so in Running, with the "More info > Run
anyway" route. Fill CompanyName and LegalCopyright in the version resource.

2.10 **CHANGELOG / release notes (do).** None exist. A short public "What is
in 1.0.0" plus a five-line feature list and the explicit sentence "you must
supply your own US cartridge dump; nothing of the game is included".

2.11 **Release page material (verify: you).** Two or three screenshots
(title, a course, the Graphics page), and mention the `.sha256` CPack writes.

2.12 **Contact, takedown, authorship (decide).** No maintainer contact or
policy for a rights-holder objection; 259 of 306 commit messages carry
`Co-Authored-By: Claude` trailers, and every commit publishes the author's
real email. Decide each on purpose: leave the trailers and say in Status how
the port was written, or rewrite them; keep the email or use a noreply one
(rewriting history is a one-time job before the first push, never after).

2.13 **Documentation drift (do).** README's Graphics rows are listed in
mailbox order, not screen order; BUILDING.md still claims the overlay table
header names `gen_overlays2.py`; `settings.h` still describes the station's
old five-second attach; the settings table lacks `snap_station`; Esc is
listed under a function that does not handle it; NOTICE.md names the
reproduced function as `renRenderModelTypeACommon` when the code defines
`renPrepareModelMatrix` and `ren_func_80013C5C`; BUILDING.md mentions an
`rc1` verification without dating it.

2.14 **Internal notes in `docs/` (do).** OVERNIGHT-REPORT-2026-09-03.md and
this file are working notes written to the author. Move them out (or under
`docs/dev/` with a heading saying what they are) before publishing.

2.15 **UI interpolation gap (decide whether to fix).** The statistics run
pairs 98-100% of named rectangles, but some 2D content is drawn without a
name and steps at the game's rate when it moves: the photo panels (290x220,
280x210), Oak's thumbnails (104x78), and full-screen 320x240 backgrounds
during transitions. That is the "UI still not interpolated" feeling. Fixing
it means naming those drawers the way the sprite and window libraries were
named (src/rect_tags.cpp); otherwise list it under known limitations.

## 3. Polish (nice)

3.1 `src/rt64_render_context.cpp`: a TODO stub for resolution scale, and eight
comments with a double-encoded dash.
3.2 A `## Credits` section gathering what is scattered: the decompilation
project, James Chambers (jamchamb) for the 2021 Snap Station research, the
RT64 and N64Recomp authors, SDL, Roboto, JackandBeans.
3.3 State a Windows floor (Windows 10 64-bit; the DPI call needs 1703).
3.4 One sentence telling players to copy `saves/` before updating or
printing; the `.bak` is one generation deep.
3.5 Keyboard bindings are fixed scancodes; say so, or add remapping.
3.6 `interpolate_camera` has no Graphics row (F4 or JSON only).
3.7 The package check should also require the two folder READMEs, the
nlohmann-json licence and the `.sha256` sidecar.
3.8 Known limitations paragraph: the printer's font is a stand-in for one
that no source records; pass timings are estimates; unnamed 2D content steps.

## 4. Already settled (for the record)

Suite 18/18 and station 5/5 on the rc2 build; no ROM or game asset tracked
or packaged; `.gitignore` covers saves, ROM, generated trees; `LICENSE` is
GPLv3; NOTICE.md documents about forty components; no scratch or replay
files tracked; the only tracked image is the port's own wordmark.
