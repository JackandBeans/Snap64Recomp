<p align="center"><img src="docs/logo.png" width="640" alt="Snap64 Recomp"></p>

<p align="center">
<a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue" alt="License: GPLv3"></a>
<a href="https://github.com/JackandBeans/Snap64Recomp/releases/latest"><img src="https://img.shields.io/github/v/release/JackandBeans/Snap64Recomp?label=release" alt="Latest release"></a>
<img src="https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey" alt="Platform: Windows 10 or 11, x64">
</p>

# Snap64 Recomp

By JackandBeans. A native Windows port of the Nintendo 64 game *Pokémon Snap*
(US release), made by static recompilation.
[N64Recomp](https://github.com/N64Recomp/N64Recomp)
translates the game's MIPS code into C, [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
(`librecomp` + `ultramodern`) stands in for the console's operating system,
[RT64](https://github.com/rt64/rt64) renders, and SDL2 provides the window,
input and audio. The game's own code runs; the port changes how it is hosted,
and every change to how it *looks* is off unless you turn it on.

This project is not affiliated with, endorsed by or connected to Nintendo,
Creatures Inc., GAME FREAK inc., HAL Laboratory or The Pokémon Company;
Pokémon and Pokémon Snap are their trademarks, and the game is theirs. No
game data is included: you supply your own cartridge dump. The executable
does contain the game's code, translated from the builder's own dump into C
by N64Recomp and compiled, as every N64Recomp port does; `NOTICE.md` says
exactly what is derived from the game and how.

The title screen's credits line, `JackandBeans (Snap64 Recomp) · v1.0.1`, is
the author's name, the port's name and its version; the name comes from the
HAL team that made the game ([The game, and its history](#the-game-and-its-history)).
The people and projects this port stands on are thanked under [Thanks](#thanks).

**Contents:** [Get it running](#get-it-running) ·
[Screenshots](#screenshots) ·
[What you need](#what-you-need) · [Running](#running)
([where things live](#where-things-live), [controls](#controls),
[the rule the port follows](#the-rule-the-port-follows),
[in-game pages](#in-game-pages), [hotkeys](#hotkeys), [photos](#photos),
[the Snap Station](#the-snap-station),
[mods and texture packs](#mods-and-texture-packs),
[settings file](#settings-file)) ·
[Known limitations](#known-limitations) · [What's next](#whats-next) · [Status](#status) ·
[What has been verified](#what-has-been-verified-and-what-has-not) ·
[Building](#building) · [The game, and its history](#the-game-and-its-history) ·
[How it was made](#how-it-was-made) · [Thanks](#thanks) · [License](#license)

## Get it running

> **In a hurry?** The archive holds a `START HERE.txt` with the short version.
> The one part people miss: this port has no launcher and no
> overlay, so everything it adds lives inside the game's own **Options**
> screen. Options > Graphics is where widescreen, the frame rate and the rest
> of the enhancements are, and they all start off, set to what the console
> did.

You need a 64-bit Windows 10 or 11 PC whose graphics driver provides
Direct3D 12, and your own dump of the US cartridge; nothing has to be
installed. Then:

1. Download `Snap64Recomp-1.0.1-win64.zip` from the
   [Releases](https://github.com/JackandBeans/Snap64Recomp/releases/latest)
   page and unpack it anywhere; it holds one folder,
   `Snap64Recomp-1.0.1-win64`, with `Snap64Recomp.exe` inside.
2. Put your own dump of the US cartridge (the ROM: the cartridge's contents
   read out into one file) next to `Snap64Recomp.exe`, named
   `pokemonsnap.z64`. You do not have to check the file yourself: a missing
   or wrong one is reported in a dialog before the window opens, with the
      expected and the actual checksum (a 64-bit hash of the file, not the SHA-1
   under "What you need").
3. Start `Snap64Recomp.exe`. Because the executable is not signed, Windows
   may first show a "Windows protected your PC" box (SmartScreen): click
   "More info", then "Run anyway", and it will not ask again. The first start
   then takes a little longer than later ones while the renderer builds the
   shader programs your GPU needs; they are kept in `cache/`, so the next
   start is quick.

The window's maximize button switches to fullscreen and F11 switches it
back, in a course or anywhere else. **Esc is the pause menu** in a course
(Continue, Retry, Quit course) and Start elsewhere. To quit the program,
**hold Esc for a second and let go**: a box asks, and Enter or Esc keeps
you playing, so a hand resting on the key cannot end a run. Keyboard and
controller mappings are under
[Controls](#controls); the Graphics and Sound pages are on the game's own
Options screen, reached from the title menu ([In-game
pages](#in-game-pages)); saves live in `saves/` next to the executable, so
keep that folder when you update. If something goes wrong, the paragraphs
under [Running](#running) say what Windows or an antivirus may object to
and what to attach to a bug report.

## Screenshots

<table><tr>
<td><a href="docs/screenshots/01-title.png"><img src="docs/screenshots/01-title.png" width="400" alt="The title screen"></a></td>
<td><a href="docs/screenshots/03-course-select.png"><img src="docs/screenshots/03-course-select.png" width="400" alt="Course select"></a></td>
</tr><tr>
<td><a href="docs/screenshots/04-beach.png"><img src="docs/screenshots/04-beach.png" width="400" alt="The Beach: Surfing Pikachu"></a></td>
<td><a href="docs/screenshots/06-volcano.png"><img src="docs/screenshots/06-volcano.png" width="400" alt="The Volcano: Charizard's Flamethrower"></a></td>
</tr><tr>
<td><a href="docs/screenshots/16-oak-check-close.png"><img src="docs/screenshots/16-oak-check-close.png" width="400" alt="Oak's check: the chosen Pikachu photo"></a></td>
<td><a href="docs/screenshots/09-graphics.png"><img src="docs/screenshots/09-graphics.png" width="400" alt="The port's Graphics page"></a></td>
</tr><tr>
<td><a href="docs/screenshots/11-gallery-print.png"><img src="docs/screenshots/11-gallery-print.png" width="400" alt="The Gallery with the Snap Station's Print"></a></td>
<td><a href="docs/screenshots/13-printer-stars.png"><img src="docs/screenshots/13-printer-stars.png" width="400" alt="The printer's display, three stars"></a></td>
</tr></table>

Taken from the 1.0.0 build, the Options screen and the Controls page from
1.0.1, at 1440p with Render Scale and Anti-Aliasing both
at 8x on the Graphics page, cropped to the game's picture. All nineteen, with
a caption each (the title menu, the Tunnel, the lab, the Options and Sound
pages, the printer's marks, Oak's check from the photo choice to the score
sheet, the Camera Check), are in [docs/SCREENSHOTS.md](docs/SCREENSHOTS.md).

## What you need

* A 64-bit Windows 10 or 11 PC (the port asks Windows for per-monitor DPI
  awareness, which needs Windows 10 version 1703 or later). The executable
  imports `d3d12.dll`, `dxgi.dll` and `d3dcompiler_47.dll` from Windows, so
  the GPU driver must provide Direct3D 12; `vulkan-1.dll` is loaded only if
  you switch the renderer to Vulkan (`graphics_api` in the settings file,
  "Settings file" below). The Visual C++ runtime is linked into the
  executable; nothing else has to be installed.
* **Your own dump of the US cartridge**, whose SHA-1 checksum (a fingerprint
  of the file's contents) is `edc7c49cc568c045fe48be0d18011c30f393cbaf`, the
  value the [decompilation project](https://github.com/ethteck/pokemonsnap)
  publishes. Name it `pokemonsnap.z64` and put it next to `Snap64Recomp.exe`
  (the port reads its own folder, not the working directory; see "Where
  things live"). A dump saved as `.v64` or `.n64` (the same data in another
  byte order) works too: the port detects the order from the file's header
  and corrects it in memory without touching the file. The file name is
  fixed, though, so rename such a dump to `pokemonsnap.z64`. A file that is
  missing, cannot be read or is another revision of the game produces a
  dialog before the window opens; a wrong dump shows both the expected and
  the actual hash (a 64-bit hash of the whole file, not the SHA-1 above), so
  you need not compute anything yourself. The ROM is never
  included with this project.
* Beside `Snap64Recomp.exe`: `SDL2.dll`, `dxcompiler.dll` and `dxil.dll`,
  and optionally `menu_text/recomp_logo.png` for the "Recomp" badge under the
  title logo (no file, no badge). The release ZIP already holds all of them;
  if you build the port yourself, the build places them there
  (`BUILDING.md`, step 12).

## Running

Start `Snap64Recomp.exe`; a shortcut works from anywhere, because the port
reads and writes the folder the executable is in, whatever the working
directory (`src/paths.cpp`). It opens a 1280x960 window titled
`Snap64 Recomp 1.0.1`; `SNAP_WINDOW=WxH` in the environment opens it at
an exact size instead (at least 320x240). The window's maximize button is the
fullscreen switch; the in-game Graphics page and F11 do the same, and F11
is the way out of fullscreen from anywhere. **A tap of Esc is Start** (the
game's own pause menu in a course, Start on any other screen). **Holding
Esc for a second and letting go asks whether to quit**; Keep playing is
the answer to Enter and to Esc, and Quit takes a click or Tab and Enter.
The window's close button and Alt+F4 quit at once, as any window's do.

Saves go to `saves/` and settings to `snapsettings.json`, both next to the
executable. No console opens: the log is `snap64.log` next to the
executable, and the previous run's log is kept as `snap64.prev.log`.
`snap64.log` is the first thing to include in a bug report. Started from a
terminal, or with its output redirected, the port writes the log there
instead and the file is not touched.

A second copy started while the first is running, from any folder, waits up
to 25 seconds for it to exit (the Snap Station relaunches itself that way)
and otherwise tells you the port is already running.

**If Windows or your antivirus objects.** The executable is not signed, so
the first start may bring up SmartScreen's "Windows protected your PC";
"More info", then "Run anyway", is the route, once. The Snap Station's Print
starts a fresh copy of the port twice in a row, which some antivirus
heuristics dislike; allow it if asked. Nothing here phones home: the port
opens no network connection at all.

**Back up your save.** `saves/pokemonsnap.bin` is the whole of your progress
in one file, with one earlier generation kept as `.bak`. Copy `saves/`
somewhere else before updating the port or trying a Snap Station print.

**Reporting a bug.** Open an issue at
[github.com/JackandBeans/Snap64Recomp/issues](https://github.com/JackandBeans/Snap64Recomp/issues)
(the repository this README came from) and attach `snap64.log` from the run
that went wrong, `Snap64Recomp.map` if the log has `[SNAP-AV]` lines, your
`snapsettings.json`, and what you were doing. Say which GPU and driver you
have; every run so far has been on one machine. The issue form asks for
these; [CONTRIBUTING.md](CONTRIBUTING.md) has the ground rules for code.

### Where things live

Everything is in the folder with the executable.

| File or folder | What it is |
| --- | --- |
| `pokemonsnap.z64` | your ROM (you provide it) |
| `snapsettings.json`, `snapsettings.json.bak` | settings, written by the in-game Graphics and Sound pages and by the hotkeys |
| `saves/pokemonsnap.bin`, `saves/pokemonsnap.bin.bak` | the game's save data, one file (a raw image of the cartridge's save memory) |
| `photos/` | the photos you save with P or the controller's Back button (see "Photos"); created on the first save |
| `cache/` | RT64's compiled shaders and the driver's pipeline cache, built on your machine, and `rt64-seen-shaders.bin`, the list of every shader the game is known to ask for -- shipped with 623 entries from a full playthrough, so the first start compiles them all during the boot logos rather than the first time each appears in play; the game adds any it meets that are not on it. Safe to delete; the next start is slower |
| `snap64.log`, `snap64.prev.log` | the log of this run and of the one before it, written when the port was not started from a terminal |
| `mods/`, `mod_config/` | the runtime's mod folders; the loader runs at every start, no mod ships with this release, and there is no in-game mod manager (see "Mods and texture packs") |
| `texture_packs/` | HD texture packs you install yourself, scanned once at start-up; created empty, none ships with this port (see "Mods and texture packs") |
| `stickers/` | the sticker sheets the Snap Station prints (see "The Snap Station"); created on the first print |
| `menu_text/recomp_logo.png` | the "Recomp" wordmark on the title screen |
| `SDL2.dll`, `dxcompiler.dll`, `dxil.dll` | the window, input and audio library, and the shader compiler and validator the renderer needs; leave them beside the executable |
| `Snap64Recomp.map` | the linker map; include it with crash reports (the `[SNAP-AV]` lines in the log are decoded against it) |
| `LICENSE`, `NOTICE.md`, `licenses/` | licences |

Coming from an earlier build: settings, saves and the ROM were already next to
the executable and carry over as they are. Earlier builds kept the shader cache
in `%LOCALAPPDATA%\pokemonsnap`; that folder is no longer read and can be
deleted. The first start after the change rebuilds the cache once.

### Controls

Keyboard and mouse (`src/input.cpp`), as the port ships them:

| N64 | Key | Mouse | In a course |
| --- | --- | --- | --- |
| Control stick | W A S D | moving the mouse | aims the camera |
| A | X | left button | the photo when zoomed, an apple when not |
| B | Z | middle button | the pester ball |
| Z | Left Shift | right button | zoom (hold or switch, the game's own option) |
| R | E | | dash |
| L | Q | | |
| C-Down | K | wheel down | the Poké Flute |
| C-Up | I | wheel up | turn to face behind |
| C-Left / C-Right | J / L | side buttons (back / forward) | turn left / right |
| Start | Enter, or a tap of Esc | | pause |
| D-pad | Arrow keys | | |

The mouse's buttons work whenever the window has focus: a click is A, so
it advances Oak's text and confirms a menu, and the rest are the buttons
above. The click that gives the window focus does not count. Its motion
aims only while a course runs: then the cursor is captured and hidden and
moving the mouse turns the view, the way a mouse does in any first-person
game, each pixel an angle added to the game's own view; everywhere else
(the title, the lab, Oak's check, the menus) the cursor is free and moving
it does nothing. Zoomed in, the same motion turns half as far, since the
view is narrower. The view moves no faster than you move the mouse and
stops when your hand does; the stick's own turning speed does not apply. The Controls page on the
game's Options screen holds the dials: Mouse Aim, Mouse Speed, Zoom Speed
and Camera Tilt (they are `mouse_aim`, `mouse_sensitivity`,
`mouse_zoom_speed` and `mouse_invert_y` in the settings file).

A pad with a gyro can aim the same way: turn the pad and the view turns
with it, at natural scale, so a ten-degree turn of the pad is a
ten-degree turn of the view, whatever the zoom, the way a real camera
follows your hands. **Gyro Aim** on the Controls page turns it on (On:
whenever a course runs; Zoomed: only while zoomed in, the way a
photographer raises the camera to aim), **Gyro Speed** scales it, and
Camera Tilt flips the vertical for the gyro as it does for the mouse. It
is off as shipped. The pads whose gyro reaches the port are the ones SDL
reads it from: DualSense, DualShock 4, Switch Pro and Joy-Cons, over USB
or Bluetooth, and the Steam Deck's own controls when Steam's controller
layer is not in between (see "Linux and Steam Deck"); an Xbox pad has no
gyro. The log says at start-up whether the pad it opened has one
(`[SNAP-Input] Opened game controller: ... (gyro: yes)`), and once the gyro
is on, whether its readings carry any turning. On a Steam Deck the port
also switches the controller's motion sensor on itself: Steam's client
turns it off whenever the active controller layout has Gyro set to None
(the desktop layout does) and the controller stays that way, while SDL's
Deck driver never turns it back on, so a pad that says "gyro: yes" reads
zero forever. The port sends the sensor's on-setting when Gyro Aim is
turned on and again whenever the readings stay at zero for half a second
(`[SNAP-Input] gyro: the Deck's IMU-on setting ...`); on a Deck the
readings came alive within a dozen of the setting, and when something
switched the sensor off again a minute later (Steam's client, when it is
running) the port switched it back.

Every keyboard, mouse and controller binding can be changed. The settings
file's `keys` table names each input (`a`, `b`, `z`, `start`, `l`, `r`, `c_up`,
`c_down`, `c_left`, `c_right`, `d_up`, `d_down`, `d_left`, `d_right`,
`stick_up`, `stick_down`, `stick_left`, `stick_right`) and lists what
presses it: SDL key names such as `"X"`, `"Left Shift"`, `"Return"`,
`"Space"`, or the mouse names `"Mouse Left"`, `"Mouse Right"`,
`"Mouse Middle"`, `"Mouse X1"`, `"Mouse X2"`, `"Wheel Up"`, `"Wheel Down"`,
or a controller's, written as `"Pad "` and SDL's own name for the button or
axis: `"Pad A"`, `"Pad B"`, `"Pad X"`, `"Pad Y"`, `"Pad Start"`, `"Pad Back"`,
`"Pad LeftShoulder"`, `"Pad RightShoulder"`, `"Pad LeftStick"`,
`"Pad RightStick"`, `"Pad DPUp"`, `"Pad DPDown"`, `"Pad DPLeft"`,
`"Pad DPRight"`, `"Pad LeftTrigger"`, `"Pad RightTrigger"`. So a player who
wants Z on the left trigger rather than the left shoulder writes
`"z": ["Left Shift", "Mouse Right", "Pad LeftTrigger"]`.
The file is written with the whole table in it after the first start, so
editing is a matter of changing a name; a name the port cannot resolve is
reported in the log and skipped, and an input left with nothing usable
keeps its default. The keys are positional scancodes: on a non-QWERTY
layout `"X"` is the key in X's place, not the letter printed on it.

Any SDL game controller overrides the keyboard while attached: left stick is
the control stick, A is A, B or X is B, the left shoulder button is Z, Start
is Start, the D-pad is the D-pad, the triggers are L and R, and the right stick
is the C buttons. The Back button (Select, View or Share on most pads) is not
an N64 button: it saves the photo on screen, as P does on the keyboard (see
"Photos"). The eight buttons -- A, B, Z, Start, the D-pad's four, L and R --
are each a row in the settings file's `keys` table and can be moved; the two
sticks and the Back button are not, and keep the mapping above.

**Which controllers work.** Anything SDL2 has a mapping for, which is most of
what is sold: Xbox pads (360, One, Series) over USB or Bluetooth, PlayStation
(DualShock 3 and 4, DualSense), Switch Pro and Joy-Con, the Steam Deck's own
controls, the Steam Controller, and a long tail of third-party pads. Rumble
works where the pad has it, and the game is told a Rumble Pak is present while
a pad is attached. Gyro aim needs a pad with a motion sensor: DualShock 4,
DualSense, Switch Pro, and the Steam Deck. A pad shaped like the N64's -- the
Switch Online N64 controller, over Bluetooth or USB -- is recognised by its
name, and its L, R and Z are L, R and Z, its C buttons the C buttons; the
`pad_layout` key forces either layout. The D-pad walks every menu as the
stick does, since the cartridge never reads it.

The first pad SDL recognises is the one used. A pad SDL has *no* mapping for
is named in the log at start-up (`[SNAP-Input] joystick ... has no game
controller mapping`) and does nothing until it is taught. Two ways to teach
it, easiest first:

* Put [`gamecontrollerdb.txt`](https://github.com/mdqinc/SDL_GameControllerDB)
  next to the executable (`Snap64Recomp.exe` on Windows). It is the
  community's list of pad mappings; the port reads it at start-up and says how
  many it added. Nothing else to do.
* Or set SDL's `SDL_GAMECONTROLLERCONFIG` environment variable to a single
  mapping string, which is what SDL itself documents.

### The rule the port follows

**Console behaviour by default; every enhancement is opt-in.** Frame rate,
aspect ratio, anti-aliasing, overscan, the intro's camera hand-off, texture
filtering, dithering: all start as the console had them. What you turn on in
the in-game **Graphics** page (a new item on the game's own Options screen) or
with the hotkeys is what changes, and only that. Three defaults are worth
knowing about because they are not literally the console's; the key in
parentheses after each is its name in the settings file ("Settings file"
below). The 3D render resolution follows the window (`resolution_scale` 0;
set 1 for 320x240). 2D content that would be scaled anyway is drawn sharp
(`upscale_2d` 1; set 0 for the original pixels). The finished frame is put
on screen with RT64's anti-aliased pixel scaling rather than raw nearest
pixels (`present_filter` 2; set 0 for the blocks). Each is one setting away
from the original.

Two mechanics depend on the game reading back its own rendered frame: photo
scoring re-renders the photographed Pokémon and counts pixels, and the
viewfinder's red focus dot is found by copying tiles of the colour buffer.
Frame interpolation (Frame Rate set to Display or Manual) presents frames the
game never drew; the window title says `interpolation ON (F8)` while it is on.
Colours the game steps once per frame are blended too. The fade to black
between screens is a full-screen quad whose alpha the game moves once per
tick; when that draw is matched to the same draw in the previous frame, the
interpolation blends the colour between the two, so the fade moves at the
display's rate like everything behind it.
Photo scoring was measured working with interpolation on (five photos, the
game's own pixel counts reproduced exactly), because the readback uses the
frames the game draws, not the synthetic ones between them. The focus dot
under interpolation has not been re-measured and should be treated as
unverified. Original, the Frame Rate row's first choice, is the default
because it is the console's rate.

### In-game pages

Options > **Graphics**, in the order the page shows them: Render Scale,
Super Sampling, Anti-Aliasing, Widescreen, Frame Rate, 2D Detail, Filter,
Texture Filter, Color Depth, Buffering, Dither, Fullscreen, Overscan Crop,
Cutscene Fix, Photo Detail, Jynx Recolor. Color Depth and Buffering take
effect after a restart; everything else applies while the page is open. The
page's Frame Rate row switches between Original and Display; the Manual
mode (`fps_mode` 2) is reached with F8 or the settings file, and the page
leaves it alone.

Options > **Sound**: Master Volume, Music Volume, Sound Effects, Shutter
Volume, Speaker Output (Stereo/Mono), Background Mute.

Options > **Controls**: Z Button (Hold/Switch) and Control Stick
(Normal/Reverse), the game's own two settings, moved here from the Options
list so the list keeps its five rows; then Mouse Aim, Mouse Speed (25 to
400 percent of the shipped speed), Zoom Speed (the share of that speed
used while zoomed in), Camera Tilt (Normal/Reverse, for the mouse and the
gyro alike), Gyro Aim (Off, On, or Zoomed for only while zoomed in) and
Gyro Speed (25 to 400 percent of natural). Eight rows, six on screen; the
page scrolls for the last two, as the Graphics page does. Every change
applies as it is made; B puts the page back as it was opened.

### Hotkeys

The hotkeys are handled in `handle_settings_hotkey` in `src/settings.cpp`
(Esc in `src/main.cpp`). A hotkey that changes a setting also
marks `snapsettings.json` for writing. Keys marked *diagnostic* exist for
investigating the renderer and are not features.

| Key | Effect |
| --- | --- |
| F11 | Fullscreen on/off |
| F10 | Widescreen on/off |
| F9 | Anti-aliasing: off, 2x, 4x, 8x, off |
| F8 | Frame Rate mode: Original, Display, Manual |
| F7 | Speaker output: stereo/mono |
| F5 | Write `snapsettings.json` now |
| F4 | Camera interpolation on/off (see `src/settings.h` for why it is on) |
| F3 | *diagnostic* Ubershaders only |
| F2 | Overscan Crop on/off |
| F1 | *diagnostic* Frame holds inside a course |
| F6 | *diagnostic* Render-to-RAM on/off; inert unless started with `SNAP_STATS=1`, never saved |
| F12 | *diagnostic* Mark the moment in the statistics log (needs `SNAP_STATS=1`) |
| Home | *diagnostic* 2D rectangle interpolation on/off |
| End | *diagnostic* Effect-sprite naming on/off |
| [ / ] | Mouse Speed down / up, through the Controls page's steps |
| P | Save the photo on screen as a PNG in `photos/` (see "Photos") |
| Esc | Tap: Start (the pause menu in a course). Held a second and released: the quit question |

### Photos

**P**, or the controller's **Back** button, saves the photo on screen as a PNG:
the photo at the game's own resolution, pixel for pixel, with no scaling, no
frame and no text over it. Files go to `photos/` next to the executable, named
`snap_YYYYMMDD_HHMMSS_<course>_NN.png` (the course is left out if the game's
own record of it cannot be read), and the log prints `[SNAP] photo saved:
<path>` or the reason it was not: no photo has been rendered yet, no photo is
on screen, or render-to-RAM is off.

What is saved is the game's own buffer. Every photo the game shows you (the
picks after a course, Oak's check, the album, the report) is drawn the same
way: the game rebuilds the photo's saved state as objects and renders them
once into a 320x210 buffer in memory (the size it asks for varies by screen,
up to that), then shows that buffer as a sprite. With render-to-RAM on, which
it always is in ordinary play (only the F6 diagnostic under `SNAP_STATS=1`
turns it off), the rendered pixels are written back into that buffer, which is
what lets the game score photos at all, and the export writes that buffer's
rendered region out. Nintendo's 2007 Wii Virtual Console release added the
same thing (Select in the album posted the photo on screen to the Wii Message
Board), so this is an enhancement with a precedent, and one that draws nothing
on screen. The code is `src/photo_export.cpp`.

### The Snap Station

The port emulates the Pokémon Snap Station's printer on controller port 4.
The station was the kiosk of 1999 and 2000 -- in Blockbuster Video stores in
North America, Lawson convenience stores in Japan, and Myer department
stores in Australia -- that printed a player's photos as a sheet of sixteen
stickers; inside it a Nintendo 64 with the Expansion Pak ran the ordinary
retail cartridge, and the printer sat on controller port 4, where the game
speaks to it as if it were a Controller Pak. Every retail cartridge carries
the code, and the protocol was recovered without a station by James Chambers
in 2021 and matches the decompilation line for line. The kiosk's own story
(the cards, the prices, how many were built) is under
[The game, and its history](#the-game-and-its-history).

It is reached from the title screen. Once the saved report holds more than
three species, the game adds its Gallery entry to the title menu, and the
port adds a fifth entry below it, **Snap Station**, drawn in the title's
own lettering. Choosing it attaches the station to port 4 for this run and
opens the game's own Gallery, exactly as the Gallery entry does; nothing is
written to the settings. The console at home had no station, so port 4
is empty otherwise. `"snap_station": true` in `snapsettings.json` keeps the
station attached on every start instead, from the moment the title menu is
up. It cannot be attached earlier: the game tests port 4 for the printer
once at boot and would go straight to the printer's display if it found one,
so the station appears after that test.

In the Gallery with the station attached, the game shows the Print button
the kiosk showed, above Save, with the game's own help text about a print
credit. Print does what it did in
the store: the game saves the four photos of its print tray to the
cartridge (the tray is the Arrange screen's four cells, which the Camera
Check fills with the photos Oak accepts), asks the station to reset the
console, and the port relaunches itself. The relaunched game finds the
station present at boot, tests the Expansion Pak memory as the kiosk
firmware required, and runs its photo display mode: a 640x480 screen that
draws the sixteen sticker slots one after another, each of the four photos
in a 2x2 block of a 4x4 sheet, the layout being the game's own table. The
kiosk's printer captured the video output at each slot; the port captures
the framebuffer the video interface is scanning out, which is the same
picture, and the renderer's presented frame beside it. Each slot is the
game's own composition: a white card and the photo filling it but for a
hem; on the fourth slot alone the game draws its black rights line along
the bottom, for no reason any source explains. When the display
ends the sixteen captures are laid out into `stickers/<date>/sheet.png`
(and `sheet_presented.png` from the renderer's frames, with the sixteen
singles in `slots/`).

The kiosk's printer had a screen of its own, laid over the video: after each
slot it showed the sticker grid it had collected so far, and after the last
one that grid under "PRINTING... PLEASE WAIT" with three marks that became
stars one by one as its three passes finished. The port shows the same,
composed from its captures (the grid is kept as `printer_display.png`). The
pass times are an estimate, because the footage they were taken from
(Leonhart's recording of a working kiosk, [Thanks](#thanks) below) has no
clock.

Then the port relaunches itself once more into a normal boot, as the kiosk
reset the console a second time. That boot opens
the sheet's folder for you, the way the kiosk handed over the stickers; the
station is not attached to it, so the title is the ordinary one until you
choose Snap Station again. Both relaunches come back fullscreen if the
print was started fullscreen. The lettering on that screen is set from
bitmaps of Roboto Regular (Apache License 2.0), a freely licensed grotesque
of the same construction as the printer's own, which cannot be read off a
recording of a curved screen; `tools/osd_font_gen.py` regenerates them.

What the sheet cannot be: the physical stickers were postage-stamp-sized
prints of a captured analog video signal, made on a photo printer whose make,
media size and colour processing the public sources do not agree on. So the
files are the pixels the game sent, at their native size and in the layout the
game defined, not a scan of a Blockbuster sheet. Nothing of the kiosk ships
with the port; every pixel on the sheet is the player's own photo drawn by the
game from the player's own save.

### Mods and texture packs

Two loaders are compiled in, and both run on every start; neither has any
content attached. The runtime this port is built on scans `mods/` for `.nrm`
mod containers, the format the other N64 recompilation projects use, and
loads the ones enabled in `mod_config/mods.json`; a mod must target the game
id `pokemonsnap`, and there is no in-game manager, so `mods.json` is the
whole of the control. The renderer scans `texture_packs/` for RT64
replacement packs, a `.rtz` archive or a folder carrying an `rt64.json`,
loads them in alphabetical order with later packs winning, and does it once
at start-up, so the folder's contents are the switch. `README` files in both
folders say the same.

Nothing is bundled and nothing is curated. No texture pack exists for this
game in RT64's format today, and no mod community exists for it, so the
port links the format's specification rather than a list: RT64's
`TEXTURE-PACKS.md`, and the `texture_hasher` and `texture_packer` tools in
`lib/rt64/src/tools/`. Making a pack needs RT64's developer mode to dump
the textures a pack is keyed on; `SNAP_DEV=1` in the environment turns it
on for a launch. RT64 then takes F1 to F4 for its own panels, so Overscan
Crop's F2 and camera interpolation's F4 belong to it until the next launch.

The one-ROM rule: this port transforms the one cartridge you supply, and
that is the whole of it. Assets taken from another game's data, models from
another title, or a pack made from them are not something this project will
host, link, or help install.

### Settings file

`snapsettings.json` is written about 750 ms after the last change (so a slider
costs one write), and on exit if anything is unsaved; the previous file is
kept as `snapsettings.json.bak` and read if the main file is missing or does
not parse. Keys are the field names of `struct Settings` in `src/settings.h`;
the defaults below are that file's.

| Key | Default | Meaning |
| --- | --- | --- |
| `fullscreen` | `false` | not persisted across runs: every boot starts windowed, except that the Snap Station's own relaunches return in the state the print started in |
| `widescreen` | `false` | RT64 Expand: a true 16:9 field of view, not a stretch, in a course; the title, the lab and the other 4:3 screens sit in black bars. Pokémon and effect sprites at the edges are kept ("Known limitations") |
| `msaa` | `0` | 0, 2, 4 or 8 |
| `fps_mode` | `0` | 0 Original, 1 Display refresh, 2 Manual (`fps_manual_target`) |
| `fps_manual_target` | `120` | |
| `stereo` | `true` | the game's own Stereo/Mono flag (`hq_sound` is read as a legacy name) |
| `master_volume`, `music_volume`, `sfx_volume`, `shutter_volume` | `100` | percent, in steps of ten |
| `mute_unfocused` | `false` | silence while another window has focus |
| `three_point_filtering` | `true` | the console's texture filter (Texture Filter: Authentic) |
| `crop_enabled` | `false` | Overscan Crop |
| `crop_left`, `crop_right`, `crop_top`, `crop_bottom` | `16`, `16`, `12`, `12` | pixels hidden per side when the crop is on |
| `intro_fix` | `false` | Cutscene Fix: skips the one frame the console drew from inside the player model at the end of the Beach and River intros |
| `photo_detail` | `false` | Photo Detail: Off draws Oak's photos and the album at native pixels as the console did; On serves them from the renderer's full-resolution render |
| `jynx_vc` | `false` | Jynx Recolor, Jynx's face and hands: Off: the cartridge's black; On: the purple of the re-releases, matched to a published Virtual Console screenshot |
| `interpolate_camera` | `true` | interpolate the view as well as objects (F4; no row on the Graphics page) |
| `snap_station` | `false` | keep the Snap Station on port 4 from the title menu on, every start (see "The Snap Station") |
| `rumble_strength` | `100` | the Rumble Pak's strength, 0 to 100; `0` switches rumble off |
| `pad_layout` | `0` | how a pad's shoulders and triggers are read: `0` decides by the pad's name, `1` the Xbox-style layout the defaults describe, `2` an N64-shaped pad (the Switch Online N64 controller), whose L, R and Z are L, R and Z |
| `mouse_aim` | `true` | the mouse aims while a course runs and the window has focus ("Controls"); its buttons work whenever the window has focus, through `keys` |
| `mouse_sensitivity` | `1.0` | angle per pixel of mouse: 1 is a full turn in about 2500 pixels, 2 twice as quick, 0.5 half; 0.1 to 10 |
| `mouse_invert_y` | `false` | mouse forward, or the pad's front rising, tilts the view down (Camera Tilt: Reverse) |
| `mouse_zoom_speed` | `0.5` | the mouse's speed while zoomed in, as a share of `mouse_sensitivity`; 0.25 to 1 (Zoom Speed) |
| `gyro_aim` | `0` | the pad's gyro aims the camera: 0 off, 1 in a course, 2 only while zoomed in (Gyro Aim) |
| `gyro_sensitivity` | `1.0` | multiplies the gyro's natural scale; 0.25 to 4 (Gyro Speed) |
| `keys` | the table under "Controls" | what presses each input: SDL key names, the mouse names, and `"Pad "` plus SDL's controller button or axis name; one or a list |
| `graphics_api` | `0` | 0 Direct3D 12 (every run so far), 1 Vulkan (RT64's other backend, untried here; an escape hatch if D3D12 fails); restart |
| `downsample` | `1` | Super Sampling factor |
| `resolution_scale` | `0` | 0 follows the window; 1-8 caps the render scale in multiples of 320x240 |
| `present_filter` | `2` | 0 nearest, 1 linear, 2 RT64's anti-aliased pixel scaling |
| `upscale_2d` | `1` | 0 original pixels, 1 only content that scales anyway, 2 everything sharp |
| `dither_noise` | `true` | the console's post-blend dither |
| `color_depth` | `0` | 0 RT64 decides, 1 console-accurate 8-bit, 2 high precision; restart |
| `triple_buffering` | `false` | restart |
| `ubershaders_only` | `false` | diagnostic |

`render_to_ram` is never read from or written to the file.

Environment variables the executable reads: `SNAP_WINDOW` (window size),
`SNAP_STATS` (statistics and the diagnostic keys), `SNAP_MUTE`, `SNAP_RECORD`
and `SNAP_REPLAY` (input recording and replay), the `SNAP_PCAP_*` family
(presented-frame capture; `SNAP_PCAP_ATFRAME` keys captures to game frames,
which do not drift between runs the way input readings do),
`SNAP_PHOTO_AUTOEXPORT` (with `SNAP_STATS`: every photo the game renders is
saved to `photos/` without a key press, so a replay can prove the export),
`SNAP_ZDUMP_EVERY` and `SNAP_ZDUMP_START` (with `SNAP_STATS`: the game's
z-buffer dumped from memory on a reading schedule), `SNAP_FX_TAGS=0` (the
effect system's rectangles left unnamed), `SNAP_DEV` (RT64's developer
mode, "Mods and texture packs") and `SNAP_MENU_FONT_DUMP` (the harvested
menu font written out). They are development switches; the source is their
documentation.

### Every course open

The game opens its courses in its own order, and that order is the game;
the port does not change it and has no switch that does. But a save with
every course already open has real uses -- checking a later course on a new
machine, a stream that wants the Volcano without an hour of Beach first, a
save that was lost -- so the repository carries the tool that makes one,
`tools/unlock_save.py`. It reads a copy of your `saves/pokemonsnap.bin`,
sets the highest-unlocked course to Rainbow Cloud, gives the Apple, the
Pester Ball, the Poké Flute and the Dash Engine, marks the tutorial done,
and writes the result with the game's own checksum; your Pokémon Report,
your album and the ending flags are left exactly as they were, so the report
still fills as you earn it. It needs only Python 3:

    python tools/unlock_save.py saves/pokemonsnap.bin unlocked.bin

Then, with the game closed, keep your original somewhere safe and put
`unlocked.bin` in `saves/` as `pokemonsnap.bin`. The tool refuses a file
whose checksum it cannot reproduce, so it cannot be run on anything but a
real save, and it never touches the file it reads.

## Linux and Steam Deck

The release page carries the Windows build and, from 1.0.1, a native Linux
build marked experimental. Two ways onto a Deck exist: the Windows build
through Proton, which players ran on the day of the 1.0.0 release, and the
native build, which has been played on a Steam Deck in Desktop Mode by me
on 2026-09-06 -- the menus, the Beach, gyro aim and the settings all as on
Windows, with one known blemish noted below. Both are described here as
they stand on that date; the Gaming Mode instructions follow what the other
N64 recompilations' players do, and every "untested" below means exactly
that.

### The Windows build through Proton

Proton is Steam's compatibility layer for Windows programs, and the one
route with a report ("works great", one player, the day of the release).
On a Steam Deck:

1. In Desktop Mode, unpack the Windows ZIP somewhere under your home
   folder and put `pokemonsnap.z64` beside `Snap64Recomp.exe`, as on
   Windows. Saves, settings, photos and the log stay in that folder; they
   do not go into Proton's prefix.
2. Right-click `Snap64Recomp.exe` and choose **Add to Steam**. In Steam,
   open the shortcut's **Properties**: under **Compatibility** tick
   *Force the use of a specific Steam Play compatibility tool* and pick the
   newest Proton; check that *Start in* is the folder holding the
   executable.
3. Back in Gaming Mode, launch it. The first start compiles shaders twice
   over (the port's own and Proton's translation of them) and takes
   longer; a `vkd3d-proton.cache` file appears in the folder, which is
   Proton's shader cache and can stay.

Under Proton the port sees Steam's controller layer as one Xbox-style
pad, which is how the Deck's controls arrive; 1.0.1 lets that pad through
(the SDL the port ships would otherwise ignore it unless Steam's own
launch said so, and that permission does not always cross into Wine), and
the log says `host: Wine (Proton)` when it applies. Untested on a Deck by
me. Every RT64-based recompilation on SteamOS, through Proton or native,
is reported to hitch at exactly half the panel's refresh now and then;
whether this port does is unmeasured.

### The native Linux build

The same source builds on Linux with Clang or GCC, rendering through
Vulkan, and packs as `Snap64Recomp-<version>-linux-x86_64.tar.gz`
([BUILDING, step 14](BUILDING.md#14-linux-build-experimental)). It has
played the Beach replay under WSL and, on 2026-09-06, a Steam Deck in
Desktop Mode. One blemish is known there and not yet understood: at the
start of the Beach, when the tutorial asks for Z and the camera is raised
and lowered, something flickers in the top-left corner for a moment and is
gone. It does not happen on Windows, so it lies in the Vulkan side of the
renderer or the Deck's driver, and it is on the list. No Linux desktop has
run the build yet. It is used like this:

1. Unpack the tarball under your home folder and put `pokemonsnap.z64`
   beside `Snap64Recomp`. That folder is where everything lives, as on
   Windows. If the folder cannot be written (a read-only mount, a system
   directory), the files go to `~/.config/Snap64Recomp` (or
   `$XDG_CONFIG_HOME/Snap64Recomp`) and the ROM is looked for there; the
   log's first line names the folder in use, and `SNAP_DATA_DIR` in the
   environment names one outright.
2. On a Deck, run it from Desktop Mode, which is where it has been
   tested; or right-click `Snap64Recomp`, choose **Add to Steam**, and
   launch it from Gaming Mode, which is untested and where Steam offers
   the game only its own virtual pad, so gyro aim has to come from Steam's
   layout ("Controls" above). The port boots fullscreen on a Deck (F11 or
   the maximize button leaves it); on any other Linux machine it boots
   windowed, as on Windows.

It needs the system's SDL2 (2.26 or newer), GTK 3 and a Vulkan driver,
and a glibc no older than the one it was built against (2.39; SteamOS 3.8
ships 2.41). The C++ runtime is inside the binary. Rendering is Vulkan
only, whatever `graphics_api` in the settings file says. If a Linux
desktop shows no controller under Steam Input, that is a known problem of
the whole family of ports outside the Deck; the Deck itself is reported
fine.

### On a Deck, either way

* **Controls.** The Deck's controls arrive as an Xbox-style pad: A is A,
  B or X is B, the left bumper is Z, Start is Start, the triggers are L
  and R, the right stick is the C buttons, the D-pad is the D-pad, and the
  View button saves the photo on screen ("Controls" above). Back paddles,
  trackpads and gyro reach the game only as whatever Steam's controller
  layout maps them to.
* **The icon.** A Linux binary carries none, so the port's icon ships
  beside it as `Snap64Recomp.png`: the window takes it at start-up, and a
  Steam shortcut shows it once you pick that file in the shortcut's
  Properties (the artwork Steam shows in Gaming Mode is a separate choice
  there, and yours to make).
* **Desktop Mode with Steam running.** Launched from a terminal or a file
  manager, Steam's desktop layout also sends Enter for A and Escape for B,
  which are the port's Start and pause keys: a shot opened the pause menu
  and B kept bringing it back. While the Deck's own controller is attached
  the port ignores those two keys, and says so in the log. Through a Steam
  shortcut the layout is the gamepad one and nothing is sent.
* **Gyro aiming.** In Gaming Mode the game cannot read the Deck's gyro:
  Steam's controller layer keeps it. In the shortcut's controller settings
  set *Gyro Behavior* to **As Mouse** and *Gyro Activation Buttons* to
  **None Selected (Gyro Always On)**: the port's mouse look then turns the
  camera with the Deck, and Mouse Speed on the Controls page sets how far.
  The right trackpad as mouse works the same way. Launched outside Steam's
  layer (Desktop Mode from a terminal or a file manager, or with Steam
  Input disabled for the shortcut) the port reads the Deck's gyro itself:
  Gyro Aim on the Controls page, at natural scale. On the first Deck run
  (2026-09-06, Desktop Mode) the gyro was found and stayed silent, which is
  what Steam's switched-off sensor looks like ("Controls" above); the port
  now sends the switch itself, and the second run's readings came alive
  within a dozen of it. Which of the two you are in shows in the log's
  controller line.
* **Quitting.** A pad has no quit; use the Steam menu's **Exit Game** (or
  hold Esc on a keyboard). The quit question and any start-up error appear
  as their own small windows in Gaming Mode; the right trackpad moves a
  pointer over them and clicks, and Keep playing is the highlighted
  default.
* **Screen.** The panel is 1280x800; leave the shortcut's Game Resolution
  alone. The Deck's own refresh and frame-limit setting should match the
  panel (60, or 90 on the OLED); the port's Frame Rate: Display then
  follows it. 16:10 with Widescreen fills the panel in a course; the
  menus, and 4:3, sit in black bars.
* **Silent?** Desktop Mode's audio mixer sometimes has an app muted on
  its own; check it before anything else. Waking the Deck from sleep or
  switching to Bluetooth audio while the game runs is untested here; the
  other ports report noise after it.
* **A shortcut missing from Gaming Mode** after Desktop Mode added it is a
  Steam quirk: restart Steam or add it again.

## Known limitations

* **Widescreen is untested beyond the Beach.** The fault it had there,
  Pokémon vanishing at the edges of the wider picture, is fixed in 1.0.1:
  the game's own on-screen test projected each Pokémon at a fixed 4:3
  focal length against fixed pixel bounds, and a patch now widens the
  horizontal bound by the factor the renderer applies, and the effect
  drawer's own test (sparkles, splashes, smoke, the leaves out of the
  grass) is widened the same way. The rest of the game under Widescreen
  has not been played through with the option on.
* Some 2D content is drawn without a name the interpolation can pair
  (the photo panels, Oak's thumbnails and full-screen backgrounds while they
  slide during a transition) and steps at the game's rate when it moves;
  named sprites and menu frames, the HUD and the fades interpolate.
* The Snap Station's printer lettering is set from a typeface of the same
  construction as the printer's, whose own character set no source records,
  and its pass timings are estimates from footage without a clock.
* Vulkan (`graphics_api` 1) is compiled in but has never been run by the
  developer; it exists for a machine whose Direct3D 12 path fails. To try
  it, add `"graphics_api": 1` to `snapsettings.json` next to the executable,
  or create that file containing just `{"graphics_api": 1}` (a key the file
  lacks keeps its default), and start the port again.
* With Overscan Crop off, the whole 320x240 frame is on screen, including
  the columns and rows a television hid, and some of the game's own art has
  edges there: the lab backdrop's leftmost pixel column and top row are
  pale in the picture itself, so a thin light strip shows at the left of the
  lab's translucent panel. The console drew the same pixels (verified
  against the picture as the game holds it in memory); Overscan Crop (F2)
  is the television's view.

## What's next

In the order it will be worked on; nothing here is a promise until it runs.

1. **Reports from machines other than mine.** The port was built and played
   on one PC and, for 1.0.1, a Steam Deck; what other GPUs, drivers and
   Windows 10 do with it is what 1.0.2 will be made of, as 1.0.1 was made
   of the first reports. The issue form is the way to send them.
2. **A page in the game for rebinding.** Since 1.0.1 every key, mouse button
   and pad button is a row in the settings file's table and an N64-shaped
   pad is recognised; what is left is changing them without editing the
   file, and binding the sticks themselves. Asked for in
   [Discussions](https://github.com/JackandBeans/Snap64Recomp/discussions/3).
3. **Widescreen beyond the Beach.** The missing Pokémon and effect
   sprites are fixed; the other courses have not been played with the
   option on.
4. **Steam Deck.** The native Linux build has been played on a Deck in
   Desktop Mode through a day of testing -- the Beach with gyro aim, the
   menus, the settings, the Snap Station -- where it takes the Deck's
   defaults (fullscreen at the panel, Steam's keyboard kept down, Steam's
   desktop-layout keys ignored). What remains is Gaming Mode, where Steam
   offers only its own virtual pad and the gyro reaches the port as mouse
   look, and the top-left flicker at the Beach's start. The Windows build
   under Proton already has one good report.
5. **VR.** An idea under investigation, not a plan: stereo rendering and
   head tracking would have to be built into the renderer.

The same list, with a place to reply, is pinned under
[Discussions](https://github.com/JackandBeans/Snap64Recomp/discussions/1).

## Status

* Built and run on one Windows machine (Windows 11, MSVC 2019, a Direct3D 12
  GPU), and, since 1.0.1, as a Linux build under WSL with Clang that has
  started and played on a Steam Deck in desktop mode ("Linux and Steam
  Deck"). No other GPU, Windows version or Linux desktop has been tried; the
  first report from a different machine is welcome, good or bad.
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
* Version `1.0.1`, typed once in `CMakeLists.txt` and shown in the title
  bar, the log banner, the credits line, the executable's file properties and
  the ZIP's name. `CHANGELOG.md` says what each release changed.
* Licensed under the GPLv3 (`LICENSE`); `NOTICE.md` lists every third-party
  component. No file in the tree carries the game's bytes: the menu font is
  cut from the game's own sprites in memory at run time, and the audio
  microcode is recompiled from the builder's ROM at build time (`NOTICE.md`,
  "Material derived from the game").

## What has been verified, and what has not

* Verified in the sense that the developer has play-tested the entire game
  on the one machine above: every course from the Beach to Rainbow Cloud,
  Oak's evaluations, the report, the album, the Gallery and a Snap Station
  print, with the port's own screens and hotkeys along the way; and the
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
  is valid, and the archive carries everything it must; `--only station` puts the Snap Station print
  through both relaunches and checks the sheets. On the 1.0.1 executable
  (SHA-256 beginning `84ab5bb4`), run without diagnostics in the
  environment, the suite passed 22 of 22 checks in 781 seconds, and the
  station's 5 of 5 in 491; the 1.0.0 executable had passed the same 22 and
  5, in 781 and 489, on a cold shader cache. The suite opens the game window
  for each run and takes about
  thirteen minutes, plus eight for the station. There is no CI run, and no
  build on any other machine is recorded in this repository. Anything not
  listed here should be assumed untried.
* The photo export (P, the controller's Back button, `photos/`) is checked
  by `SNAP_PHOTO_AUTOEXPORT` on an input replay that reaches Oak's check,
  not by hand: saving from the keyboard and from the controller has not been
  tried.
* The recompiled game is generated from a specific decompilation build; the
  chain of tools and inputs is spelled out in `BUILDING.md`, including one
  stale input on the developer's machine that must be regenerated before the
  recompiled code is.

## Building

See [BUILDING.md](BUILDING.md). Short version: the decompilation and IDO under
WSL, N64Recomp for the game and the patches, CMake and MSVC on Windows, and a
list of things git does not carry
([What a clean checkout is missing](BUILDING.md#what-a-clean-checkout-is-missing)).
`cpack -C Release` in the build directory then writes
`Snap64Recomp-1.0.1-win64.zip` ([step 13](BUILDING.md#13-package)).

## The game, and its history

Pokémon Snap was made at HAL Laboratory, with Pax Softnica assisting, and
published by Nintendo: Japan on 21 March 1999, North America in the summer
of 1999 (sources give 30 June and 26 July), Europe on 15 September 2000.

It did not begin as a Pokémon game. In 1995 a small team at HAL under
Yoichi Yamamoto, with Satoru Iwata (then HAL's president, later Nintendo's)
and Shigeru Miyamoto producing, began a photography game called *Jack and
the Beanstalk* for the 64DD disk drive, and took the name **Jack and
Beans** for itself. Iwata told the story in an Iwata Asks interview in
2010. The game, he said, "wasn't a Pokémon game, but rather a normal game
in which you took photos, but the motivation for playing the game wasn't
clear". The question of what players would want to photograph was answered
with Pokémon, in what he called a somewhat forced switch. Masanobu
Yamamoto, a designer on the team, said in the same interview that the
change "clarified what we should do and the direction we should head" and
"had saved us". Pokémon Snap was shown for the 64DD at Nintendo Space World
in November 1997; the disk version was dropped (reported in January 1999)
and the game shipped on a cartridge.

The team's name is still in the game. Its "JACK and BEANS" logo is shown in
the opening beside HAL's and Nintendo's, and the staff roll opens with it,
under "POKéMON SNAP Staff" and above the directors (the credits table is
`src/credits/A94940.c` in the decompilation). That is the name the port's
author took, and why.

Yoichi Yamamoto, Koji Inokuchi and Akira Takeshima directed; Iwata, Miyamoto
and Kenji Miki produced; Ikuko Mimori wrote the music. Sixty-three of the
first 151 Pokémon appear, across seven courses: Beach, Tunnel, Volcano, River,
Cave, Valley and Rainbow Cloud. The game sold more than 1.5 million copies by
the end of 1999, was the best-selling Nintendo 64 game in the United States
that year, and took the Interactive Achievement Award for console children's
and family title of the year.

Two things about the original release shaped this port. The first was the
kiosk. In 1999 Nintendo put Pokémon Snap Station kiosks into Blockbuster
Video stores across North America (the deal was announced in May 1999 and
the stations were in stores by November), Lawson convenience stores in
Japan, and Myer department stores in Australia (Toys "R" Us there too, by
one account), and the promotion ran until late 2000. Each was a Nintendo 64
in a blue child-height cabinet with a slot for the player's own cartridge, a
Canon photo printer on controller port 4, and a Gemco card reader. A player
bought print credit on one of five Pokémon smart cards (Bulbasaur,
Charmander, Squirtle, Pikachu and Jigglypuff), brought a save in, and left
with a sheet of sixteen postage-stamp stickers of their own photos, for
three dollars or 300 yen; Blockbuster ran a "Take Your Best Shot" contest
around them. About 4,500 units were built, by the Arcade Museum's count;
when the promotion ended most were scrapped or turned into demo units for
other games, Nintendo recalled some, and a working one is a rarity. The
cartridge's code for the printer was recovered without a station by James
Chambers in 2021, from the ROM, a debugger and a controller-bus tool of his
own, and it matches the decompilation; the port emulates the device ("The
Snap Station" above).

The second was the re-release. When Nintendo brought the game to the Wii's
Virtual Console in December 2007 (Wii U in 2016 and 2017, Nintendo Switch
Online on 24 June 2022), it replaced the kiosk with saving photos to the
Wii Message Board, from which they could go to an SD card or to friends
(the Wii U version sent them to Miiverse instead). It also recoloured Jynx
from black to purple, as it had in its other early Pokémon re-releases.
The port's photo export and Jynx Recolor options are those two changes,
reproduced and off by default.

The port could not exist without the
[decompilation](https://github.com/ethteck/pokemonsnap), the community's
years of work turning the cartridge back into readable C. Every statement
in this README about the game's own behaviour was checked there.

Sources:

* [Wikipedia](https://en.wikipedia.org/wiki/Pok%C3%A9mon_Snap).
* [Iwata Asks: Kirby's Epic Yarn, part 4](https://www.nintendo.com/en-gb/Iwata-Asks/Iwata-Asks-Kirby-s-Epic-Yarn/Iwata-Asks-Kirby-s-Epic-Yarn/4-Surprise-Fun-and-Warmth/4-Surprise-Fun-and-Warmth-207100.html)
  (Nintendo, October 2010), for Iwata's and Masanobu Yamamoto's words.
* [Nintendo Life, "Pokémon Snap: The 64DD Origins Of A Picture-Perfect Spin-Off"](https://www.nintendolife.com/news/2021/04/feature_pokemon_snap_-_the_64dd_origins_of_a_picture-perfect_spin-off)
  (2021).
* [Unseen64, "Jack and the Beanstalk [N64 DD - Cancelled]"](https://www.unseen64.net/2010/10/29/jack-and-the-beanstalk-nintendo-64-dd-cancelled/)
  (2010).
* [Nintendo World Report, "Know Your Nintendo Developers: Pokémon Snap"](http://www.nintendoworldreport.com/feature/43893/know-your-nintendo-developers-pokemon-snap)
  (2017).
* [Bulbapedia](https://bulbapedia.bulbagarden.net/wiki/Pok%C3%A9mon_Snap),
  for the kiosk's cards and prices.
* [Serebii, Virtual Console changes](https://www.serebii.net/snap/virtualconsole.shtml).
* [Museum of the Game, "Pokemon Snap Station"](https://www.arcade-museum.com/Vending/pokemon-snap-station),
  for the unit count, the Gemco reader and Canon printer, the run to late
  2000, and what became of the units.
* [Nintendo Wiki, "Pokémon Snap Station"](https://nintendo.fandom.com/wiki/Pok%C3%A9mon_Snap_Station),
  for Toys "R" Us in Australia, which no other source read names.
* [Den of Geek, "How Pokemon Snap Stations Defined the Original N64 Game's Legacy"](https://www.denofgeek.com/games/pokemon-snap-original-n64-blockbuster/),
  for the November 1999 rollout and for Myer in Australia; Myer's own kiosk
  cards still turn up for sale, which is the corroboration.
* [TheGamer, "I Almost Bought A Pokemon Snap Station (Twice)"](https://www.thegamer.com/nintendo-pokemon-snap-station/)
  (2021).
* [jamchamb, "Reversing the Pokémon Snap Station without a Snap Station"](https://jamchamb.net/2021/08/17/snap-station.html)
  (2021).

## How it was made

Two answers, because the question has two parts.

**What the port is, mechanically.** N64Recomp reads the game's code out of
the builder's own cartridge dump and writes it out as C, one function at a
time, at build time; none of that output is in this repository. That C is
compiled and linked with three other things: librecomp and ultramodern,
which give it the console's operating system calls; RT64, which turns the
game's display lists into Direct3D 12; and the port's own code under
`src/`. That code is the window, input, audio and settings, the identity
the frame interpolation pairs objects and sprites by, the menu pages
composed from the game's own sprite font, the photo export and the Snap
Station. Where the game's own
behaviour had to change for a feature (the Graphics page on its Options
screen, the fifth title entry, the intro's camera fix), the changed function
is a copy of its decompiled source under `patches/src`, compiled with the
decompilation's own IDO toolchain and loaded over the original. The whole
chain, with the tools and inputs at each step, is `BUILDING.md`.

**Who wrote it.** One person directed it, not a team, and the models named
below wrote it. JackandBeans is an individual with no studio, no
collaborators and no funding behind this, who directed the work from the
first commit on 17 August 2026 to this release. That direction was setting
the rule the port follows (console behaviour by default), choosing what it
would and would not do, play-testing every build on screen and the whole
game through on the release build, and finding the reference material the
work needed. That material was the references for
the Snap Station, chief among them Leonhart's recording of a working kiosk,
the main video of the station in use and the one the printer's display was
measured against; a published Virtual Console screenshot, found through an
image search, that the Jynx colour was matched from; and the renders the
logo was composed from. "The developer" elsewhere in this README is that person; no model
played the game.

Every line of the port's own code, its tools and its documentation, this
README included, was written by Anthropic's Claude models running in Claude
Code under that direction. The models were Claude Fable 5.1 and Claude
Fable 5, with Claude Opus 5 for a large share of the commits and two
commits by Claude Opus 4.8; the author also used Claude Sonnet 5 in some
sessions, and no commit names it. A commit's trailer names the model that
wrote it (`git log --format=%(trailers:key=Co-Authored-By)`); forty-six
commits between 18 and 24 August 2026 carry none, because they come from
sessions before the trailer was added every time. Fable 5.1, the newest of
them, carried the release work: the Snap Station from
the decompiled protocol to the printer's display, the renderer's
frame-pacing and identity work, and the audit and packaging of this
release.

What that meant in practice is the honest measure of what these models can
do when someone directs and checks them. They held the decompilation, the
runtime and RT64's source in view at once and changed one without breaking
the others. They found the game's own behaviour in its code before deciding
whether a difference was the port's (the intro's off-by-one frame, the lab
picture's pale edge, the station's boot-time printer test). They measured
instead of eyeballing: frames were compared pixel by pixel against the
picture in the game's memory, and the kiosk's lettering was measured from
frames of a recording of a real screen. They built their own verification,
the headless suite that replays controller recordings through the real
executable and reads back its log and captured frames. And they wrote down
every finding, measurement, dead end and revert in the commit messages,
which are the project's real notebook. What they cannot
do is see the screen or hold a controller: every judgement of how a thing
looks or feels was the author's, made on one machine, and the port has not
yet run on any other.

None of this asks to be taken on trust. The code is here, the commit
history is here with its reasoning, the notes under `docs/dev/` are the
models' reports to the author kept as written, and the suite's replays
are tracked so its checks can be re-run. `git log --stat` and an afternoon
of reading is the way to judge the job Claude did.

## Thanks

None of this would exist without:

* The [Pokémon Snap decompilation](https://github.com/ethteck/pokemonsnap)
  and everyone who worked on it: the port's patches are compiled against
  its headers and symbols, and every fact about the game's code in this
  README was read there.
* [N64Recomp](https://github.com/N64Recomp/N64Recomp) and
  [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) by
  Wiseguy and contributors, the recompiler and runtime this port stands on,
  and the [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp)
  project whose structure it follows.
* [RT64](https://github.com/rt64/rt64) by Dario and contributors, the
  renderer, whose frame interpolation this port extends.
* James Chambers (jamchamb), whose [2021
  write-up](https://jamchamb.net/2021/08/17/snap-station.html) recovered the
  Snap Station protocol from the cartridge without a station to test
  against; the port's emulation of the device follows it and the
  decompilation.
* Leonhart, whose recording of a working Snap Station kiosk, ["Using
  Pokemon Snap Station For First Time In 20
  Years!"](https://youtu.be/lCnvpIEVpqo), was the main video reference for
  how the station behaved in use, and the one the printer's display was
  measured from: its sticker grid, its marks and stars, and the pace of its
  three passes all come from those frames.
* [ido-static-recomp](https://github.com/decompals/ido-static-recomp)
  by the decompals, which lets the decompilation's compiler, and so the
  port's patches, build on a modern machine.
* [SDL2](https://libsdl.org), the DirectX Shader Compiler, and the libraries
  named in `NOTICE.md`.
* The Roboto Project Authors, for the typeface the printer's lettering is
  set from.
* Anthropic's Claude, which wrote the port ("How it was made" above).
* Jack and Beans, the team at HAL Laboratory who made the game. Their name,
  shown in the game's opening and at the head of its credits, gave the
  port's author a name.
* Everyone who plays it and reports what they see: the first reports from
  other machines are what 1.0.1 was made of, and the next ones are what
  1.0.2 will be.

## License

Copyright (C) 2026 JackandBeans. GPLv3: see `LICENSE` and `NOTICE.md`. The port's own code is the author's
and GPLv3; the game is Nintendo's, Creatures', GAME FREAK's and HAL's, and
nothing of it is here.
