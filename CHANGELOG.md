# Changelog

## 1.0.1 -- unreleased

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
  now runs until the game stops it, as the pak did.
* `rumble_strength` in the settings file sets the Rumble Pak's strength from
  0 to 100, and `0` switches rumble off. The cartridge had no such control,
  so the default is full strength.
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
  sprites (sparkles, splashes) have a test of their own and still pop at
  the 4:3 edge. Widescreen is still untested beyond the Beach.
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
  software Vulkan device under WSL; it has not yet run on a Linux desktop
  or a Steam Deck, so it is not on the release page. Two portability fixes
  came from a contributor's macOS build; the presented-frame capture and
  the Snap Station's sheet capture needed a texture-to-buffer copy the
  Vulkan backend lacked.
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
