# Changelog

## 1.1.1 -- 2026-10-03

* RT64, N64ModernRuntime, plume and N64Recomp are git submodules on Snap64
  Recomp's own forks, with the port's changes as commits on each fork's
  `snap64` branch, where they had been tracked copies with their changes
  written out as patch files; SDL and DirectX-Headers are submodules on
  their upstreams. A clone gets every tree through
  `python tools/fetch_deps.py`, which checks all 33 out as their
  repositories store them on every machine. Applying each patch to its
  upstream commit reproduced the tracked trees exactly, and turned up one
  change no patch had recorded: N64Recomp's `recomp.h`, where relocated
  addresses are summed through a volatile so MSVC keeps their 16-bit wrap.
  The changes are not offered upstream: those projects do not accept
  contributions made with an LLM.
* GitHub's machines build the Windows and Linux archives on every push and
  pull request (`.github/workflows/build.yml`), and a pull request from a
  branch of this repository gets them linked in a comment.
* A release's archives are made on GitHub's machines by
  `.github/workflows/release.yml` and signed with GitHub's build
  provenance, so `gh attestation verify <file> --repo
  JackandBeans/Snap64Recomp` shows which commit and workflow made a
  download; until now every archive was built on the one machine that cut
  the release. The release suite runs on those files before they are
  published; its first run on a GitHub-built Windows executable (Visual
  Studio 2022, where the releases so far were built with 2019) passed every
  check (`docs/VERIFICATION.md`).
* The Linux and Mac executables no longer carry the build machine's
  folders. The Linux one named its source files by their full paths on the
  machine that built it, a home folder under WSL (`/home/<user>/...`; seen
  in 1.0.9 and 1.1.0, and nothing in the build ever trimmed them), because
  only the Windows build did (`/d1trimfile`). The 1.1.0 Mac one carried
  GitHub's runner folder, in its source names and in the linker's debug
  map, which named every object file it linked. Clang and GCC now map the
  source and build folders away (`-ffile-prefix-map`), Apple's linker takes
  the build folder off the object names (`-oso_prefix`), and the package
  check, which looked only at the Windows zip and only for a Windows user
  folder, now searches every archive for any build machine's folders.
* Linux comes three ways: the x86_64 tarball as before, a tarball for ARM64
  (a Raspberry Pi 5, an ARM laptop, a Linux virtual machine on a Mac), and
  a Flatpak for x86_64 that brings its own SDL2, GTK 3 and Vulkan loader
  and installs with one command on a Steam Deck or any desktop with
  Flatpak (`linux/flatpak/`; it keeps its files under
  `~/.var/app/io.github.jackandbeans.Snap64Recomp/`). Both are made on
  GitHub's machines by the build workflow, the ARM64 one on GitHub's ARM
  runners; the Flatpak is installed and started there on every run. No
  machine has played the game from either yet (`docs/STEAM-DECK.md`).
* The build workflows' private inputs name the tree they were made from
  (`inputs.json`, written by `tools/ci_inputs.py`, which was
  `tools/macos_inputs.py`), and every build stops when they belong to
  another tree; before, a build could have carried a different tree's
  patches without a word.
* Control Stick: Reverse, set from the pause menu's Options, did nothing for
  the rest of the ride (issue #19, myleskeller, an Xbox 360 pad). The
  Controls page wrote the game's own setting into the save at once, but a
  ride reads that setting once, as it starts, into a copy of its own
  (`IsAxisYInverted`), so the change waited for the next ride; Z Button
  (Hold/Switch) did the same. The manual said both took effect the moment
  they were changed. They do now: a change over a ride's pause is put into
  the ride's copies as well, and Z Button's HUD hint for Switch, the Z
  beside the camera while zoomed in, which the ride makes only when it
  starts in Switch, is made or removed with it. The row's help line also
  had the two settings backwards; on the cartridge, Normal tilts the camera
  down when the stick is pushed up and Reverse tilts it up, and the line
  says so now. Checked on the Beach replay: the same push of the stick
  tilts the view down with Normal and, changed to Reverse over the pause,
  up for the rest of the ride; Z Button changed to Switch mid-ride zoomed
  on one tap, kept the zoom after the release with the hint shown, and
  zoomed out on the next; changed back to Hold, it zoomed only while held,
  with no hint.
* The photographer card, where a new game's name is entered, takes the
  keyboard. Its letters are picked from a grid with the stick and A, and
  the port's keys were only the game's buttons there (W, A, S and D the
  stick, X the A Button), so a name could not be typed. A key typed on the
  card now enters its letter as if the cursor had been moved onto it and A
  pressed: the capitals and small letters, the digits, the card's own
  punctuation and the e acute, Space, Backspace (a letter back) and Enter
  (End); a character the card lacks is passed over, and a full card goes to
  End as it does. The arrow keys, which are the D-pad, move on the grid
  (the cartridge's card listens to the stick only). While the card is up
  only the keys that type are its own: Esc and the port's own keys keep
  their jobs, and after the card the keyboard stays away from the game
  until every key is up, so the Enter that ended it is not a Start.
  Checked with scripted typing on the card: "T y'é1{" gave "T y'é1" (the
  brace passed over), "ABCDEFGH" with an x and a Backspace gave "ABCDEF"
  (the card full after seven), and the D-pad stepped the cursor from A to
  B to G.
* The question New Game asks over a saved game ("If you save a new game,
  all previous data will be deleted") did not take the mouse: its Yes and
  No were the stick's, where the title's list beside it had taken the
  pointer since 1.0.4. The word under the pointer is selected as the
  pointer moves onto it, a click on a word chooses it, a click anywhere
  else does nothing, and the right button is No, as B is. Checked with the
  scripted pointer: hovering selected Yes and then No, a click on the box's
  red did nothing, a right click closed the question, and a click on Yes
  while No was selected chose Yes.
* The question asked when the last session did not close normally and mods
  are on cut its second button's label off on Windows ("Start with mods
  off" showed as "Start with mods"): SDL's Windows message box gives every
  button the same 88 pixels whatever its label says. The button says "Turn
  mods off" now, and the question "Turn the mods off?". The question itself
  was asked after a session that had closed normally: the release suite's
  Snap Station check ends the relaunched game from outside, as it must,
  and left the session mark that the game's own exit would have removed,
  so the next start of a build the suite had run on asked it. The check
  removes the mark it leaves.
* The game's own questions take the mouse: Oak's "Shall I repeat this
  information?", the lab's "Shall I save your progress?" and "Should I
  take this course?", the photo check's "Should I show this to Prof.
  Oak?", the album's "Delete this picture?", the Gallery's "How's this?",
  and every other text with an A line and a B line. A click used to be A
  wherever the pointer was, so the only answer a mouse could give was the
  first, and Oak repeated himself until B was pressed on something else.
  While such a text is on screen, the A or B icon under the pointer is
  underlined in orange, with the move sound as the pointer comes onto it,
  as the Options pages' A and B are; a click on an icon presses its
  button (A, B, or Z where a text offers one), a click anywhere else,
  the words beside the icons included, does nothing, and the right
  button is B. The icons are the game's own button
  images, one object each, made as the text is printed, so a question is
  known from them wherever it appears, and the answer is laid into the
  frame's input record, which every screen reads. Checked on Oak's repeat
  question (the pointer on an icon underlined it and on the words beside
  it did not, a click on the words did nothing, a click on the B icon
  answered No) and on the save question, whose two answers share one line.
* Oak's photo check, showing one picture at a time, did not take the mouse:
  the header's arrows turned the pages of the grid of pictures but did
  nothing on a single picture, where the stick's left and right step
  through a Pokémon's pictures, so a mouse alone could not reach the second
  picture of a Pokémon. The arrows step there now, as the wheel does; a
  click on the picture is A on it; the right button is B. The view runs
  the stick's steps on its own, so the port's hooks on the grid never ran
  there (src/menu_mouse.cpp); the view's own picture lookup, made every
  frame, is hooked now. Checked on the eval replay: the arrows and the
  wheel showed DODUO's second picture and the first again, the click chose
  the picture, and the right button left for the panel.
* The mouse pointer on the menus is the port's own: an arrow carrying a
  camera lens, JackandBeans's pictures at pointer size, as the system's
  cursor (so it moves at the mouse's own rate, where a pointer drawn by
  the game would trail a frame behind at the menus' thirty frames a
  second). Its size follows the game's picture: 46 by 52 in a 1280 by
  960 window, half that in a window under 720 lines, and never past 64
  pixels on screen (at 92 by 104 the picture under it glitched a little
  as the shutter changed, JackandBeans's report, the likely cause being
  that a cursor too large for the display's hardware cursor is composed
  in software). It is drawn as pixel art of 23 by 26, each pixel a block
  of two screen pixels in that window, so it sits with the game's own
  sprites; outside the lens the four frames are one picture, so only
  the shutter moves. The lens has a shutter,
  four frames from open to closed (`menu_text/pointer.png`, beside the
  executable): a click closes it and opens it again, seven tenths of a
  second in all. When it shows and
  hides is unchanged: hidden while a course runs and the mouse aims, shown
  on the menus, and in fullscreen while it moves or clicks (it hid three
  seconds after the last move even under a clicking hand, JackandBeans's
  report; a click counts as a move now; and it went at every fade between
  two menus, since a fade drops the mouse for half a second, and flashed
  back in on the next screen: it now rides through a drop of up to a
  second and a half, and goes for a cutscene or the intro), and it goes
  out in a flash:
  JackandBeans's comic burst over the whole pointer in the last four
  tenths of a second before the hide (`menu_text/pointer_flash.png`), the
  pointer gone in it, and it comes back the same way in reverse, the
  burst first and the pointer out of it. A turn of the wheel keeps it
  shown as a move or a click does, and a course takes the mouse only
  after the flash out, so a ride resumed from the pause menu no longer
  snatches the pointer away. The Controls page's new Pointer
  row picks Camera, the port's own, or System, the system's arrow
  (`custom_pointer` in `snapsettings.json` is the same switch); a
  missing `pointer.png` leaves the system's.
* The mouse turned the camera of the title's attract demo, the course the
  title plays from a recorded script after a while idle (JackandBeans).
  The port's mouse look writes the view's angles straight into the game,
  which the demo's script never reads back, so the picture followed the
  hand while the demo rode on. On the cartridge the stick does nothing to
  the demo and only A or Start ends it (`updateIdle`, app_level/player.c).
  The mouse and the gyro now leave the demo's camera alone, the demo does
  not take the mouse, and a click, which is A, ends it as the cartridge
  does.
* The side panel's buttons (the lab, the course list, the PKMN Report, the
  album, the Gallery, the photo check) turned crisp and moved half a pixel
  the instant a choice was made, before the screen faded. The buttons slide
  in by their scale, and the cartridge's slide-in stops one step short of
  setting it to one, so each button rests a little under its height (97.8%
  and 99.4% for the album's two); a choice sets the scale to exactly one
  before the buttons slide out. At 320 by 240 the two look the same. The
  port's renderer draws scaled 2D smoothly at the output resolution and
  one-to-one 2D on the native grid, so the buttons were soft at rest and
  jumped at the choice. The slide-in now ends at exactly one
  (`patches/src/layout_patch.c`, the game's `UILayout_UpdateButtons` with
  that one change), so the buttons rest as they were drawn after a choice.
  Found in playtesting, fullscreen, with and without interpolation; checked
  on the album from a real save at 2560 by 1440: the buttons do not move or
  change from rest through the choice, and match, pixel for pixel, how the
  choice used to draw them.

## 1.1.0 -- 2026-09-27

* The mod kit is public. The template and the symbol files went up on
  2026-09-20 as Snap64RecompModTemplate and Snap64RecompSyms, after a review
  against the released 1.0.9 that started from a clean clone and found four
  faults. The example's manifest asked for 1.0.8, which has neither
  `recomp_printf` nor the collections, so the mod would have failed there on
  a missing import; it asks for 1.0.9. The mod tool calls `zip` on Linux and
  says only that it failed to run it; the template's README lists it, with
  the tool's build from N64Recomp's source at the port's own commit. And the
  port's manual, twice, and the note it ships in `mods/` still said there is
  no in-game mod manager, while the manual put `mods.json` in `mod_config/`;
  the released executable writes it beside itself, and the documents say so
  now, on `main` too. Checked against the released executable: the example
  from a clean clone, turned on at first sight; a second mod using the game's
  headers from the unbuilt decompilation, a game variable by name, a hook
  reading a return value and a replaced function; the replacement of a
  function the port itself replaces, which the runtime refuses by name and
  `RECOMP_FORCE_PATCH` forces; and a manifest asking for 1.1.0, refused with
  a message. Both symbol files regenerate byte for byte from the
  decompilation at `3a236dc`, and the template builds and packs itself on
  every push, its workflow building RecompModTool from N64Recomp `ffb39cd`.
  Not checked: the Windows and macOS tool lines, the published RecompModTool
  binary, native libraries, and dependencies between mods.

* The README argued where it should have stated. A reader on Reddit said the
  page read as defensive and confrontational, and quoted "I am one person,
  with no team behind it" from it; they were right, and I said so there. The
  section on how the port was made listed everything I do, told the reader
  what to judge the port by, and ended on "None of this asks to be taken on
  trust": answers to objections nobody on the page had raised, which is why
  a reader who had raised none felt accused. It is three sentences under a
  heading of their own now -- I made the port with Claude Code; Claude wrote
  the code, the tools and the documentation in sessions I directed, and
  every such commit names the model in its trailer; my part is deciding,
  playing and releasing -- so every fact of the disclosure stands and only
  the argument is gone. The same pass restated the sentence on the
  executable being compiled from the game's translated code without its
  emphasis, took the vow out of the mods bullet and the hedge out of What's
  next, and replaced "the ground rules" in the README, the manual and
  CONTRIBUTING.md with a Contributing section and an opening that say
  reports, pull requests and mods are welcome. CONTRIBUTING.md no longer
  tells a contributor who used an AI tool to read what it wrote; every pull
  request is asked for the same thing, what the suite reported. A second
  pass made the page plain to read, for a reader of any age. Sentences of
  forty to sixty words were split, lists took the place of sentences that
  carried lists, the pad's buttons became a table, and the words a newcomer
  meets first (static recompilation, ROM, frame interpolation) are explained
  where they first appear. Measured before and after with a Flesch-Kincaid
  script: the sections ran from grade 9 to 18 and now run from 4 to 8, and
  of thirty-five sentences over thirty-five words one is left, the contents
  line. Three claims were brought back to what has been seen: the key was
  said to open the pages over the Report, which has not been photographed;
  it was said to work on any screen, where the manual names the few on which
  it does nothing; and I was said to have played every build on a Steam
  Deck, which 1.0.9 has not been. The paragraph on how the port was made now
  sets my part and Claude's side by side, one sentence each. On `main` too.

* The front page follows the layout players know from the other
  recompilations, and speaks of the port, not of "my port". Measured on
  2026-09-21 against the seven most starred N64 recompilation repositories,
  this README was two to four times their length, and the only one with no
  download link in its first screen, no System Requirements section and no
  FAQ. It now opens with what the port is, the download, and the line that
  no game assets are included, and names the Releases page as the only
  official download. It has System requirements (the processor's SSE4.1 and
  the renderer's Shader Model 6.0 stated; the oldest graphics cards quoted
  from Zelda 64: Recompiled, which uses the same renderer, because none has
  been measured here), Features, an FAQ of nine questions, and Known issues.
  The long explanations moved, unchanged in substance, to
  `docs/KNOWN-ISSUES.md` and to a new section of the manual, Faithful by
  default; the old section names still work as links. The project is the
  subject of its own front page now. The first person is kept where a person
  is speaking: the disclosure under How it was made, the thanks, and the
  sentence on whose cartridge the code was translated from. On `main` too.

* The small icon, the film canister the window's title bar and the Linux
  window show, did not match the logo's canister: a smear of the burst's
  yellow beside its cap, and a flat dark bar down its left where the right
  has a black outline and a label that shades in from it. The icon generator
  cuts the canister out of the logo, where the burst's shaded,
  half-transparent edge hugs the cap and the filmstrip's dark end covers the
  canister's left edge. Two fixes earlier the same day mirrored the right
  side onto the left, the second one paint and all, which took the 64 off
  the label. Now the cut treats the burst's soft edge as ground, so the
  smear is gone on both sides; the outline's left takes the right side's
  shape; and the right side's colors come across up to the 64 and fade into
  the drawn pixels over the next columns, with the ink, the 64's strokes and
  the Poke Ball, keeping its own color (`tools/icon_gen.py`). The .ico, the
  window icon and the macOS icon are regenerated; the canister's size in
  each is unchanged.

* With the Options pages opened over a screen from anywhere, the music
  played on. The pages freeze the screen's objects and processes, and the
  game's audio thread is none of them, so the music ran under the pages and
  a screen that moves to its music, the title's opening or the lab, came
  back out of step (seen in my own play on 2026-09-21). The two BGM players are
  muted first, by a volume event their sequence player takes on its next
  turn, and then held: while the pages are up the host skips the sequence
  players' handler (`manualfunc_8002E2F8`, libaudio's `__CSPVoiceHandler`,
  which the driver does not name), so no event of theirs is processed and
  their position stands; the sound player is untouched, so the pages' own
  sounds go on. When the pages close the hold is lifted in the tick the
  screen thaws, and the volume the driver believes in is posted, which the
  handler takes on its first turn back. Events posted while held wait in the
  queue, and a full queue drops one rather than failing, as the library is
  written. In a course the pages use the game's own pause, which leaves the
  music playing, as the cartridge does. The release suite's `pages` check
  reads the host's line for each player, its clock at the hold and at the
  release, and passes only when they are equal (`pages-music`; the suite is
  29 checks).

* A mod's hook on a game function that calls the game's `memcpy` stopped
  the game with "Failed to find function at 0x80037660" (found while
  writing Unlimited Film, which had to be built around it). The runtime
  finds a function by its address through the port's own function table,
  `src/recomp_overlays.inl`, and a hooked function is recompiled with every
  call looked up that way. `tools/gen_overlays.py` left 83 functions out of
  that table: everything whose name ends in `_recomp`, on the belief that
  all of them were the runtime's reimplementations. Some are the game's own
  code, renamed by the recompiler away from a C library name (`memcpy`,
  `sprintf`, `strlen`, `sqrtf`); the rest are libultra calls the runtime
  does reimplement (`osRecvMesg`, `osSendMesg`, `osCreateThread`). The game
  calls both kinds at those addresses, so all of them belong in the table.
  The tool keeps them now, and gains `--arrays-only`, which rewrites the
  function lists from the recompiler's output without the ELF (the one on
  this machine is stale, BUILDING.md step 2); the 83 rows are the only
  change, checked line by line. A test mod hooking `makePhoto` stopped the
  game at its first photo before, and after it ran the evaluation replay
  through Oak's evaluation with the hook called 16 times.
* Three faults found by DramaticShape while building a VR fork of the port
  (prismaticShape/Snap64RecompVR), fixed here his way, with thanks.
  * The game reads the audio queue's length straight from the hardware
    register, twice: once a tick, and once when it rebuilds its audio
    players. A fresh build from source read the first from a word nothing
    wrote, because the recompiler's config still pointed it at an old
    address and the released builds worked only through a hand edit of the
    generated code. The second read was never redirected at all, so the
    first rebuild would have crashed the game. Both reads now ask the audio
    queue at that moment (`tools/hook_funcs.py`), and no hand edit is left.
    This also ends a small fault of the old way: the port wrote the length
    once per drawn frame from the main thread, which stops while a dialog is
    open, so the game read a stale length meanwhile. Checked with a test mod
    that changes the reverb, which makes the game rebuild its players: the
    old build crashed at once (an access violation), the new one rebuilt
    them and played on.
  * A game thread that started while the game was quitting read the game's
    registration as `quit()` cleared it, and could throw on the way out. It
    reads it under the same lock now, and starts nothing once the game is
    gone.
  * Resizing the window, or switching to fullscreen, resized RT64's swap
    chain while its framebuffers still held the old images. Direct3D 12
    refuses that, so the first attempt failed and cost a frame. The
    framebuffers are released first now. Upstream RT64 has the same order.
* The Mods page answers what a player would run into. I tried the page and
  asked for everything a player could complain about; these are the fixes.
  * A held Left or Right turned a mod on and off again with the stick's
    repeat. Only a fresh press turns it now.
  * Restart Game was one A away, and the pages open over a course,
    which a restart loses. It asks first now, as Exit Game does: "Press A
    again to restart, B to stay".
  * A mod turned on or off said nothing about when that happens. Its help
    line now says "Turns on after a restart" (or off).
  * A mod installed during play was not listed until the next start. It is
    listed at once now, after the others, with the value New, and a mod's
    update says which version waits for the restart. A drop while the page
    is open refreshes it.
  * A mod file the game would not load was left out of the list without a
    word. It is listed now with the value Error and the reason (a mod for
    another game, or one that needs a newer release).
  * A mod's options had no way back to their defaults. Its options page
    ends with Restore Defaults now, which B undoes like any change there.
  * A mod that stopped the game at start could not be turned off from
    inside it. When the last session did not close normally and mods are
    on, the next start now asks whether to start with them off.
  * The help box's lines could run past its right side: 236 pixels were
    allowed where the box leaves 229. The limit is 226 now, measured on a
    capture.
  * The page with no mods says how to install one.

  Checked with replays in a scratch folder: a held Right turned a mod off
  once; the question came on the first A, B withdrew it, and nothing
  restarted; a mod dropped during play was listed as New and an update
  named its version; a mod for another game was listed as Error with its
  reason; Restore Defaults put an option back; the start-up question,
  answered through `SNAP_SAFE_START_ANSWER`, turned the mods off in
  `mods.json` before any loaded; every help line ended by x 268. The
  suite's page checks passed.
* A mod downloaded from Thunderstore installs. Thunderstore packages a mod
  as a zip (a manifest, an icon and a README beside the `.nrm`), and the
  other recompilations take that zip dropped on the window. Here a dropped
  zip was copied into `mods/` as it was, and the runtime loads only `.nrm`
  files, so the mod never loaded and nothing said why. Now a mod, a texture
  pack (`.rtz`), or a zip holding them, dropped on the window, is installed
  (`src/mod_installer.cpp`): every `.nrm`, `.rtz` and native library in a
  zip is taken, wherever in the zip it sits; each mod's manifest is read
  first, and a mod for another game or one that needs a newer release is
  refused with the reason. A box says what was installed and what was not.
  A mod loads when the game starts, and the runtime holds loaded mods open,
  so one installed during play waits for a restart (the Mods page's last
  row says so), and a file already in place is written beside it and
  swapped in at that start. A zip left in `mods/` is unpacked at the next
  start and renamed `.zip.installed`. Checked with six packages (the mod at
  the zip's root, in a folder inside it, no mod, a mod for another game,
  one asking for 9.9.9, a texture pack), at start and dropped during play
  (`SNAP_DROP_TEST`), and with the next start swapping in what the drop
  staged.
* Two more kinds of mod hook failed, found by testing hooks one at a time
  for the modding release. A hook on a function the port's own patches
  replace (`auPlaySound`, which carries the sound-effect volume, is one of
  29) did not load: "Code mod loading internal error". And a hook or a
  patch on any function that waits a frame stopped the game when its
  process ended, which in this game is most of them. Four causes:
  * The runtime recompiles a patched function live from the patch table,
    and the recompiler writes that table's section numbers in the symbol
    file's order (`.main` is 0). The port's own table numbers them its own
    way (`.main` is 2 for a call, 3 for an address), because the port
    builds the game from the ELF where the other recompilations use the
    symbol file for both. `tools/hook_funcs.py` renumbers them now.
  * The patches' unnamed static functions (99 of them) were not in the
    table, so the live copy could not find them. They are listed now.
  * The bytes the live copy is made from came from a link that leaves
    every call into the game as zero on purpose, for the recompiler to
    match. A game function handed over as a pointer (`omCreateProcess`)
    became address 0. The bytes now come from a second link with every
    game function defined (`tools/gen_patch_funcs_ld.py`); the Makefile
    checks that the two links lay out the same, and the 380 words that
    differ are exactly the 316 calls and 32 address pairs.
  * A thread ends by throwing an exception that unwinds its stack, and
    code made at run time has no unwind information, so the exception
    could not pass it and the game closed. The game parks every process in
    `ohWait` and ends it from outside, so this met every hook on a function
    that waits. Such a thread now returns to its start without unwinding
    (`ultramodern/src/threads.cpp`); nothing on the way needs cleaning up.
    Without code mods every thread ends as before.

  Checked with test mods and the scoring replay: hooks on `auPlaySound`,
  on the Beach intro's patched camera glide, on two patched main-menu
  functions, and on `ohWait` itself (75,000 calls, so every process that
  ended did so with the hook on its stack) all loaded and fired, and the
  replay scored its 45 photos. A hook on `dmaCopy` in the boot code, which
  had failed alongside them, loads too: it had only failed because another
  hook in the same test mod did. The log now names a live recompilation's
  failure instead of only "internal error".
* The Mods page worked unlike every other Options page, and its text did
  not fit. I reported it with two screenshots. The header had lost the
  A OK and B Cancel every other page shows; in their place stood "L R
  Order", which did not say what it did. The help box's lines ran past its
  frame. The causes: the page took the header's legend down to put its
  own hint there; the help lines were never measured against the box; and
  the fonts lacked letters a mod's text uses, so a description lost its
  semicolon and its "60", and the row font's g, taken from a taller font
  and cut to fit, read as an a. Now the header's legend says what A and B
  do on the page (A Details, B Back, in the legend's own icons; see the
  details page below), Left and Right turn a mod on or off, and a mod's
  options page keeps A OK and B Cancel, B putting its options back. The
  help box shows the mod's short description, and under it what A and L, R
  (move it up or down the load order) do on that row. Every line is
  measured and ends in "..." when it is too long, so no mod's text can run
  past the frame; on an options page the option's description has both
  lines. The last row reads "Restart Game to Apply" while a change
  waits for the next start. The help font gains the capitals, digits and
  punctuation it lacked; the row font gains ! ? ( ) " & and a g and y
  drawn a row higher. Checked with a replay in a scratch folder holding
  Unlimited Film and Unlock Everything: it turned Film off, moved Unlock
  up, changed Film's counter, and canceled each. The log said "1 change
  undone" and "2 changes undone", `mods.json` and Film's settings file
  were as before, and the captures showed each screen.
* The Options pages opened with Esc over another screen were hard to
  read. I reported it with a screenshot of the course select: its course
  names and scores read through the list. The screen behind was dimmed
  only as far as the pause menu dims a course (153 of 255). Now it is put
  down to a twenty-fifth of its brightness (245 of 255); at 230, tried
  first, the lab's "Go to Course" and "Save" could still be made out. A
  capture of the title with the pages open measured the brightest pixel
  behind them at 10 of 255, where it had been 255. Over a course the
  pages sat on the pause menu's own dim, the same 153; while they are up
  that dim is 245 too, and the pause menu gets its 153 back when they
  close. In the Beach replay with the pages open, the brightest pixel of
  the course went from 95 to 9. The title's own Option screen and the
  pause menu are unchanged.
* With the Options pages opened over the game's opening, the music and
  the picture stopped but the running water went on. I heard it and
  reported it. The pages hold the music, and left the sound effects
  alone so their own sounds could play; the opening's water is a looping
  sound effect. Now every effect playing when the pages open is turned
  down to nothing, and put back at its own volume when they close. The
  pages' own sounds still play. A new line in the log says how many
  effects were held and how loud the game was while they were: opened
  over the opening, it read 1 effect (the water), and the game's own
  sound fell to 15 of 32767 as the music's tail died away.
* A mouse click chose whatever a menu had highlighted, wherever the
  pointer was: Continue on the title, the Beach on the course list. I
  reported it. The left button was simply A. Now the menus take the
  mouse: the title's list, the lab's panel and the course list, the pause
  menu, every Options page, the album, the PKMN Report and its table,
  Oak's photo check, the Gallery and a new game's name card. Moving the
  pointer over an item or a photo selects
  it, a click chooses the item under it, and a click anywhere else does
  nothing. The right button is B and the wheel steps up and down. On an
  Options page a click on a setting's value steps it, the left half back
  and the right half forward, and the header's A OK and B Cancel and the
  scroll arrows can be clicked. The pointer's place comes from where the
  renderer drew the picture, so the window's size, widescreen and the
  overscan crop all count. While a menu has the mouse, its buttons stop
  reaching the game as A, B and the rest; everywhere else they are what
  they were, so a click still advances Oak's text, and outside a course
  the right button is B while it is bound to Z, so it answers a question's
  "No" too. The photo screens are caught in the game's own navigation
  functions (src/menu_mouse.cpp), which keeps their code as it is. The
  pause menu frees
  the cursor a course captures, and in fullscreen the pointer shows while
  it moves on a menu. Checked with a scripted mouse (`SNAP_MOUSE_TEST`)
  in a scratch folder: the title's Options chosen over the highlighted
  Continue, a click on empty space ignored, the lab's Go to Course chosen
  over the highlighted PKMN Album, Super Sampling stepped from 2x to 3x
  by a click on its value, Button Setup opened from its row, Quit Course
  pointed at without quitting, the pause menu's Options pill clicked, an
  album photo enlarged from a click, the Report's bar walked to Pidgey
  and its page opened, a photo of Oak's check chosen from a click, a
  Gallery button pointed at, and a name entered and End clicked. Not
  checked: the pick between two photos, the Gallery's print places, and
  the right button as B on a screen that does not take the mouse (a
  replay hands the game its own buttons, so it needs a hand on the
  mouse). I tried it and found the title's rows lit up with the pointer
  beside their words: a label's sprite is wider than its words (Snap
  Station's by 25 pixels on the left). Each row now answers to its words'
  own box, measured from the letters that change as the selected row
  pulses.
* A mod that another mod needs showed Off on the Mods page while the game
  loaded it. The runtime turns on every mod an enabled mod cannot do
  without, whatever that mod's own switch says, and the page only read the
  switch. Now such a mod shows On, its help line names the mod that needs
  it ("On: Test User needs it."), and Left or Right leaves it on, since its
  switch would change nothing. Checked with two test mods, one needing the
  other, and the needed one switched off in `mods.json`: the page showed
  it On with that line, and a click on its value left it On.
* The Mods page has a details page for every mod, a row to install mods,
  and the mouse. A on a mod, or a click on its name, opens the details (Z
  as well): the mod's name and version, who made it, its whole description
  a paragraph at a time, what it needs and what needs it, with the rest a
  scroll away (the next entry). A there opens the mod's options when it
  has any. Before, the page showed one line of a description and nothing
  of who made a mod. Since A opens them, B goes back keeping every
  change, each being written to `mods.json` as it is made, as in the
  other recompilations' mod menus; it used to put the visit's changes
  back, which with A no longer leaving the page would have undone a
  change on the way out. The header's legend says so: "A Details  B
  Back" on the list, "A Options  B Back" on the details of a mod with
  options, each in the game's own A and B icons beside words in the help
  box's face. A new first row under the mods, **Install Mods...**,
  opens a file picker for mods, texture packs and their zips, several at
  once, and installs them as a drop on the window does (the picker is
  shown by the window's thread, since a dialog on the game's thread would
  stop its clock long enough to be reported as a hang). With the mouse, a
  mod dragged up or down the list moves in the load order, as L and R move
  it. Checked with a scripted mouse: Unlimited Film's details opened and
  scrolled, its options opened from them, Test Base's details showed what
  needs it, Unlimited Film dragged down two places, and the Install row
  installed a test mod that the list then showed as New.
* The Mods page, a mod's details and its options were hard to read, and
  the Mods page did not look like the other Options pages. I asked for
  them to read at least as well as the other recompilations' mod menus,
  then held the Mods page against the Graphics page. It had a dark box
  behind its rows and help box, its values sat further right, its last
  rows were in lower case, and a mod's details said only "On" under its
  author. Now the Mods page and a mod's options page are laid out as
  Graphics is: no box, the values in the same column and all in orange
  but Error in red, and the rows under the mods named as the other pages
  name theirs (Install Mods..., Open Mods Folder, Restart Game). The list
  shows each mod's name alone; the version is on the details. The details
  page is read, not set, so it keeps a dark panel under its text. It
  starts with the facts: the name and version, "By" and who made it, and
  a sentence on its state ("This mod is on.", "On: Test User needs it."),
  one word of each in color, with the mod's picture beside them (the next
  entry). Under a rule comes the description, four lines at a time, with
  a scroll bar when there is more; a click on the bar turns a page. The
  help box says what A leads to ("A: this mod's option.", or "This mod
  has no options to set.") and how to see the rest. A mod's options page
  shows whose options they are: the mod's name, above a rule. The header
  font's l, cut from a taller letter, had a thin top; it is one even
  stroke now. Checked with a scripted mouse in a scratch folder, on the
  title and over a paused Beach: captures of the list, of three mods'
  details, of Unlimited Film's details before and after a click on the
  bar, and of its options. Then, asked to critique the pages again and
  again until nothing was left, I went around six more times. The help
  boxes spoke in button codes ("A: details. L, R: move it up or down."),
  where the Graphics page's help box speaks in sentences; they say "L and
  R move it up or down the load order." and "A opens this mod's option."
  now. The header said "A Details" on Install Mods..., Open Mods Folder
  and Restart Game too, where A carries the row out: it says "A OK  B
  Back" there. The By line had its label orange and its value white, the
  other way around from Graphics: "By" is white and the author orange. A
  mod that another mod keeps on showed "< On >" with arrows that did
  nothing: its On has no arrows now, like New and Error, and all three
  stand where the word stands between arrows. The same wait was worded
  "when the game restarts" in one line and "after a restart" in the next:
  it is "after a restart" everywhere. What needs a mod was said twice, in
  its status and again under its description; once now. A mod installed
  during play showed no author, only its one-line description, no picture
  and "This mod has no options to set." even when it had options: the
  installer keeps its authors, whole description, picture and option
  count from its file, and the line says "Its option can be set after a
  restart." A file that will not load said it had no options; it says
  "Remove it from the mods folder." Its name, the file's, lost its
  underscores, which the fonts lacked; both faces have one. And a mod
  with no author left a blank line under its name; its status takes that
  place. Unlimited Film's option is "Film Counter", in the Title Case of
  the Graphics page's rows. Each round was checked with a scripted mouse
  in a scratch folder, one run a round: the list on a mod and on Install
  Mods..., a mod turned on, a mod installed from the Install row, a mod
  file made for another game. Then I found a mod's details crowded: the
  name, the author and the picture sat low in the panel, 15 rows under its
  top and 2 over the rule, with the text 3 rows under that. The panel is
  spaced evenly now, measured on a capture: 5 rows above the picture, 5
  under it to the rule, 6 from the rule to the text, 6 under the last
  line; the three lines beside the picture stand 13 rows apart, centered
  on it, and a mod with no author has its two lines centered instead.
* A mod's details page shows the mod's picture, as the other
  recompilations' mod menus do. I asked for it in the space beside the
  name. It is `thumb.png` at the root of the mod's `.nrm`, which the
  runtime already read and nothing drew. The picture is fitted into 256
  texels square and drawn 40 pixels square, so it stays sharp at a high
  render scale, and dithered, since five bits a channel would band its
  shading. A `thumb.dds`, which the runtime reads first, is shown too
  (the next entry but one). Unlimited Film and Unlock Everything carry their
  Thunderstore icon as `thumb.png` from their 1.0.1 packages on. Checked
  with both in a scratch folder: each details page showed its picture
  whole, with no seam between the eight bands it is drawn in, and the log
  said "with its picture"; Test Base, which has none, kept its version at
  the text's right edge. The line the log prints when the Mods page closes
  now gives the strip pool too: how many strips it refused, and the most
  it found in use at a sweep (it sweeps only when all 64 slots have been
  handed out). Over a paused Beach with a picture up it had refused none,
  and its fullest sweep found 50 in use.
* A mod's options page handles every kind of option the runtime has, and
  the rules between them. I asked for a test mod with more than five
  options, since no mod had ever scrolled the page; the test mod has ten:
  enums, yes-or-no, numbers, a percent, a text, a name too long for its
  column, a value too long for its own, one option another disables and
  one another hides. Its run found six faults. The manifest's
  `hidden_from` and `disabled_from` never took effect: the runtime parsed
  each rule, checked it, and added it to a copy of the mod's schema that
  it then threw away (upstream's runtime has the same code; the port's
  copy adds them to the mod's own config, VENDORING.md). The page now
  applies them as values change: an option another one hides leaves the
  list and comes back when that option changes, and one it disables is
  gray, has no arrows, keeps its value under Left and Right, and says why
  in the help box ("Unavailable while Extra Film is Off."). The page read
  its row count once, when it opened; it follows the count now, and keeps
  the selection on the option that was changed. A name too long for its
  column was cut with no mark and never shown whole: it ends in "..." and
  the help box says it whole, first. A value too long for its column lost
  its closing arrow; it is cut inside the arrows now. A percent showed as
  a bare number; it has its sign, which the row and help fonts lacked. A
  text option, which the page cannot change, showed arrows; it has none.
  Checked with a scripted mouse in a scratch folder: nine of the ten
  shown with Ghost Mode Off, Film Rolls gray with its reason, Film Rolls
  back when Extra Film was turned on, Ghost Opacity in the list when Ghost
  Mode went to Faint, "< Sunset Orange... >" and "< 80% >" on screen, the
  page scrolled to its last rows, and B undoing both changes. Then I asked
  for the options page itself to be critiqued until nothing was left, and
  found it bland and tight beside the details page: the mod's name in the
  rows' own face read as the first option, the rows were packed under it
  at the top, and the rest was empty. A small caption, tried first, fixed
  the first and not the rest. Now the options page opens with the details
  page's own block on the details page's panel -- the picture, the name
  and version, By and the status, which says whether the options do
  anything yet -- and its rows sit under the rule where the details page
  has its text, three on screen, with the details page's scroll bar at
  the right in place of the list's chevrons (which sat on the panel's
  edge). A click on the bar steps the rows. A value cut to fit its column
  is said whole on the help box's second line ("Now set to Sunset Orange
  Glow."). Restore Defaults' help was a fragment, "Every option back to
  its default."; it is a sentence like the Graphics page's, and when
  every option is at its default already it says so, and A there answers
  as a press that does nothing does rather than as a change. Checked with
  a scripted mouse on the title and over a paused Beach: both mods'
  options, the test mod scrolled and stepped by a click on its bar, the
  Mods list's chevrons back after, Photo Frame's help, and the log's
  "defaults restored; 0 changes" then "1 change"; over the Beach the
  pages left 7300 of their 8192 bytes of stack and the strip pool
  refused none.
* The Mods page with no mods showed its three rows and nothing else: a
  new player saw no word that there were no mods, nor what to do. A note
  added first ("No mods yet.", in the help face over the rows) I found
  bland and tight, and a list of one or two mods tight too, and "New" did
  not stand out: it was the orange of On and Off. Now the page is two
  groups, the mods at the top and the three actions at the foot of the
  list under a rule, like a footer: while the list fits the window the
  actions stand at its last three places with room above them, and a
  longer list scrolls as one, the rule still over the first action,
  midway between the rows. With no mod at all the room above the actions
  says "No mods yet" in the rows' face and "Mods change or add to the
  game." under it, both centered: a headline with no period, and what a
  mod is, since the help box under the Install row already says how to
  add one (a first line, "Install one below, then restart the game.",
  said it twice). A mod installed during play says NEW, in capitals as a
  badge rather than one more value like On and Off, in the rainbow of the
  title's bottom line gliding through the word until its details are
  opened; then it is a plain orange NEW, since it still loads only after
  a restart. Checked with a scripted mouse: the empty page; Unlimited
  Film installed from it, its NEW in different
  colors in two captures 40 frames apart and orange once its details had
  been opened; the rule in a longer list's first window and in the window
  scrolled to its end, measured with three clear rows above it and three
  below. And I found a mod's web address cut on its details page
  ("Source: https://github.com/Jack..."): lines broke only at spaces, and
  an address has none. A word longer than a line is broken now where it
  fills the line, after a '/', '.' or '-' where it can be, and goes on on
  the next; Unlimited Film's source line reads whole over two lines.
* macOS, built and run on GitHub's virtual Mac, not yet on a physical
  one. The Apple Silicon code from pull request #2 (appleforever11) had
  been in the tree since 2026-09-16 with no build since; going through it
  against what Zelda64Recomp's macOS build carries, and against what the
  port added since, found three faults a Mac would have hit and one thing
  it lacked, and the runner's runs found three more:
  - SDL's Metal view resized its layer itself whenever the window changed,
    and plume's swap chain updates the size of the pictures it draws only
    when it finds the layer at another size than the window, so every
    resize and every switch to or from fullscreen would have drawn at the
    old size into pictures of the new one. `src/macos_support.mm` turns
    SDL's update into nothing, as Zelda64Recomp does.
  - plume's Metal backend could not copy a texture into a buffer, which the
    presented-frame capture and the Snap Station's sheet capture do: the copy
    fell through to the image-to-image path and dereferenced a null
    texture, the fault its Vulkan backend had on Linux. `plume_metal.cpp`
    gains the same branch, and is the port's fifth force-tracked plume file.
  - macOS gives every thread but the main one a 512 KB stack, where Windows
    gives the game's threads 8 MB (the executable's `/STACK`) and Linux 8 MB.
    The host threads that run game code get 8 MB on a Mac now
    (`ultramodern::threads::make_game_host_thread`; elsewhere they are the
    plain threads they were).
  - Mods could not load: macOS lets a program patch its own code only when
    the code segment's maximum protection allows writing, which ld64 will
    not link. `tools/macos/ld64` runs the linker and sets it (Zelda64Recomp's
    approach, rewritten without macholib), a post-build step signs the build
    tree's executable again, and the bundle is signed with the hardened
    runtime and Zelda64Recomp's entitlements (JIT and self-patching).
  - Found by the first run on the Mac: Apple's paravirtual GPU driver, the
    GPU of a virtual Mac, answers yes to `supportsCounterSampling` at the
    blit boundary and then fails an assertion inside `sampleCountersInBuffer`
    ("Not implemented"), which aborted the game at its first frame. plume's
    Metal backend takes no timestamp counter set from a device named
    Paravirtual, or under `RT64_NO_GPU_TIMESTAMPS`; the timestamps feed only
    the renderer's profiler, which reads zero there.
  - Found by the second: every quit ended in a crash report, "mutex lock
    failed" on the runtime's pool replenisher, and once that thread's
    objects were made immortal, a segmentation fault on its timer thread.
    Both are detached threads that still run while the process's globals
    are destroyed; on macOS a lock on a destroyed mutex throws where Windows
    and Linux tolerate it. The pool's and the timer's objects are never
    destroyed now.
  - Found by the third: the game ran at 13 to 18 frames a second even at
    the title, though its GPU drew a frame in 10 ms and its logic took one.
    A probe (`tools/macos_timer_probe.py`) measured the virtual machine
    waking a plain 16.7 ms sleep 61 ms late, and the same wait on a thread
    with macOS's time-constraint scheduling policy, the one audio code
    uses, waking in 0.04 ms. The runtime's VI and timer threads take that
    policy on Apple and wait with `mach_wait_until`; the title runs at 57
    frames a second there now. A physical Mac sleeps precisely either way.
  The build is universal now, Apple Silicon and Intel in one executable
  (the zip is `-macos-universal`). The workflow builds it on GitHub's Mac,
  checks both halves' code-segment protection and the signature, and runs
  `tools/macos_smoke.py` on that virtual Mac, natively and through Rosetta 2
  in parallel jobs: the title from a cold start with lit frames and the
  game's frame rate, the same boot under Metal's API validation, the scoring
  replay (its lines inside the healthy signature, and how many photos stayed
  in step with the Windows scores), and a mod whose hooks run; crash
  reports macOS writes are uploaded. `tools/macos_inputs.py` lays out the
  private repository the workflow's inputs come from. What the last run
  showed: the intro at 57 frames a second on both, no failed assertion
  under validation, the Beach ride's photos scored inside the healthy
  signature within a few units of Windows, the mod's hooks fired, no crash
  report. One limit is the tape's, not the Mac's: a replay is presses per
  pad reading, and it holds only while the machine keeps the console's
  pace at every press, which a shared virtual Mac does not always (a new
  `SNAP_TICK_DELAY_MS=<n>` makes every tick n ms longer on a PC and stops
  the same tape in the lab at 25); a gate of two retraces per frame in the
  VI thread was tried against it, did not hold the tape and broke the 3x
  speed check, and was taken out. Checked here too: the linker wrapper on
  the macOS `dxc` binaries in RT64's tree, the thread code on Linux under a
  512 KB default stack, the smoke script's stages against the Windows build,
  and the Windows and Linux builds with every runtime change (the scoring
  replay's 45 scores unchanged, identical at 3x). Not yet: a physical Mac.
* A mouse could not clear a Button Setup row: on that page its buttons are
  the pointer's, and the row was cleared only by Z, the keyboard's Left
  Shift. Delete clears the selected row now, as it does in most PC games'
  lists of bindings, and so does Backspace, the key a Mac's keyboard calls
  delete; with a mouse, the row under the pointer. Each clears only while
  no binding uses it, and the help lines name Delete on a keyboard and
  with a mouse ("X changes it, Delete clears it.", "A click changes it,
  Delete clears it."). The window's thread counts the presses and the
  page takes them through a new mailbox byte, +0x9C. Checked: a scripted
  Delete press on the A Button row cleared it ("keys.a: the controller
  cleared on the Button Setup page"), and both lines in a capture.
* The port's help lines named the N64's buttons whatever was in the
  player's hand. I found it at the Exit Game question with a mouse, where
  it should say to click; on a keyboard it was worse, because the A key
  steers left and the Z key is B -- the Button Setup page's "Z clears it"
  sent a keyboard player back out of the page -- and an Xbox-style pad has
  no Z at all. The port kept no record of which device was used last. Now
  the window's thread notes the device of every press (a key, a mouse
  button or the wheel, a controller's button or a stick past half its
  travel; the Deck's own Enter and Esc do not count), and every line of
  the port's that names a button is worded for it: the Exit Game and
  Restart questions, the Controls page's Zoom and Button Setup rows, the
  Button Setup page's Device row, Restore Defaults and all eighteen input
  rows, the Mods list's load-order line, a mod's details and its options
  page (its Restore Defaults, a text being typed, an option with no
  description of its own). On a controller they say A and B and the pad's
  own name for the rest ("L Bumper clears it", "L Trigger and R Trigger
  change its load order"; Z on an N64-shaped pad); on a keyboard, the keys
  the bindings name ("X changes it, Delete clears it", "Q and E move it up
  or down the load order"); with a mouse, clicks ("Click again to close
  the game. Right click to stay.", "Drag it to move it in the load
  order.", "Click A Options above for this mod's option."). Each line tries shorter wordings until one fits the box, and falls
  back to the controller's words, with a log line, when none does or the
  help face cannot spell a key's name. The staged lines keep one size and
  are repainted in place, so they change the moment another device is
  pressed, even while on screen; in a course that waits for the pages to
  open, where the keyboard and mouse take turns many times a second. The R
  Button row's text is shorter ("Dashes while held, once you own the dash
  engine.") so the mouse's words fit it. A new switch,
  `SNAP_DEVICE_TEST=<reading>:<key or pad button>`, pushes SDL's own press
  of a key or pad button at a reading, so a replay can prove it. The first
  version of this made the Exit Game question two lines tall and the
  staged strings ran 2,688 bytes past their space, into the Button Setup
  page's first row value; that space starts 256 KB later now, and the port
  refuses to publish strings that would pass it, with an error in the log.
  Checked with the scripted mouse and device presses: every line above in
  the three wordings on the title's Option list, the Controls, Button
  Setup, Mods, details and options pages and the pause menu's list, no
  line falling back; "Press Left Ctrl again to close the game." / "Press Z
  to stay." with A bound to Left Ctrl; and the release suite's pages
  checks, 3 of 3. A text being typed was not captured.
* The header's A and B did not answer the mouse as the rows do. I found it:
  moving onto a row plays the move sound and selects it, while moving
  onto A OK or B Cancel did nothing to see or hear, and on the title's
  Option list a click on them did nothing at all -- that list read clicks
  on its rows only. Their click boxes were also fixed rectangles, so on
  the Mods page the end of "A Details" fell inside B's. Now the header's
  buttons are read off the legend's own pixels (the A icon blue, the B
  icon green, the words after each), for every legend a page shows;
  moving onto one plays the move sound and underlines it in the
  selection's orange; and a click presses it on every page, the Option
  list's included. Checked with a scripted mouse: the underline under
  "B Cancel" then "A OK" on the Option list and under "B Back" then "A
  Details" on the Mods page, a click on "B Back" closing the Mods page
  and one on "B Cancel" leaving the Option list for the title. The
  sounds were not heard: the runs are muted.
* In the Mods page's NEW the N stood a row lower than the E and W, and the
  rule over the actions was too dark a gray to see over the island. I
  found both. The cartridge's own capitals are not one height: its C, H,
  M and N stand at rows 2 to 9 of the face and its A, B, D, P, R, S and Z
  at rows 1 to 9, as does every capital the port draws but its round G,
  O and Q, which it had drawn at 2 to 9 as well. All seven stand at 1 to
  9 now -- the cartridge's C, H, M and N drawn again, for the port's own
  text only, in the style of the port's other capitals; the game's menus
  keep their pictures -- and a new switch, `SNAP_FONT_DUMP=1`, prints
  every capital's and digit's rows when the font is read, so no later
  glyph can drift. The rules, here and on a mod's details and options,
  were 96 of 255 over an island dimmed to between 22 and 65; they are
  176, one step under the header's own lines (217). Checked: the dump
  with every capital at rows 1 to 9, NEW's three letters each at rows 74
  to 82 in a capture, and the rule measured at 176.
* A mod can be turned on or off from its details page, and a mod's text
  option can be typed, as in the other recompilations' mod menus. I asked
  whether the menu was the best it could be; checked against Zelda's own
  menu code, these were the two things it did that ours did not. On the
  details page, Left and Right, or a click on the status line, turn the mod
  on or off as its row in the list does, and the status says what that
  does ("Off after a restart."); the help box says so ("Left and Right
  turn it on or off."). A mod another one keeps on, or one not loaded yet,
  answers with the back sound. On a mod's options page a text option was
  shown and left to its file: now the header says "A Type" on it, and A,
  or a click on it, opens it for typing -- the text white over a line,
  with a blinking cursor and its start cut with "..." when it is long --
  and Enter or the pad's A keeps it, Esc or the pad's B leaves it as it
  was. While a text is typed the keyboard reaches neither the game nor the
  hotkeys, and after it the keyboard stays away from the game until every
  key is up, so the Enter that kept it is not also a Start. A text too
  long for its column is said whole on the help box's second line.
  Checked with a scripted mouse and SDL's own text and key events
  (`SNAP_TEXT_TEST`): Unlimited Film turned off and on from its details,
  the list following; a text typed, a stray letter taken back with
  Backspace, and kept with Enter; another left with Esc; and B undoing the
  kept one. Not checked: a real keyboard's keys held away from the game
  while typing, which pushed events cannot show.
* A mod's picture may be `thumb.dds`, which the runtime reads before
  `thumb.png`, as the other recompilations' mods may ship it. It showed
  nothing: the port read PNG only. `src/dds_image.cpp` decodes
  uncompressed RGBA and BGRA and the block formats BC1, BC2, BC3 and BC7.
  BC7's partition and anchor tables were read off Pillow's decoder, with
  blocks built to show them, and the decoder was compared with Pillow's on
  random files of every format, two sizes each, and on 64 blocks of each
  of BC7's eight modes: every value matched but those of one block in a
  reserved mode, which the format decodes to transparent black and Pillow
  to opaque black. The test mod's BC3 picture showed on its details page.
* The pages run on a stack of their own. A game process has the scene's
  stack: 1024 bytes on the title, 1088 in the Tunnel, 768 in the Rainbow
  Cloud. Opening a mod's options from its new details page froze the game
  in a course, and the cause was that stack: the pause menu's process, the
  list, the Mods page and the options page came to more than it holds,
  and past its far end lies the canary the game checks, then other
  threads' memory. On the title, the options' sprite calls had 124 bytes
  left. Now the pages from a course, the pages from anywhere and the
  title's Option screen each run on a process with 8192 bytes of stack,
  taken from the port's own arena. The same test in all three places left
  at least 7308 of them, and 7284 in a course once the details page had
  its scroll bar; the log says how many when the Mods page closes.

## 1.0.9 -- 2026-09-20

* The runtime is upstream's current one, and the port's changes to it are a
  patch. `lib/N64ModernRuntime` had been a copy of N64Recomp/N64ModernRuntime
  from a commit nobody had recorded, edited in place in thirteen files. It
  is upstream's `cdf5abb` (2026-08-30) now, with the port's changes carried
  as `lib/N64ModernRuntime/SNAP64-CHANGES.patch`, fourteen files that applied
  to that commit reproduce the tree byte for byte; the old base turned out
  to be the tree of 2026-05-17, and three files conflicted. What the update
  brings is the runtime's configuration system and its current mod loader,
  with dependency checks, deprecated-mod handling and game modes, which the
  mod tooling and the shared front end need. Two of upstream's changes since
  the old base are not taken, on purpose. The runtime no longer switches
  present-early on for the port at the first task, so the port does it
  there itself, where the runtime used to. And the runtime now shapes every
  stick through an N64-style octagon, full deflection about 82 rather than
  127, which reshaped the port's own mouse, gyro and keyboard values and
  took the scoring replay from 45 photos to 21; the linear mapping stays,
  and a physical stick's gate remains the port's to add in its own layer,
  as a choice. Verified: the scoring replay scores the same 45 photos and
  exports the same 55; the release suite passed 26 of 26 checks
  in 1104 seconds and the Snap Station's 5 of 5 in 487 on this executable,
  the 1.0.8 numbers to the second.
* The renderer is upstream's current one, and the port's changes to it are a
  patch too. `lib/rt64` had been rt64/rt64 `a012a23` (2026-07-22) edited in
  place; it is `4337374` (2026-09-02) now, eleven upstream commits later,
  with the port's changes carried as `lib/rt64/SNAP64-CHANGES.patch`, and
  the four plume files the port changes carried the same way against the
  plume commit that RT64 pins, which moved with it and brings plume's
  Metal-leak fix. The rebase applied with no textual conflict. What the
  eleven commits bring: VIs with inverted regions are no longer treated as
  valid, draw-area detection considers the viewport's clip rectangle, tile
  synchronization detection is improved, and a Vulkan workaround for RDNA4
  cards. That last one is taken differently: upstream forces Vulkan on an
  RX 90 card with a driver up to `0x200000794103EC` whatever the player
  chose, and my own RX 9060 XT is on exactly that driver, on which this
  port's whole suite and a full playthrough have run in D3D12 without the
  fault; so the port applies that
  clause only in Automatic mode, which it never uses, and leaves the API
  where the Graphics page put it. Verified: the scoring replay scores the same 45 photos, and
  the release suite passed 26 of 26 checks in 1104 seconds and the Snap
  Station's 5 of 5 in 487 on this executable, still on Direct3D 12.
* The port is ready for mods to be written for it. The loader has been in
  since 1.0.0; what was missing was everything a mod author needs and one
  thing the port owed the loader. The kit is two repositories of their own,
  not public at the time of this release: a template
  (Snap64RecompModTemplate: an example hook behind an option, the MIPS
  build with clang and lld against the decompilation's headers, the
  manifest, the modding headers) and the game's symbols
  (Snap64RecompSyms), both in the form N64Recomp's mod tool reads;
  `docs/MODS.md` says how they fit. `tools/gen_reference_syms.py` writes
  the symbols: it now also emits the data symbols a mod names variables
  with (12,754 across 156 sections, the code sections included, because
  this game keeps most of its globals beside its code), and it writes
  each section's ROM address where it had written its file offset in the
  ELF, which the port's own patch build never noticed and a mod's hook
  cannot survive, since the runtime matches hooks by section ROM address;
  the tracked `patches/pokemonsnap.syms.toml` carries the corrected
  addresses, and the patch build's output is byte-identical either way.
  The thing the port owed: a mod imports `recomp_printf` from the port,
  which the other recompilations provide as game-side code their patch
  toolchain exports; this port's patches are compiled with IDO and cannot,
  so `src/mod_api.cpp` provides it as a host function that formats the
  guest's call itself, reading the arguments as the o32 convention lays
  them out, and registers it before the runtime scans `mods/`. Verified:
  the example mod, built in WSL with clang 18 and packed with a
  RecompModTool built from the vendored N64Recomp, loads in the port and
  its hook on the game's scene set-up ran four times in forty seconds of
  the Beach replay; the scoring replay still scores the same 45 photos and exports the same 55.
  The collections the other recompilations' `recompdata.h` gives a mod
  (hashmaps, hashsets and slotmaps kept by the port between calls) are
  provided too, `src/mod_data_api.cpp`, ported from Zelda64Recomp's data
  API on Sergey Makeev's SlotMap with three of that file's faults put
  right (a slotmap read, write or erase of a missing key went on to touch
  a null element; a memory slotmap's erase freed the wrong address); the
  template's example keeps a set in one, and its counts came out right in
  the log across four scene set-ups of the Beach replay.
* A Mods page in the game's Options screen, on the row the stock Return
  held (B returns from the screen, as Return's own help line said, and the
  list has no seventh slot above the help box). One row per mod in the
  `mods/` folder, six on screen, On or Off beside each; A turns the
  selected mod on or off, `mods.json` records it at once, and the change
  takes effect at the next start, which the help line says under the mod's
  own short description. The page is the Button Setup page's arrangement
  turned to a list the host composes: the names and help lines of the six
  rows on screen are drawn by the port into a bank the page is not showing
  and the page turns to the bank (`patches/src/graphics_menu_patch.c`,
  `src/menu_assets.cpp`); the string directory grew past 255 entries for
  it. One line of the runtime changed for it (`librecomp/src/mods.cpp`, in
  the port's patch): upstream refuses to turn a loaded code mod on or off,
  which suits a launcher that toggles mods before the game starts; the
  port's copy takes the change for the next start, which is what the page
  says. Verified by a scripted visit with two example mods in the folder:
  the page opened with both rows, the second was turned off and on again,
  `mods.json` followed each press, and the presented frames show the page
  as described.
* The pages open anywhere, on one key. Until now Graphics, Sound, Controls
  and Mods lived only in the title's Options screen, so a frame-rate or
  button change mid-ride meant quitting the course, which is the one thing
  every other recompilation's menu gets right by opening on a key anywhere.
  Esc, or Select on a pad, now opens the list on any screen -- the title,
  the lab, the map, a paused course, the Report, the Gallery -- over the
  screen as it stands: on a screen without a pause of its own, the port
  makes an object with one coroutine on it through the game's own object
  manager (a dead function of the resident code, replaced, is the coroutine
  the runtime can dispatch), and that coroutine freezes every other object
  the way the level's pause freezes the player's, dims the picture at the
  pause's own 153 of 255, runs the list in the Options screen's dress, and
  wakes exactly what it froze when Esc, Select or Start closes it. In a
  course the key pauses the ride as Start does and opens the pages at once,
  with no menu in between. Enter stays the game's Start: the pause menu,
  which has a fourth pill under Retry, Options, which opens the Options
  screen's list over the paused, dimmed course; B brings the pause menu
  back, and Start on the list or on any page closes everything and resumes
  the ride at once, keeping what is on screen. The key stays out of what
  a held screen would break, and four things decide that. The scene: the
  attract demo and the credits are scripted to their music and the freeze
  would leave them behind it, so the key is dropped there (a ride with no
  pause handler is the demo: the game makes the handler only when no idle
  script runs). The fade: its veil is drawn over everything by a process
  the freeze would hold, and the first version, opened under the title's
  fade in, showed a white screen for as long as the pages were up. The
  press now waits while a fade step is under way, or while a veil object is
  in the scene with an alpha to draw, four seconds at most, and is dropped
  under a veil that stays. Neither half is enough alone: a fade out leaves
  the alpha at 255, and the screens that follow without a fade of their own
  -- the photo check after a ride, the evaluation -- keep that number with
  no veil drawn, while the title keeps a veil object long after its fade in
  has brought the alpha to nothing. A screen's first two seconds are left
  to its entrance in the same wait. The display list: the pages are
  sprites drawn into the screen's main display list buffer, which is the
  cartridge's own size, and the photo check after a ride fills its 53,248
  bytes to within 176 on its own. The list on top overran it by 712, which
  is the game's panic -- a loop the recompiler turns into a parked thread,
  and the port's hang report ten seconds later. The room that buffer had
  left is measured every frame now (`src/dl_budget.cpp`) and the pages stay
  shut below 8,192 bytes of it, from the key and from the pause menu's pill
  alike, with a line in the log; the list and the Graphics page together
  drew 5,432 bytes over the title, and the pages 2,016 to 4,040 over a
  paused ride whose buffer is 20,480. The camera: the pages sat on the draw
  link the title's and the course's sprite cameras cover, and on the other
  screens they were drawn, where they were drawn at all, by the fade
  system's 3D camera with whatever render state the screen had left -- the
  evaluation and the lab came out as static with the list in pieces, and
  the photo check showed nothing while the list took every press. The
  runner makes a sprite camera of its own now, as the title screen makes
  its, on a draw link no screen uses and at the lowest draw priority, which
  is the last to draw. Two faults of my own the same tapes found. I had
  declared the object manager's limit as 32 bits where the game's is 16
  (`omMaxObjects`, sys/om.c), so the store that lifts the limit went to the
  wrong bytes and put 65535 into the object size beside it; every other
  extern in the patches was then checked against the decompilation's
  declarations (a hundred, no other mismatch). And the patch Makefile did
  not list the `.inc` files the page sources include, so three rebuilds
  "with the fix" carried the old code; an `.inc` edit rebuilds its object
  now. The pools that limit lifts grow from the screen's general heap, a
  bump allocator with no free, so the pages bring their own memory as well:
  the heap's pointers are turned toward an arena of the port's own, in
  RDRAM beyond anything the cartridge addresses, while the runner's object
  and thread are made and while its pages are up, and turned back after;
  the arena is cleared and its cursor moved back where the game starts a
  scene's heap (`gtlInitHeap`, hooked). Every heap the tapes met had room
  (390,680 bytes at the least), so the arena is a guard, not a cure I can
  show. The runner's log line says what the screen had to give: the scene,
  its age, the heap's free bytes, the objects and their limit, the display
  list's room. On the pad, the photo save moved from Select to the right
  stick pressed in for it. Photographed, each opened and closed by the key:
  the title during its intro (the press waited out the fade), the New Game
  question, the course map, the evaluation, the lab, and a paused ride by
  the key and by the pill; and the attract demo with the key dropped and
  the photo check with the pages refused. The PKMN Report, the Album and
  the Gallery run the same code and are not yet photographed. The release
  suite presses the key now (`tools/release_check.py`, the `pages` check:
  the title tape with the key at two readings, the runner's line and the
  display list's read from the log, nothing hung or overrun); the released
  executable passed the suite's 28 checks in 1154 seconds and the Snap
  Station's 5 in 488. The pill is the pause menu's
  own artwork: the three stock pills are 89x19 full-color sprites with
  their words baked in, so the port composes a fourth pair, plain and
  selected, out of the yellow pair in the ROM -- the word erased to the
  fill, every texel's hue turned so the fill sits at green (the one color
  of the set the menu does not use), and Options set in the pills' own
  letters at the pills' own centering, the O being the Q of Quit Course
  without its tail and the p the n's stem closed like the o. The list wears
  the Options screen's own dress at the screen's own coordinates -- the
  rules above and below the heading, A OK and B Cancel, the help box --
  from strips of the very same sprites (the rule and the box's side written
  down texel for texel, the heading and the legend harvested whole, the
  legend as 32-bit RGBA, the one 32-bit strip the pages draw; the rules,
  sides, heading and legend of the two screens measure the same to the
  pixel in the captured frames), kept under every page as the title's
  screen keeps its own, and has five rows, Graphics, Sound,
  Controls, Mods and Exit Game, from the first row down as the stock list
  runs. The dim behind it is the game's own pause dim, black at 153/255,
  which leaves the same 40 percent of the picture the title's Options
  screen leaves (its backdrop is tinted 0x66). The HUD's item icons step
  aside while the pause menu is up, as the game itself takes them down for
  its cutscenes: the fourth pill stands where the balls are, and even the
  cartridge's Retry pill lay over the B ball's badge; the film counter goes
  only behind the pages, whose header rule runs under it. Four functions of
  the level code are replaced with their original bodies plus the item
  (`patches/src/pause_menu_patch.inc`); the pages lost their dependence on
  the main menu overlay, whose little sprite helpers they had been calling
  and which is not loaded in a course, and learned where they are open, so
  over the pause menu they touch none of the Options screen's sprites and
  use the course's sounds, and the Controls page's Z Button and Control
  Stick rows write the player flags directly rather than the overlay's
  mirrors. Verified by two scripted visits on the Beach, photographed at
  each step: the pause menu with the pill plain and selected, the dressed
  list, the Graphics and Mods pages over the course, Exit Game's question
  asked and withdrawn, the pause menu back on B, and the ride resumed both
  from the pause menu and by Start from a page; the first attempt crashed
  in the Graphics page's own walk over the Options screen's sprite chains,
  which is why the chain accessor now returns nothing outside that screen.
* The Mods page does what the other recompilations' mod menus do. Their
  menus toggle, reorder and configure mods, install one from a file and
  open the folder; ours toggled. Now: L and R move the selected mod up or
  down the load order (`set_mod_index`, kept in `mods.json`); Z opens a
  mod's options page when its manifest declares any, one row per option
  with Left and Right changing it -- an enum to its next choice, a bool on
  or off, a number a step at a time within its range, a text shown and left
  to its file -- each change written to the mod's own settings file at once
  through the runtime's `set_mod_config_value`; the rows show the version
  after the name and the help line the author; two rows under the mods,
  Open the mods folder and Restart the game, do what their buttons do; a
  `.nrm` dropped on the window is copied into the folder for the next
  start; and a hint at the header's right says which of Z, L and R apply to
  the row; a mod whose required dependency is missing or the wrong version
  says so in its help line. Verified by a scripted visit with the example
  mod in the folder twice under two ids. Two smaller things from the
  screenshots: the header face's drawn lowercase -- the d of Mods above
  all, whose stem rose two rows past the t's -- was measured against the
  stock word and resized to its x-height; and the title's Options list
  left its Mods row on screen under the Graphics and Sound pages, over
  the Frame Rate row, because those two pages hide the list's staged rows
  with their own older copy of the code that never learned the row
  (photographed, then fixed and photographed again).
* macOS, in the tree and not yet run. The community's Apple Silicon build
  of 1.0.0 (pull request #2, by appleforever11), which drew with Metal and
  reached a course on an Apple M3 Pro, is ported onto today's tree behind
  `__APPLE__` and `if (APPLE)`: Metal through plume's backend, with RT64's
  shaders compiled to Metal libraries; the window's Metal layer handed to
  RT64; SDL static inside the executable; the data directory in
  `~/Library/Application Support/Snap64 Recomp/`; the Linux log, lock and
  relaunch made POSIX; the controller subsystem owned by the pad thread,
  because SDL's IOKit driver only sees pads from the thread that
  initialized it; and `tools/macos_bundle.py`, which lays out the
  application bundle, signs it ad hoc and zips it with a START HERE text.
  A workflow, `.github/workflows/macos.yml`, builds the bundle on GitHub's
  Mac from a private repository of the ROM-derived inputs, as the other
  recompilations' workflows do, and runs the bundle once for its log and
  frames. I have no Mac: none of this has run on one, and the README does
  not offer it until it has; BUILDING.md, step 15, is the whole account.
  Windows and Linux compile what they did; the scoring replay scores the
  same 45 photos on the rebuilt Windows executable.
* The documentation, rebuilt. The README had grown to fourteen thousand
  words, six times the size of the other recompilations' pages, and in
  several places contradicted itself: it said every run had been on one
  machine while listing three players' GPUs, that Vulkan had never been
  run while its own settings table said where it had, that the Deck's
  Gaming Mode and the corner flicker remained after both were done. It is
  a front page now -- what the port is, how to run it, what it adds, the
  rule, the controls, what is known not to work -- and the manual lives
  under `docs/`: MANUAL.md, SNAP-STATION.md, STEAM-DECK.md,
  VERIFICATION.md and HISTORY.md, the text moved rather than rewritten and
  the stale sentences corrected where they stood. The section on how the
  port was made now says what it is, my project made with Claude Code,
  with the commit trailers as the record, in place of the account of the
  models that had stood there. `START HERE.txt` no longer promises the
  rebinding page that 1.0.6 shipped; NOTICE.md counts the screenshots as
  they are; the bug report form asks which system rather than which
  Windows, and points requests at Discussions; and a GitHub workflow runs
  `tools/check_docs.py`, which follows every relative link and anchor in
  the Markdown, and compiles the Python tools on every push, the only
  check a build that needs the ROM can have.

## 1.0.8 -- 2026-09-15

* Fast forward: hold Tab, or the pad's right shoulder button, and the game
  runs at two, three or four times the console's speed, the Controls page's
  new Fast Forward row (3x as shipped, or Off). It is the console run
  faster, not the game changed: the runtime's clocks -- the retraces, the
  counter, osGetTime, the OS timers -- run that many times faster than the
  wall clock from the moment the key goes down, so the game steps through
  exactly the frames it would have stepped through anyway, with the same
  scores and the same saves. The renderer shows the latest frame at the
  display's rate, frame interpolation off for the duration, and the audio
  path averages every two, three or four stereo pairs into one, so the
  sound keeps the picture's pace, quicker and higher the way a tape is in
  fast forward. The key is the `fast_forward` entry of the settings file's
  `keys` table; the Button Setup page refuses its sources, as it refuses
  Back. `SNAP_SPEED=<n>` holds a whole run at that speed with no key, and
  the release suite's new speed check runs the scoring replay at 3x and
  compares every scored photo with the 1x run's. Verified on my PC: a real
  Tab hold took the Beach ride from 64 drawn frames per pacing report to
  600, three times the rate, and released cleanly with no stall at either
  end; the scoring replay at 3x scored the same 45 photos with the same
  numbers as at 1x; the Controls row and its help line composed, and Right
  and B read back on the host as 4x and then 3x again. Asked for by
  [Video Game Esoterica](https://www.youtube.com/@VideoGameEsoterica) in his video
  ["Pokemon Snap Recomp Out NOW! More Pokemon PC Ports"](https://youtu.be/1ds9leciGU4?si=iLBwSeI-OqSO8NsI)
  (2026-09-13); thank you.
* Slow motion, the same thing run the other way: hold Space, or press the
  left stick in on a pad, and the game runs at half or a quarter of the
  console's speed, the Controls page's new Slow Motion row (Off as
  shipped, because a shot lined up at half speed is an easier shot than
  the cartridge offered). The runtime's clocks take a ratio now rather
  than a whole number; below 1x the renderer keeps interpolating, each
  game frame's span stretched over the longer time it stands for, so a
  slow ride is smooth rather than a slideshow, and the audio path
  stretches every stereo pair into two or four, interpolated, so the sound
  plays slower and lower in step with the picture. The key is the
  `slow_motion` entry of the `keys` table, on the left hand so the right
  thumb stays free for A; slow motion wins when both keys are down;
  `SNAP_SPEED=1/2` holds a whole run at half speed, and the suite's new
  slow check times the Beach replay to the same reading at 1x and at half
  speed. Verified on my PC: a real Space hold took the Beach ride from 64
  drawn frames per pacing report to 32 with the display's presents
  unchanged, and released with one hitch and nothing after; the scoring
  replay at half speed scored the same 45 photos with the same numbers as
  at 1x, in twice the time; the Controls row and its help line composed,
  and Right and B read back on the host as 2x and then Off.

* Photo Detail showed some photos, or the bottom rows of one, at the
  console's own resolution, some runs and not others, a restart clearing
  it (issue #15, Succulent-Puppet, Intel UHD 630; my own review and Oak's
  comparison, since 1.0.0). Three faults in the ring of pinned copies,
  found one at a time from my own playtest logs. The ring pinned every
  small color render as a possible photo, the interface's 8x8 and 16x16
  icons included, dozens a frame on the review screens, so it overflowed
  within a single frame and the photos on screen were the ones pushed out;
  nothing smaller than a thumbnail is pinned now, and a photo drawn within
  the last two frames is never pushed out. A photo 105 rows tall is loaded
  in strips of fourteen, the last of them seven rows past the bitmap's
  end, and the comparison only ever tried windows that fit whole, so the
  last seven rows of every such photo drew from the console's texels; a
  strip is matched on the rows that exist now. And the game keeps a
  photo's bitmap across screens without rendering it again -- Oak's
  comparison draws "This time" from the Camera Check's thumbnail -- while
  the Report's browsing renders a fresh preview on every cursor move and
  six thumbnails a page, all of which churned the course's own photos out
  of the ring before the comparison needed them; a copy now lives until
  the game halves another photo over its bitmap, which is the one moment
  it can never be drawn again, and the ring grew from forty to sixty-four.
  Verified on my PC on the last build: the review photo sharp to its
  bottom edge and both of Oak's comparison thumbnails sharp, with the log
  showing every strip served and nothing evicted. The log names any
  photo-sized load that matches nothing, with the copy it came closest to
  and the first pixel that differed, so a recurrence explains itself.
* `snap64.log` can be copied while the game runs. It was opened for
  exclusive access, so a player could not attach it to a report without
  quitting first.
## 1.0.7 -- 2026-09-13

* Restore Defaults is the last row of the Button Setup page, after Stick
  Right, where 1.0.6 had it second, under Set Up. Second put the page's
  least-used row in its most looked-at slot and in the path of a quick
  Down and A, and cost the first screen a binding: the page now opens on
  Set Up and five inputs, A Button to L Button, and ends the way every
  such list ends, with its reset after what it resets. Up from the top
  row wraps to it, as it always did; its help line names the device at
  the top, which is off screen from down there; the second A it asks for
  is unchanged. Raised by me on 2026-09-13, the day 1.0.6
  shipped.
* A first start with no ROM asks for it. Until now a missing
  `pokemonsnap.z64` produced a dialog naming the path and nothing more, and a
  read-only Linux install needed the file placed in the config home by hand.
  Now the port says what it needs, opens the system's own file chooser,
  checks the chosen file the way the runtime does (the byte order from the
  header, the 64-bit hash of the big-endian image against the expected
  one), refuses a wrong one with both hashes and asks again, and copies a
  right one into the data folder as `pokemonsnap.z64` in big-endian order,
  so it is never asked again. The chooser is RT64's own copy of
  nativefiledialog-extended, which the build already carried. `SNAP_ROM_PICK`
  answers the chooser for the release suite.
* The README no longer lists Widescreen as untested beyond the Beach: I
  have since played every course with the option on and seen nothing
  wrong, and say so in the verified list, as play, not proof.

## 1.0.6 -- 2026-09-13

* A page in the game for the bindings: the Controls page's new Button
  Setup row, third on the page, opens it (named as the game names such
  screens, "Z Button Setup"). A row for each of the game's eighteen
  inputs, named by its button and what it does ("Z Button: Zoom", "C-Up:
  Look Back"), shows what presses it on the device picked at the top
  ("Set Up: Keyboard", Mouse or Controller), two names joined with "or",
  and its help line says more about what the input does in the game. A on a row listens for the next press of that device and binds it
  in place of the device's old one; Z clears the device's binding of the
  row unless nothing else would press the input; Restore Defaults, asking
  for a second A, puts the shipped bindings of the device shown back. What
  no table changes is named on the rows too: the stick that aims and the
  one that works the C buttons, the mouse's motion while Mouse Aim is on,
  Esc beside Start. The port hands the game no input while it listens, and
  none until every key and button is let go after, so the press cannot
  land as the button it was or as the one it now is. Every change is in
  force at once and reaches the settings file by the usual debounced
  write; the file's `keys` table is the same table and stays editable.
  Asked for in Discussions #3, and the first of the things the other
  recompilations have that this port did not. The page names any key in
  the menu's own face, so that face gained the capitals I, K, Q, U, X and
  Y, a q and the punctuation keys are named with, drawn in its style, and
  the header face a B and an e for the page's heading (an M and a g were
  drawn on the way and stay for a heading that needs them).
* The Controls page has a Pad Sticks row: Swapped makes the right stick
  aim and the left work the C buttons, for a player who aims with the
  right thumb (`pad_sticks_swapped` in the settings file). The sticks were
  the one part of a pad no setting could move. Beside it a Dead Zone row:
  how far the aiming stick moves before the game sees it, 0 to 40 percent
  in steps of five, 15 as shipped (`pad_deadzone`), for a pad whose stick
  drifts at rest; the port's dead zone had been fixed at 15 since 1.0.0.
* The Graphics page's Frame Rate row runs past Display to 60, 90, 120,
  144, 165 and 240, each the Manual mode held at that rate by
  interpolation; until now Manual was reachable only with F8 or the file,
  and the page's own row could not show it. F8 still cycles the modes.
* A saved fullscreen is restored after the window opens, as 1.0.5 said it
  was. That release kept aside what the settings file said about
  fullscreen for a restore a moment after the window opened, but the
  reader of the file had no line for that key (fullscreen had been a
  session-only field until then), so the saved value was always false and
  the restore ran only on a Steam Deck, whose default is fullscreen. Found
  by this release's own screenshot run, which asked for fullscreen and
  got a window.
* The menu strings' directory could hold 127 entries and 1.0.5 wrote 129:
  the last two landed in the pixels of the black tile nothing draws. It
  seats 255 now, and the strings sit after it.
* The strip pool's two counters shared their mailbox words with the Exit
  Game item's pointers since 1.0.4. Nothing read the counters and a
  pointer is larger than any count, so the peak never overwrote one; a
  refused strip, never seen, would have moved the item's pointer by one.
  Moved.

## 1.0.5 -- 2026-09-13

* `SNAP_DATA_DIR=<absolute path>` in the environment moves the data
  directory on Windows as it already did on Linux: the ROM is looked for
  there, and the save, the settings, the log, the exported photos and the
  mods folders live there, while the files the port ships beside the
  executable stay beside it. Asked for by PortForge's author (zamiba,
  issue #8) so a launcher can keep profiles apart; a relative path is
  ignored with a line in the log.
* The runtime's video update no longer reads the game's video mode before
  the game has chosen one: a tick before the first mode dereferenced a
  null pointer. Found by appleforever11 on a macOS build (pull request
  #2); the guard is theirs.
* On a Steam Deck in Widescreen, a course's first frames after its
  opening cinematic showed a chunk at the top-left -- black bands and a
  piece of an earlier picture -- until the next camera cut, the
  viewfinder's first raise, repainted it (me, on the Deck,
  from 1.0.1 to 1.0.4). One triangle of the course's sky dome has a vertex
  on the camera plane there; the renderer nudges such a vertex a hair in
  front of the camera, which makes its projected coordinate enormous. The
  Deck's Mesa driver (RADV) culls that triangle in its NGG stage, in
  software, ahead of the hardware clipper that copes with it, so the
  corner kept whatever the color buffer held before. Settled with the
  driver's own switches on my Deck: `RADV_DEBUG=nonggc` (that
  culling alone off) draws the frame whole, while its synchronization,
  memory-zeroing, compression and shader-compiler switches change nothing;
  Windows (Direct3D 12 and Vulkan) and Mesa's software renderer always
  drew it. The port now sets `RADV_DEBUG=nonggc` before it creates its
  Vulkan instance on Linux, keeping and extending a value the player set;
  other drivers ignore the variable, and the log says what was set.
* A freshly created render target is cleared before its first use, ahead
  of the game's framebuffer being read into it. The memory a driver hands
  a new texture is zeroed on Windows and a previous owner's under the
  Deck's, and the margins Widescreen adds are drawn by nothing until the
  picture reaches them. A hardening, first taken for the fix of the chunk
  above, which it is not.
* The one-tick hold at a camera cut delivers the held picture into the
  renderer's target by a drawn copy instead of a transfer command, which
  RT64 itself keeps away from its render targets. Nothing shown changes:
  verified by presented-frame capture of the cut holds through the logos
  and the intro on Windows (Vulkan, anti-aliasing 4x at 90 Hz), each held
  frame equal to the one before it.
* With Mouse Aim or Gyro Aim on, the Beach tutorial asked for the Control
  Stick after ten seconds of looking around (me, on a
  Steam Deck with the gyro). The mouse and the gyro turn the view directly,
  never through the stick, so the game's watch for the stick
  (player.c, the check the tutorial runs for six hundred frames) saw
  nothing. The port now reports a turn it applied, frame by frame, in a
  byte of its mailbox, and the tutorial's check takes that as the stick
  (patches/src/tutorial_patch.c). With both off the byte stays zero and
  the check is the ROM's; the counts, the pause and the message are the
  ROM's in every case.
* Fullscreen entered with the maximize button or F11 was dropped by the
  next change of any setting on the Graphics page (issue #13, flamespeedy
  on Windows and dCo3lh0 on Linux). The page reads its rows from the
  port's mailbox and writes every one of them back on an edit, and the
  mailbox's fullscreen byte was written only when the pages were seeded,
  so a fullscreen entered anywhere else left it saying windowed and the
  next edit made it so. Every setting change made outside the pages now
  writes the current settings into the mailbox first. A saved fullscreen
  is also restored a moment after the window opens, through the same
  live path the Deck and the maximize button use, on every platform;
  1.0.4 forgot it on every launch.
* A change of anti-aliasing on the Graphics page or with F9 took effect
  only at the next launch: the code that rebuilds the render targets
  compared the new sample count with itself, so the rebuild never ran
  (seen in a Steam Deck's log, the renderer at one sample with the setting
  at 8x). The previous count is now read before the new setting lands,
  and the log says when the targets were rebuilt.
## 1.0.4 -- 2026-09-12

* After a switch between a window and fullscreen on the Gallery or the
  PKMN Report, the thumbnails showed the wrong pictures (issue #12,
  Succulent-Puppet). Photo Detail draws each thumbnail from a pinned
  high-resolution copy of the game's own render of it. The renderer
  destroys every such copy when the window changes size, and the game does
  not render its photos again for that: the thumbnails' tiles went on
  naming copies that were gone, were left unset, and drew from the wrong
  texture. The pinned copies now outlive that wipe -- each is its own
  texture and needs nothing the wipe removes -- so the thumbnails keep
  their detail across the switch; a copy pinned before the switch is at the
  render scale of that time, and is replaced when the game next renders the
  photo. A copy's number is also never reused for another copy. On an AMD
  card under Direct3D 12 the same switch did worse than wrong pictures:
  the first frame after it drew from tiles that named no live texture,
  Direct3D removed the device (`CreateResource failed with error code
  0x887A0005` in the log) and the port crashed on the next call, twice of
  two runs of 1.0.3 here, and once more from this tree with only the copy
  survival switched off, which is what pins the cause. The fixed build
  played the same switch through five times on both backends, thumbnails
  intact in fullscreen and pixel-identical after the return to a window.
  A tile whose copy is missing for any other reason now draws one
  transparent black texel instead of texture zero (with that alone the
  device survived the switch and the thumbnails went blank until the game
  rendered them again), and the render thread's timing readback no longer
  dereferences the null a removed device hands back: `[SNAP-D3D12]` names
  the removal in the log instead.
* On Linux the log is written on every launch: `snap64.log` in the data
  directory, whatever stdout is. 1.0.3 wrote it only when there was nowhere
  to print, so a launch from Steam, whose stdout is Steam's own pipe, left
  no log to attach to a report. A terminal or a redirection still gets
  every line. The previous run's log is kept as `snap64.prev.log`.
* On a Linux install whose folder cannot be written, the files the port
  ships beside the executable -- `gamecontrollerdb.txt`, the window icon,
  `menu_text/recomp_logo.png`, the seed of the seen-shader list -- were
  looked for in the data directory (`~/.config/Snap64Recomp`) and not
  found: no community pad mappings, no window icon, no Recomp badge under
  the title, and every shader compiled the first time it appeared. They are
  read from beside the executable now; a `gamecontrollerdb.txt` or a
  `menu_text/` in the data directory is read too and wins, and the
  seen-shader list is copied into the data directory once. The launcher
  the port writes points at the executable's folder in that case, not at
  the data directory.
* The log reports a stall. When the game has not run a logic step for ten
  seconds while the window is up, `[SNAP-HANG]` says what the game had
  submitted, what the renderer took and presented, and the state of every
  game thread with the queue it waits on. For the freeze aiming at Moltres
  on a Steam Deck (issue #11, leansteak096-blip), which has not reproduced
  on this Deck or on Windows: the next report's log will name the stuck
  side; not after the machine was asleep or the process held, which the
  clock behind it would otherwise count. `[SNAP-OS]` reports a message
  the game sent without waiting that a full queue dropped, twice per
  queue, for the same reason; the game's one- and two-slot flag queues,
  which drop by design and appear in every log, are marked expected.
* The gyro is no longer reported as "all zero for half a second" when no
  readings arrived at all, as at the quit box, which holds the event loop:
  the earlier build logged that twice at every quit, with the same count
  both times. Readings that arrive and carry no turning are still
  reported, and on a Deck still re-send the IMU switch.
* The texture decode shader declares its output as 8-bit RGBA for Vulkan,
  the format the texture has, instead of the 32-bit float format the
  compiler inferred; the validation layer reported that mismatch seven
  times a run. The mismatches it reports for the framebuffer read-back and
  write-back buffers, whose format follows the framebuffer's depth, need a
  newer shader compiler than the tree's and are left as they were.
* The 2D Detail help and the README say what Sharp for everything
  (`upscale_2d` 2) costs: a line under a logo and a fringe around a keyed
  sprite, seen on a Steam Deck.
* On a Steam Deck in Gaming Mode the port drew into a 16:9 surface, which
  gamescope scaled onto the 1280x800 panel with bars top and bottom and
  a softer, less even picture, and it stuttered: gamescope gives a
  non-Steam shortcut a screen of Steam's choosing unless its Game
  Resolution is set to Native -- 3840x2160 on my OLED Deck --
  and the port took the screen it was given, so it was rendering at its
  8x cap on a handheld (found in the 1.0.4 test logs: the view widened
  by 16:9 in Gaming Mode and by 16:10 in Desktop Mode, and the screen
  the port was asked to fill measured 3840x2160). The port
  now asks gamescope for the display's own size at every start, before
  its first fullscreen -- the request Steam's Native setting makes, a
  property on the X root window that gamescope clamps to the display --
  and, if the surface has not taken the screen's size three seconds
  later, sizes the window by hand and applies fullscreen again. The log
  says what was asked, what the screen then is, every change of the
  surface's size, and, if a surface stays off the panel's size for three
  seconds, that the Native setting is the fallback.
* With anti-aliasing on, the frame after the HAL logo -- the intro's
  scenery re-posed for one tick, a frame the console never displayed --
  showed on a Steam Deck (me, since before 1.0.0) and on
  any 60 Hz monitor. The renderer holds the previous picture over that
  tick, and with anti-aliasing the held picture has to go into a
  single-sampled target of its own, the first interpolated target; the
  present thread showed that target only for a tick of more than one
  display frame. A 90 Hz Deck over the 60 fps logos alternates one- and
  two-frame ticks, so the hold on a one-frame tick was counted as
  delivered and never seen; at 60 Hz there is no interpolation and the
  hold was refused outright. Reproduced on Windows under Vulkan with the
  Deck's own settings paced at 90 Hz: the captured presents show the
  re-posed scenery for exactly one display frame between two identical
  ones, and not at all with anti-aliasing off. The hold is now delivered
  through that target in every anti-aliased case, and the present thread
  is told to show it.
* The game's Options screen has an **Exit Game** row, under Return, in the
  screen's own font and rhythm (me, from Steam Deck play:
  a pad had no way to close the program, and in Gaming Mode the quit
  question of a held Esc needs a keyboard). Its help line says what it
  does; A turns the help line into "Press A again to close the game, B to
  stay", the second A closes the program the way the window's close
  button does, and B or moving off the row withdraws the question. The
  choice reaches the host through the settings mailbox and is logged as
  `quit: Exit Game chosen on the Option screen`. Exercised by replay on
  Windows: the confirmed row closed the program by itself, and the
  withdrawn one left the player on the list and then, through Return, on
  the title.

## 1.0.3 -- 2026-09-10

* The port ships `gamecontrollerdb.txt`, the community's list of pad
  mappings (SDL_GameControllerDB, zlib license, `licenses/`), beside the
  executable. SDL uses only the pads it has a mapping for; its own list
  holds a few thousand and no 8BitDo N64 Mod Kit, so that pad was seen at
  start-up and ignored, as the log said, while the recompilations that ship
  the community's list took it. Reported on Reddit by alefsousa017. The
  port has read the file since 1.0.1 when a player put it there; now it is
  there. Three lines are appended for 8BitDo's three N64 pad ids over
  Bluetooth on Windows, where SDL tags a pad with the bus it arrived on
  and the list's lines for them are USB lines: the same mappings with the
  Bluetooth bus byte, derived, not read from a pad.
* The log says when `gamecontrollerdb.txt` is not beside the executable,
  since it is expected to be.

## 1.0.2 -- 2026-09-09

* A Switch Online N64 controller over Bluetooth opened a black window that
  never drew the boot logo (issue #7). Two faults met. Every SDL call the
  port made for a controller ran on the game's own controller thread, and
  SDL's HIDAPI driver for a Nintendo pad over Bluetooth spends seconds
  there: its open and close handshakes are synchronous, up to five attempts
  of a 500 ms write and a 100 ms wait for each command, it puts the pad in
  the report mode that only speaks when a button moves, declares it gone
  after three seconds of silence and takes it back at the next touch. And
  the game's controller thread, straight after opening the pad, registers
  with the scheduler by sending into an eight-slot queue without waiting
  and then waits for the answer forever (`scExecuteBlocking`); the runtime
  delivers every retrace that piled up while the thread was held before
  that send, so a pad open longer than eight retraces -- about 130 ms --
  filled the queue, the registration was dropped, and the boot never got
  its hand-off. Reproduced here with an injected 20 s pause. Both are
  fixed: the pad now lives on a thread of its own, which opens, polls and
  closes it and hands the game a copy of its state, so the game's threads
  make no SDL controller call at all and neither does the window's; and a
  periodic interrupt message (a retrace, an audio tick) no longer takes
  the last free slot of a game queue when a burst of them is delivered,
  which is what a console does too, where at most one such interrupt is
  ever pending. `SNAP_PAD_STALL_MS=<ms>` in the environment holds the pad
  thread that long after an open, for anyone who wants to watch the game
  carry on regardless.
* `pad_enabled` in the settings file: `false` makes the port ignore every
  pad without unplugging one.
* The renderer no longer drops a Pokémon's pass because its triangles
  reach the camera plane. RT64 fits a box to where each triangle's
  vertices project, from positions divided by w with no clipping, and
  drops a pass whose box stays empty; a vertex at or behind the camera
  plane lands anywhere by that arithmetic, so a triangle that straddles
  the plane was judged back-facing or its box missed the picture, and a
  pass made only of such triangles vanished whole. The console clips such
  a triangle and draws what remains. A triangle with such a vertex now
  counts as visible and as covering the scissor; the same family of fault
  as 1.0.1's Widescreen pop-out, which only the widened view had been
  guarded against. Found while chasing Snorlax vanishing when the camera
  is pitched up at him on the Beach -- which turned out to be the
  cartridge's own rule, not this (README, "Known limitations").
* Rumble, corrected. 1.0.1's notes said the Rumble Pak now ran for as long
  as the game asked; the game never asks. The only motor calls in the
  cartridge are in its reset handler, which the port never runs, and
  `contRumbleStart` is called nowhere in the ROM, so no player has ever
  felt a buzz from this port and none will. The two 1.0.1 entries below say
  so now. `rumble_strength` is gone from the settings file (a file that
  still carries it loads as before), and the port no longer tells the game
  a Rumble Pak sits in port one: a console with an empty pak slot reports
  none, and so does the port, which also keeps the game's pak probe off a
  slot with nothing in it. The stub that probe reached returned 2, which is
  `PFS_ERR_NEW_PACK`, under a comment calling it "no pak"; it returns 1,
  `PFS_ERR_NOPACK`, which is what it meant.

## 1.0.1 -- 2026-09-07

* Every shader the game is known to ask for is compiled during the boot
  logos, on idle threads, instead of the first time it appears in play. The
  port always had the machinery -- it records each shader it meets in
  `cache/rt64-seen-shaders.bin` and warms the list at the next start -- but
  it shipped no list, so every machine started cold and met each course's
  shaders as a stall on their first frame, which under Proton is the
  slowdown a player reported in the later courses. The archive carries a
  623-entry list from a full playthrough now; a shader not on it is still
  compiled when met, and added.
* Rumble lasted a tenth of a second. The Rumble Pak is on or off -- the game
  runs the motor with `osMotorStart` and stops it with `osMotorStop`, and
  nothing re-triggers in between -- but the port asked SDL for a hundred
  millisecond buzz, so every rumble the game meant to hold was cut short. It
  now runs until the game stops it, as the pak did. [Corrected in 1.0.2:
  the cartridge never runs the motor in play, so this changed nothing a
  player could feel.]
* `rumble_strength` in the settings file sets the Rumble Pak's strength from
  0 to 100, and `0` switches rumble off. The cartridge had no such control,
  so the default is full strength. [Removed in 1.0.2: the game never asks
  for rumble, so the key had nothing to set.]
* A pad SDL does not recognize can be taught with `gamecontrollerdb.txt`,
  the community's own mapping list, dropped beside the executable; the port
  reads it at start-up and reports how many mappings it added. The log line
  for an unrecognized pad now says so. What used to be offered instead was
  SDL's `SDL_GAMECONTROLLERCONFIG` environment variable, which is a
  developer's tool rather than a player's.
* The controller can be rebound. Its buttons and triggers were fixed in the
  port's own code; they are entries in the settings file's `keys` table now,
  beside the keyboard's and the mouse's, written as `"Pad A"`,
  `"Pad LeftTrigger"` and the rest of SDL's own names. The defaults are
  exactly the mapping that was hardcoded, so a player who never opens the
  file feels no difference, and one who wants Z on the left trigger changes
  one line. Asked for in Discussions; an in-game page for it comes next.
* Keyboard and mouse support. In a course the mouse turns the view
  directly, each pixel an angle added to the game's own yaw and pitch, so
  it moves as far and as fast as your hand and no stick speed limits it
  (the cursor is captured while the course runs and the window has focus,
  and free everywhere else); wherever the window has focus the left button is
  A, the right button Z, the middle button B, the wheel C-Down and C-Up,
  and the side buttons C-Left and C-Right, so the game's own scheme carries
  over: click to shoot, right-click to zoom, wheel down for the flute, and
  a click advances Oak's text. Every keyboard and mouse binding is
  remappable through the settings file's `keys` table; the shipped layout
  is unchanged.
* Esc is the pause menu in a course and Start elsewhere. Holding Esc for
  a second and letting go asks whether to quit, with Keep playing as the
  answer to Enter and Esc. It used to quit at once, on the key every PC
  game uses for "menu", with a course lost. F11 leaves fullscreen from anywhere. The
  [ and ] keys step the Mouse Speed mid-course.
* A Controls page on the game's Options screen, beside Graphics and Sound,
  in the game's own face: Z Button and Control Stick (the game's two
  settings, moved there from the Options list, which keeps its five rows),
  Mouse Aim, Mouse Speed, Zoom Speed, Camera Tilt, Gyro Aim and Gyro
  Speed, eight rows the page scrolls through six at a time. Changes apply
  live and B cancels, as on the other two pages.
* Gyro aim. A pad with a gyro (DualSense, DualShock 4, Switch Pro and
  Joy-Cons; the Steam Deck's own controls outside Steam's controller
  layer) turns the view as it is turned, at natural scale, whatever the
  zoom; on always, or only while zoomed in; off as shipped. In the Deck's
  gaming mode Steam keeps the gyro and its "As Mouse" setting feeds the
  port's mouse look instead. Outside it the port switches the Deck's
  motion sensor on itself: Steam's client turns it off whenever its
  layout has Gyro set to None and SDL's Deck driver never turns it back
  on, so the readings arrive as zeros; the setting is sent when Gyro Aim
  is turned on and again whenever the readings stay at zero for half a
  second (on a Deck, 2026-09-06, the readings came alive within a dozen of
  the setting).
* Visiting the Options screen enough times froze the game. Every strip the
  port's pages draw -- a label, a value, a help line, an arrow -- was taken
  from the scene's general heap, which is a bump allocator with no free of
  any kind, and the Option screen is a state inside the main-menu scene
  rather than a scene of its own, so nothing between two visits ever moved
  that cursor back. A Graphics visit cost about 5.8 KB of the 1,887,328
  bytes the scene has, and a few hundred visits walked it off the end, where the
  game's allocator branches to itself for ever: the last picture stayed on
  screen, the sound stopped, and nothing answered the controller. The strips
  come from a table of the port's own now, sized by what can be on screen at
  once and reclaimed by what is still reachable, so a visit costs nothing it
  does not give back; a table that ran dry would leave a row blank and say
  so in the log rather than stop the game. The cartridge drew its Options
  screen from static templates and never allocated per visit, so this was
  the port's fault and not the game's.
* Three ways a save could be lost, all in the runtime's own save path. The
  game rewrites its record a sector at a time -- an erase and 128 page
  writes for each of the eight sectors it spans, over a thousand writes for
  a whole save -- and the runtime published the file after 128 of them, or
  after ten milliseconds of quiet, which is part of the way through by
  construction. What reached the disk carried a fresh checksum over
  partly-old bytes, so the game rejected it on the next boot. The same
  publish rotated the previous file into the backup, so both copies could be
  spoiled at once, and a torn file is exactly the right length and reads
  cleanly, so the backup was never consulted at all. Writes still in
  flight when the player quit were dropped. And a save that existed but
  could not be read was replaced by zeros, which the next write committed
  over it. The file is published only once the game's writes have stopped
  now, the exit waits for one still in flight rather than dropping it, the
  Snap Station's relaunch brings the save to disk before it replaces its own
  process, and a save that cannot be read is never written over.
* Widescreen no longer loses Pokémon at the edges. Two things took them.
  The renderer drops a pass whose triangles all landed outside the 4:3
  viewport, measured before the projection is widened, and the game's
  photo detector puts each Pokémon in a pass of its own, so a Pokémon
  entirely in the widened margin was never drawn and popped out just past
  the film counter's end; identified by mstan in pull request 5, and such
  a pass is now kept (the RDRAM writeback it bounds is unchanged). And the
  game decides whether a Pokémon is on screen by projecting its position
  at a fixed 4:3 focal length against fixed pixel bounds, which only bites
  on very wide pictures; a game-side patch widens that bound by exactly
  the factor the renderer applies (a mailbox word the port publishes each
  tick), and leaves the vertical bound and every 4:3 run untouched. Effect
  sprites -- the leaves out of the tall grass, the sparkles, the splashes
  -- have a test of their own inside the effect drawer, against the
  console's picture in the projection's own units; it is widened by the
  same factor, and the drawer's viewport is shifted for the pass, with the
  renderer moving the rectangles back, so a particle left of the console's
  picture is not clamped to its edge. Widescreen is still untested beyond
  the Beach.
* Widescreen widens the course and nothing else. The title, Oak's lab, the
  album, the reports and the credits are drawn for a 4:3 screen -- their
  art is 320 wide with nothing behind it -- and under Widescreen they went
  into a wider picture whose margins nothing repainted, so the last frame
  of a course stayed beside the lab after quitting one, and the lab's
  island spread past the panel that frames it. The renderer now widens
  only while a course's code is loaded; every other screen sits in black
  bars, as it does with the option off. Seen on a Steam Deck.
* The viewfinder keeps the console's frame under Widescreen, and a fade
  covers the whole picture. Raising the camera letterboxes the view to an
  inset of the screen -- black bands thirty pixels wide at the sides, the
  film counter sitting over the right one -- and the renderer had stretched
  that inset with the wider picture, so the world filled the bands and ran
  past the counter. A scissor narrower than its own viewport is a crop the
  game authored in its picture's pixels, and it now keeps its place; the
  view inside is still drawn from the wider render. The fade to black at a
  course's end reached only the 4:3 picture, so the margins held the last
  frame of the ride until Oak's lab was up; its quad now follows the
  renderer's widening and the margins fade with the rest. Both from a
  Steam Deck screenshot.
* On a Steam Deck launched from Desktop Mode with Steam running, a shot
  opened the pause menu and B kept bringing it back: Steam's desktop
  layout sends Enter for A and Escape for B along with the pad's own
  buttons, and Enter is the port's Start, Escape its pause key. While the
  Deck's own controller is attached, those two keys are ignored. Seen on
  a Deck.
* The D-pad walks the menus. The cartridge never reads the D-pad -- only
  its crash screen does -- so the lab, the title and every other menu
  answered to the stick alone; the D-pad, and the arrow keys bound to it,
  now move the stick as well while the stick is at rest. Asked for on
  Reddit.
* The Switch Online N64 controller's buttons land where they belong. SDL
  reports that pad's L and R as the shoulders and its Z as the left
  trigger, the reverse of the port's Xbox-style defaults, so its L acted as
  Z, its Z as L and its R as nothing. A pad whose name says N64 has
  shoulders and triggers change roles; `pad_layout` in the settings file
  forces either layout. Its C buttons arrive as the right stick, which the
  port has always read as C. Reported on Reddit.
* The Options pages no longer change a value on a sideways drift of the
  stick while scrolling. They moved on the game's slow-stick bits, which
  fire at a small deflection in any direction; they read the stick
  themselves now, with a dead band, the dominant axis only, an edge, and a
  slow repeat while it is held. Reported on Reddit.
* A slit of the scene at the picture's right edge beside the viewfinder's
  black bands, ten pixels wide at 2560x1440, is closed. The renderer snaps
  a right-anchored rectangle's edge down to the native pixel grid and
  takes the target's misalignment off it, which left the bands short of
  the edge by an amount that depended on the resolution; an edge that
  reached the picture's right edge now stays on it.
* A stalled audio device cannot crash the game any more. When the device
  stops draining (a sink that never plays, a Bluetooth switch mid-stream,
  a machine back from sleep), the queue the game reads as its DAC backlog
  grew without bound, its frame arithmetic wrapped, and the synthesizer
  overran its command list. Above a second of queued audio the queue is
  dropped and a full buffer reported; a device that goes away is reopened
  on the next buffer.
* A native Linux build, experimental: the same source compiles with Clang
  or GCC, renders through Vulkan and packs as a tarball (BUILDING, step
  14). It has played the Beach replay and drawn correct frames on a
  software Vulkan device under WSL, and on a Steam Deck in Desktop Mode
  the Beach with gyro aim, the menus, the settings and the Snap Station;
  it is on the release page marked experimental. No Linux desktop has run
  it yet. Two portability fixes came from a contributor's macOS build; the
  presented-frame capture and the Snap Station's sheet capture needed a
  texture-to-buffer copy the Vulkan backend lacked. The port's icon, which
  the Windows executable carries as a resource, ships beside the Linux
  binary: the window takes the film canister (`Snap64Recomp-window.png`)
  as the Windows title bar shows it, a Steam shortcut can be pointed at
  the tile (`Snap64Recomp.png`), and a `.desktop` launcher with the tile
  is written beside the binary on the first start, the one way a Linux
  binary shows a logo in a file manager or a menu.
* Steam Deck: the port knows a Deck (Steam's `SteamDeck=1`, or the board
  vendor under `/sys`) and boots fullscreen there; SDL's screen keyboard
  is kept off so Steam's does not open over the game; the Snap Station's
  relaunch on Linux replaces the process in place so Steam keeps the game
  as running. The Windows build under Proton now lets Steam's virtual
  controller through. The README has the Deck instructions for both
  routes and what is untested.

## 1.0.0 -- 2026-09-05

The first release. A native Windows port of Pokémon Snap (US) by static
recompilation; you supply your own cartridge dump, nothing of the game is
included.

What it does, all of it off unless you turn it on except where noted:

* The game as the console ran it: its own code, its own rendering through
  RT64, its own audio and saves, at the console's frame rate by default.
* Frame interpolation to your display's refresh rate (Frame Rate: Display
  or Manual), covering objects, the camera, the game's 2D sprites and menu
  frames, and the fades between screens.
* Widescreen with a true wider field of view, anti-aliasing, super sampling,
  render scale, texture filter and dither choices, overscan crop, an
  in-game Graphics page and a Sound page added to the game's own Options
  screen, and hotkeys for the common ones.
* Photo Detail: Oak's photos, the album and the report served from the
  renderer's full-resolution render instead of the console's halved pixels.
* Jynx Recolor: the purple face and hands of the re-releases, matched to a
  Virtual Console capture.
* Cutscene Fix: the one frame the console drew from inside the player model
  at the end of the Beach and River intros, skipped.
* Photo export: P or the controller's Back button saves the photo on screen
  as a PNG, as the Wii Virtual Console's Message Board post did.
* The Snap Station: the Blockbuster kiosk's sticker printer, emulated on
  controller port 4, reached from a fifth title-menu entry; a print produces
  the sixteen-sticker sheet and the printer's own display, composed from
  the game's captures, and the sheet's folder opens when it is done.
* Mods and texture packs: the runtime's loaders run; nothing ships.

Fixed along the way, for anyone comparing with the console: the renderer
recorded each draw call with the next call's texture state; the game-side
patches' data section was never loaded into memory; the fade quad left a
hairline of the scene along the screen's edge at high resolution; a photo's
transparent void showed the Gallery through it; the Snap Station could race
the game's boot-time printer test; particles that carry their own depth (the
effect system's dust and leaves) passed the depth test against every wall.
All are in the git history with their reasoning.
