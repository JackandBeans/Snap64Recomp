# Changelog

## 1.0.4 -- unreleased

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
  side. `[SNAP-OS]` reports a message the game sent without waiting that a
  full queue dropped, twice per queue, for the same reason.
* The texture decode shader declares its output as 8-bit RGBA for Vulkan,
  the format the texture has, instead of the 32-bit float format the
  compiler inferred; the validation layer reported that mismatch seven
  times a run. The mismatches it reports for the framebuffer read-back and
  write-back buffers, whose format follows the framebuffer's depth, need a
  newer shader compiler than the tree's and are left as they were.
* The 2D Detail help and the README say what Sharp for everything
  (`upscale_2d` 2) costs: a line under a logo and a fringe around a keyed
  sprite, seen on a Steam Deck.

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
