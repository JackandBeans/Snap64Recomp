# Changelog

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
  synchronisation detection is improved, and a Vulkan workaround for RDNA4
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
  the heap's pointers are turned towards an arena of the port's own, in
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
  own artwork: the three stock pills are 89x19 full-colour sprites with
  their words baked in, so the port composes a fourth pair, plain and
  selected, out of the yellow pair in the ROM -- the word erased to the
  fill, every texel's hue turned so the fill sits at green (the one colour
  of the set the menu does not use), and Options set in the pills' own
  letters at the pills' own centring, the O being the Q of Quit Course
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
  initialised it; and `tools/macos_bundle.py`, which lays out the
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
  small colour render as a possible photo, the interface's 8x8 and 16x16
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
  corner kept whatever the colour buffer held before. Settled with the
  driver's own switches on my Deck: `RADV_DEBUG=nonggc` (that
  culling alone off) draws the frame whole, while its synchronisation,
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
  mappings (SDL_GameControllerDB, zlib licence, `licenses/`), beside the
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
* The renderer no longer drops a Pokemon's pass because its triangles
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
* A pad SDL does not recognise can be taught with `gamecontrollerdb.txt`,
  the community's own mapping list, dropped beside the executable; the port
  reads it at start-up and reports how many mappings it added. The log line
  for an unrecognised pad now says so. What used to be offered instead was
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
