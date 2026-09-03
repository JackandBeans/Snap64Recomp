# Overnight report, 2 to 3 September 2026

What was built, decided, verified and left out while you slept, and the
checklist to validate the whole game when you wake up. Everything below is
committed on `snap-port`; nothing was pushed, and the backup folder was not
touched.

## 0. The commits, in order

| Commit | What |
| --- | --- |
| `8d1c027`, `f7fa4d5` | (before you slept) a fresh clone builds: the vendored pins recovered, `tools/fetch_deps.py`, the v1.7.2308 `dxil.dll` |
| `4972025` | a windowed program that keeps its log; version rc2; `tools/release_check.py` |
| `ec73c7d` | the Snap Station on port 4, with the two port bugs it exposed fixed |
| `87859cc` | mods and texture packs: the loaders disclosed and anchored |
| `94580a0` | the credits face gets the digits 2, 3 and 7 so the rc2 title line opens the menu |
| `6c541e1` | the menu check drives eval.inputs to the title |
| (last) | this report |

Nothing was pushed. The PokemonSnapRecomp-backup folder beside the
repository was not touched. Your save and settings in `build-win/Release`
are the ones from before the night's test runs (the station runs rewrite
the save; the backups were put back). The `stickers/` folder there holds
the sheets the test prints made; delete them or keep them as you like.

## 1. What was built

### 1.1 The release decisions (part 1)

* **A windowed program.** The executable links with the Windows subsystem,
  so a shortcut launch opens no console beside the game. Where the log goes
  is decided before the first line: an inherited pipe or file is kept (the
  headless rigs and `> out.log` read exactly as before), a parent console is
  attached (typing the name into cmd still prints there), and otherwise
  `snap64.log` is written beside the executable with the previous run kept
  as `snap64.prev.log`. `src/main.cpp`, `snap_bind_stdio()`.
* **Version 1.0.0-rc2**, not 1.0.0. The rc1 archive on disk predated the
  validator change and the windowed build. 1.0.0 is one edit in
  `CMakeLists.txt` (`SNAP_VERSION_PRERELEASE` to empty) once your playtest
  passes; I did not judge a build ready for the final number before the six
  unplayed courses have been played.
* **The headless suite in one script**: `tools/release_check.py`. Subsystem,
  the three log paths, the Beach replay under player conditions with captured
  frames that must be real pictures, the diagnostic replay's pacing and
  coherence numbers, the recorded run to Oak's evaluation with the scorer's
  healthy signature and the photo export, the settings file, and the archive.
  About eleven minutes; it opens the game window for each run.
* The stale `%LOCALAPPDATA%\pokemonsnap` cache folder was deleted (only
  shader caches from before the move beside the executable).

### 1.2 The Snap Station (part 2)

The Blockbuster kiosk's printer, emulated on controller port 4 so that the
game's own code runs the whole print. `src/snap_station.{h,cpp}`, off by
default, `"snap_station": true` in `snapsettings.json`.

* **What the game does** (all of it read in the decompilation and matched
  line for line to James Chambers' 2021 reverse engineering): every retail
  cartridge probes each port that reports a pak by writing a block of FE
  and then a block of 85 to Controller Pak address 0x8000; a device that
  does not echo the FE but does echo the 85 is the printer. With one
  present, the Gallery grows a Print row above Save. Print saves the four
  photos of the print tray to the cartridge between message bytes CC and 33
  (every cartridge save is bracketed that way while a station is present),
  then sends 5A and waits for the console to be reset. Booted with the
  station present, the game runs a memory test over the Expansion Pak range
  and then its photo display mode: a 640x480 screen showing sixteen slots
  from a table in the ROM (each of the four photos a 2x2 block of a 4x4
  sheet), sending 01 before the first, 02 after each, 04 after the last;
  the kiosk's printer captured the video output at each 02.
* **What the port does**: answers the probe, the message register and the
  busy byte; relaunches itself for the reset (no soft reset exists in the
  runtime; a marker file beside the executable carries the pending job and
  the old process id, and the new process waits for the old one before it
  opens the save or the caches); captures on each 02 both the VI
  framebuffer the game is scanning out (the digital source of what the
  printer saw) and the renderer's presented frame at the player's
  resolution, holding the game with the busy byte until both are on disk;
  lays them out after 04 as `stickers/<date>/sheet.png` (2560x1920) and
  `sheet_presented.png`, with the singles beside them; then relaunches once
  more into a normal boot, as the kiosk reset the console a second time.
* **Two port bugs the kiosk exposed**, both fixed: the port's audio-backlog
  word sat inside the range the kiosk firmware memory-tests, so the test
  could never pass (moved into the port's mailbox page); and the photo
  display runs the RSP on the L3DEX2 2.08H line microcode, which RT64's
  database did not know, so the renderer crashed on the first slot (the
  microcode and its hashes are added to RT64's table, with a diagnostic
  that makes the next unknown microcode a one-run job).
* **Verified**, section 3.

### 1.3 Mods, texture packs and the one-ROM rule (part 3)

The research (four readers, an opus decision, an opus verification) found
that the port already carries more loader plumbing than Zelda64Recomp shipped
with on its day one: librecomp's `.nrm` mod loader runs at every start, and
RT64's texture replacement scans a `texture_packs/` folder at every start.
The gap was disclosure and one bug, so that is what was built:

* **The texture-pack folder is anchored on the executable and created
  empty** (`src/rt64_render_context.cpp`). It was the one path in the port
  that resolved against the working directory, so a shortcut with a
  different "Start in" found no pack and said nothing.
* **Two first-party notes travel in the archive**: `mods/README.md` and
  `texture_packs/README.txt`, installed into their folders, saying what each
  loader accepts, that nothing ships, and the one-ROM rule.
* **README and NOTICE say it plainly**: a "Mods and texture packs" section,
  corrected folder rows, and one NOTICE sentence that no pack or mod ships
  and that content from another game's data is outside what the project
  hosts, links or helps install.
* **`SNAP_DEV=1`** turns on RT64's developer mode for one launch, which is
  the only way to dump the texture hashes a pack is keyed on. Documented
  with its cost: RT64 takes F1 to F4 while it is on.

Rejected, with the reasons in section 2: bundling any existing pack, a
curated download list, an in-game mods menu, a Thunderstore community, a
first-party demo mod, autosave, and replacing the game's Pokémon models
with Pokémon Stadium's.

## 2. What was decided, and why

* **Windowed build with a log file** rather than a console: a public build
  that opens a black console beside the game reads as unfinished, and the
  log survives in a file that a bug report can attach. The headless rigs
  were the constraint, and they keep working because an inherited pipe or
  file is kept.
* **rc2, not 1.0.0.** The final number should follow your playtest of the
  six courses, not precede it. One edit flips it.
* **The Snap Station stays off by default** and lives in `snapsettings.json`
  tonight. The kiosk was a store fixture; a cartridge at home never showed
  the Print button, so off is the faithful default. I did not add a menu
  row: the Graphics page's field bank is full (sixteen of sixteen), and the
  game's own Option list, the right home for a peripheral toggle, has its
  rows at a sixteen-pixel pitch from y=89 to y=153 with the help box at
  y=171, so a seventh row has no vertical room without re-pitching every
  row, which is a layout change only your eye can judge. The row is the
  first follow-up (section 4).
* **The reset is a relaunch of the executable.** The runtime has no soft
  reset (no PRENMI, `osContReset` asserts), and the game's own boot with the
  station present wipes all of Expansion RAM twice before the display mode,
  so nothing a soft reset would carry across can reach the feature. The
  marker file and the save file are the only state, and the new process
  waits for the old one to exit before it opens either.
* **Two captures per slot.** The kiosk's printer took the analog video
  signal; the port keeps the VI framebuffer the game is scanning out
  (640x480 RGBA16, exactly the digital source of that signal) and the
  renderer's presented frame at the player's resolution, and lays each set
  out as a 4x4 sheet in the game's own slot order.
* **No Pokémon Stadium models.** Three independent grounds. Legal: the
  port's whole defence is that it transforms the one cartridge the player
  supplies and ships nothing of Nintendo's; assets lifted from a second
  Nintendo ROM would be exactly the thing the project promises never to
  host. Faithfulness: Snap's models were authored to be photographed at
  240p with this game's lighting and animation; swapping them is a
  different game. Engineering: RT64's replacement system replaces textures
  keyed by TMEM hash and nothing else; there is no model replacement path,
  and building one is a project, not a feature. What a player could
  legitimately do instead is a mod of their own making in the runtime's
  `.nrm` format, or an RT64 texture pack they authored, both of which the
  loaders now accept and the README now describes.
* **Research on smaller models.** The readers ran on Sonnet, the synthesis
  and the three adversarial verifiers on Opus, and the implementation, the
  runs and the judgement calls here were mine. The verifiers earned their
  keep: they caught an inverted reading of which sticker carries the game's
  rights line, a misattribution hazard in the capture path, and thread
  placement that would have stalled the game during encoding.

## 3. What was verified, and how

* **Part 1**: `tools/release_check.py` against the rc2 build and archive,
  all 17 checks passed in 651 s (PE subsystem, the three log paths through
  a pipe, a console-less launch and a relaunch, the Beach replay under
  player conditions with four captured frames all real pictures, the
  diagnostic replay's 60 pacing reports and 1110 coherence lines with no
  hold failure, Oak's evaluation with the scorer's signature on every photo
  and 55 photos exported, the settings file, the archive's contents and the
  v1.7.2308 validator).
* **Part 2**: the route to the Print button was built step by step from
  captured frames (a synthesised replay: a course, the Camera Check's Album
  Mark, Oak's check, the lab's Save, the title menu's Gallery entry, four
  rows down, Print). The first process logs the save's CC and 33, the 5A
  and its own relaunch; the second logs the pending job, the probe answered
  with 85, the display start, sixteen captured slots and both sheets; the
  third boots normally. The framebuffer sheet's four 2x2 blocks each differ
  from their twin by 0.0 and from the other photos by 28 to 44 luminance
  units, no slot is dark, and the rights line appears on the fourth slot
  only, as the decompilation says it must. The same flow is repeatable as
  `python tools/release_check.py build-win/Release --only station`
  (about ten minutes; it puts the save back afterwards).
* **Part 3**: the texture-pack loader found a minimal pack beside the
  executable when the game was started from a different working directory
  (`[SNAP-TEX] 1 texture pack(s) in texture_packs/: loaded`), and created
  the folder when none existed. `SNAP_DEV=1` reaches the renderer's
  configuration; RT64's developer panel itself is interactive and was not
  driven headless.
* **The archive** was rebuilt after everything above and carries the
  station, the two folder notes and the licences; the suite was run against
  it once more (its result is the last line of this section). A fresh clone
  of the final code commit was fetched with `tools/fetch_deps.py` in 177 s,
  configured in 43 s and built in 259 s with no errors, then deleted.
* **The last run**: `tools/release_check.py` against the final build and
  the rc2 archive, now with the menu check, **18 checks, 0 failed, 781 s**
  (04:40 on 3 September): the windowed subsystem, the three log paths, the
  Beach replay's four real captured frames, the diagnostic replay's 60
  pacing and 1110 coherence lines, Oak's evaluation scoring every photo
  and 55 exported, the settings file, the menu staging its 90 interface
  strings, and the archive with the v1.7.2308 validator.

### 3.1 A regression the suite let through, and the guard added

The rc2 version bump (`4972025`) silently broke the in-game Options page,
and two suite runs passed over it because nothing opened the menu. The
title's version line is one of the strings the port composites for its
Graphics and Sound pages, set in the credits face harvested from the game's
copyright block; that block supplies the digits of 1995-1999, and the port
synthesised 0 and 4 but not 2, so "rc2" asked for a glyph the face did not
have. One missing glyph withholds the whole staged directory, and the page
fell back to the stock five-item menu with no Graphics or Sound entry. It
surfaced only because I went to add the Snap Station menu row and found the
page blank. The fix adds 2, 3 and 7 to the credits synth (`94580a0`), and
`tools/release_check.py` gained a `menu` check that fails if a boot to the
title does not log that the interface strings staged. The lesson is the one
this project already learned once: a green suite that never exercises the
feature is not evidence.

## 4. What was left out, and why

* **A Snap Station menu row.** *Done later the same day, on the title
  screen rather than in Options: a fifth title entry, "Snap Station", in
  the title's own lettering, shown with Gallery; see the README's section
  and the commit that followed this report.* The night's account: I did
  build a seventeenth Graphics row
  (the mailbox field bank grown to +0x18, the page's scratch arrays widened,
  `station_set_enabled` wired to the new byte): it compiled and ran, but
  while verifying it I found the credits-digit bug above, which had blanked
  the menu for every row. Once that was fixed I reverted the seventeenth
  row rather than ship a mailbox relayout I had not yet verified renders,
  and the sixteen-row page is confirmed working. The row is a clean
  follow-up now that the menu opens: either finish that seventeenth Graphics
  row (verify it draws and that toggling it makes port 4 appear), or take
  the design's preferred path, a seventh entry on the game's own Option
  list, which needs the list's rows re-pitched. The setting stays in
  `snapsettings.json` meanwhile, and the station works from there.
* **Anything from another game's data.** See section 2.
* **A soft reset inside the process.** Not in the runtime; the relaunch is
  faithful in effect and documented.
* **The physical sheet.** Sticker size, sheet margins and the printer's
  colour processing are not established by any public source; the files are
  the game's pixels in the game's layout, and the README says so.
* **Linux and macOS.** No platform but Windows has ever been built; the
  station's relaunch is Windows-only and would need a POSIX spawn.

## 5. Playtest checklist

Everything a machine could check has been checked; this is the rest. Play
from the release archive `build-win/Snap64Recomp-1.0.0-rc2-win64.zip`
unpacked into a fresh folder with your ROM beside the executable, so the
archive itself is what you validate. Your own `build-win/Release` folder
keeps your save and settings; the archive starts clean.

### 5.1 Launch and files (five minutes)

1. Double-click `Snap64Recomp.exe` from Explorer. Expect: no console
   window; the game window titled `Snap64 Recomp 1.0.0-rc2`; a `snap64.log`
   beside the executable with the `[SNAP]` header lines. Quit with Esc and
   start again: `snap64.prev.log` now holds the first run's log.
2. Start it from a terminal (`.\Snap64Recomp.exe` in PowerShell). Expect the
   log lines in that terminal and no new `snap64.log`.
3. Confirm the folders exist beside the executable after the first start:
   `cache/`, `mods/` (with `README.md`), `texture_packs/` (with
   `README.txt`), `saves/` after the first save.

### 5.2 The game, course by course (the long part)

For each course, one full run and Oak's evaluation. Watch for anything that
is not the cartridge: a pop, a flash, a missing Pokémon, a hitch that is not
the game's own update-x3-draw-x1 hold at cart crossings, wrong scoring.

4. Beach (already played on this build's predecessor; play once more).
5. Tunnel, 6. Volcano, 7. River, 8. Cave (Jynx lives here: check the
   cartridge's black Jynx with the Graphics row off, then the purple one with
   it on), 9. Valley, 10. Rainbow Cloud (needs all the signs).
11. Between courses, return to the lab and use Save at least twice; quit and
    relaunch and confirm the save came back (album count, report score,
    items).

### 5.3 The Graphics and Sound pages (ten minutes)

12. Options from the title: the list must show **Graphics** and **Sound**
    entries (if it shows only Screen, Sound, Z Button, Control Stick, Return,
    the menu font failed to stage, which the `menu` suite check now guards
    against). Every Graphics row cycles and its description reads correctly
    (rows 15 Photo Detail and 16 Jynx Recolour are the newest; the Jynx text
    names both positions). B cancels back to the values the page opened with;
    A keeps them. Sound page: sliders apply live, Stereo and Mono switch.
13. Photo Detail: with it on, the photos Oak holds up in the evaluation are
    sharp at your window's resolution; with it off, they are the cartridge's
    pixelated ones. The NEW badge and the photo frame must look the same
    either way.

### 5.4 Photos and the Snap Station (fifteen minutes)

14. P (or the controller's Back button) on any photo screen writes a PNG
    into `photos/` and the log says `photo saved`.
15. Snap Station. Quit the game; open `snapsettings.json` and set
    `"snap_station": true`; start the game. Play one course and, in the
    Camera Check, mark at least one photo with the Album Mark (the button
    is disabled on a brand-new save until the first course is done; the
    lab enables it). Let Oak check the rest so the print tray fills. Back in
    the lab, Save, and answer No to "keep going" so the game returns to the
    title; with more than three species in the saved report the title menu
    now carries a Gallery entry. Open it. Expect the row list to read
    PKMN Report, PKMN Album, Arrange, Enlarge, a divider, **Print**, Save,
    and the Print row's help text to mention a print credit.
16. Press Print. Expect "Now Saving...", the window closing, and a new window
    opening straight into a 640x480 photo display that shows sixteen
    stickers one after another (four photos, each four times, white card,
    the rights line on the fourth only). It takes about a minute. Then the
    window closes again and the game boots normally to the title.
17. Look in `stickers/<date>/`: `sheet.png` (2560x1920, the framebuffer
    sheet), `sheet_presented.png` (the renderer's frames), and the singles.
    The sheet must read four 2x2 blocks of the same photo each.
18. Set `"snap_station"` back to `false` and confirm the Print row is gone
    from the Gallery.

### 5.5 The one thing only you can decide

19. If every course, the save, the pages and the station pass, change
    `SNAP_VERSION_PRERELEASE` in `CMakeLists.txt` to an empty string,
    rebuild, run `python tools/release_check.py build-win/Release --zip
    build-win/Snap64Recomp-1.0.0-win64.zip` after `cpack -C Release`, and
    that archive is 1.0.0.
