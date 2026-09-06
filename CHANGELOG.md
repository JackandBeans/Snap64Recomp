# Changelog

## 1.0.1 -- unreleased

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
  Mouse Aim, Mouse Speed, Zoom Speed and Mouse Tilt. Changes apply live
  and B cancels, as on the other two pages.
* The README says Widescreen is untested beyond the Beach and that the
  game culls by its 4:3 view, so objects at the edges vanish.
* A native Linux build, experimental: the same source compiles with GCC,
  renders through Vulkan and packs as a tarball (BUILDING, step 14). Built
  and started under WSL; not yet run on a Linux desktop or a Steam Deck,
  so it is not on the release page. Two portability fixes came from a
  contributor's macOS build.
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
