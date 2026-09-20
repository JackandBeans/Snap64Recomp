# The manual

Everything the port does, in the detail the front page leaves out: where
its files live, every control and how to change it, the pages it adds to
the game's Options screen, the hotkeys, the photo export, mods and texture
packs, and every key of the settings file. The short version is the
[README](../README.md); the Snap Station has [its own page](SNAP-STATION.md),
and so do [Linux and the Steam Deck](STEAM-DECK.md).

**Contents:** [Running](#running) · [Where things live](#where-things-live) ·
[What you need, in detail](#what-you-need-in-detail) · [Controls](#controls) ·
[In-game pages](#in-game-pages) · [Hotkeys](#hotkeys) · [Photos](#photos) ·
[Mods and texture packs](#mods-and-texture-packs) ·
[Settings file](#settings-file) · [Every course open](#every-course-open)

## Running


Start `Snap64Recomp.exe`; a shortcut works from anywhere, because the port
reads and writes the folder the executable is in, whatever the working
directory (`src/paths.cpp`). It opens a 1280x960 window titled
`Snap64 Recomp 1.0.9`; `SNAP_WINDOW=WxH` in the environment opens it at
an exact size instead (at least 320x240). The window's maximize button is the
fullscreen switch; the in-game Graphics page and F11 do the same, and F11
is the way out of fullscreen from anywhere. **A tap of Esc opens the port's
Options** on any screen (in a course it pauses the ride and opens them at
once); Enter is the game's Start. **Holding
Esc for a second and letting go asks whether to quit**; Keep playing is
the answer to Enter and to Esc, and Quit takes a click or Tab and Enter.
**Exit Game**, the last row of the game's Options screen, closes the
program from a pad: A turns its help line into the question, a second A
closes, B stays. The window's close button and Alt+F4 quit at once, as
any window's do. On a pad, Select opens the port's Options anywhere and the
right stick pressed in saves the photo on screen (until 1.0.9 that was
Select, as on the Wii Virtual Console release).

Saves go to `saves/` and settings to `snapsettings.json`, both next to the
executable. No console opens: the log is `snap64.log` next to the
executable, and the previous run's log is kept as `snap64.prev.log`.
`snap64.log` is the first thing to include in a bug report. Started from a
terminal, or with its output redirected, the port writes the log there
instead and the file is not touched.

A second copy started while the first is running, from any folder, waits up
to 25 seconds for it to exit (the Snap Station relaunches itself that way)
and otherwise tells you the port is already running.

### If Windows or your antivirus objects

The executable is not signed, so
the first start may bring up SmartScreen's "Windows protected your PC";
"More info", then "Run anyway", is the route, once. The Snap Station's Print
starts a fresh copy of the port twice in a row, which some antivirus
heuristics dislike; allow it if asked. Nothing here phones home: the port
opens no network connection at all.

### Back up your save

`saves/pokemonsnap.bin` is the whole of your progress
in one file, with one earlier generation kept as `.bak`. Copy `saves/`
somewhere else before updating the port or trying a Snap Station print.

### Reporting a bug

Open an issue at
[github.com/JackandBeans/Snap64Recomp/issues](https://github.com/JackandBeans/Snap64Recomp/issues)
(the repository this README came from) and attach `snap64.log` from the run
that went wrong, `Snap64Recomp.map` if the log has `[SNAP-AV]` lines, your
`snapsettings.json`, and what you were doing. Say which GPU and driver you
have, and which operating system; the port has run on my PC, on a Steam
Deck, and on the machines of the players who have reported, and each new
one has taught it something. The issue form asks for
these; [CONTRIBUTING.md](../CONTRIBUTING.md) has the ground rules for code.

## Where things live


Everything is in the folder with the executable. `SNAP_DATA_DIR=<absolute
path>` in the environment moves that folder, on Windows and Linux alike,
for a launcher that keeps profiles apart: the ROM is looked for there and
the files below are written there, while the files the port ships beside
the executable stay beside it. The log's first line names the folder in
use.

| File or folder | What it is |
| --- | --- |
| `pokemonsnap.z64` | your ROM (you provide it) |
| `snapsettings.json`, `snapsettings.json.bak` | settings, written by the in-game Graphics and Sound pages and by the hotkeys |
| `saves/pokemonsnap.bin`, `saves/pokemonsnap.bin.bak` | the game's save data, one file (a raw image of the cartridge's save memory) |
| `photos/` | the photos you save with P or the pad's right stick pressed in (see "Photos"); created on the first save |
| `cache/` | RT64's compiled shaders and the driver's pipeline cache, built on your machine, and `rt64-seen-shaders.bin`, the list of every shader the game is known to ask for -- shipped with 623 entries from a full playthrough, so the first start compiles them all during the boot logos rather than the first time each appears in play; the game adds any it meets that are not on it. Safe to delete; the next start is slower |
| `snap64.log`, `snap64.prev.log` | the log of this run and of the one before it; on Windows written when the port was not started from a terminal, on Linux on every launch |
| `mods/`, `mod_config/`, `mods.json` | the runtime's mod folders and its list of the mods that are on; the loader runs at every start, no mod ships with this release, and the game's Options screen has a Mods page for them (see "Mods and texture packs") |
| `texture_packs/` | HD texture packs you install yourself, scanned once at start-up; created empty, none ships with this port (see "Mods and texture packs") |
| `stickers/` | the sticker sheets the Snap Station prints (see [SNAP-STATION.md](SNAP-STATION.md)); created on the first print |
| `menu_text/recomp_logo.png` | the "Recomp" wordmark on the title screen |
| `SDL2.dll`, `dxcompiler.dll`, `dxil.dll` | the window, input and audio library, and the shader compiler and validator the renderer needs; leave them beside the executable |
| `Snap64Recomp.map` | the linker map; include it with crash reports (the `[SNAP-AV]` lines in the log are decoded against it) |
| `LICENSE`, `NOTICE.md`, `licenses/` | licences |

Coming from an earlier build: settings, saves and the ROM were already next to
the executable and carry over as they are. Earlier builds kept the shader cache
in `%LOCALAPPDATA%\pokemonsnap`; that folder is no longer read and can be
deleted. The first start after the change rebuilds the cache once.

## What you need, in detail


* A 64-bit Windows 10 or 11 PC (the port asks Windows for per-monitor DPI
  awareness, which needs Windows 10 version 1703 or later). The executable
  imports `d3d12.dll`, `dxgi.dll` and `d3dcompiler_47.dll` from Windows, so
  the GPU driver must provide Direct3D 12; `vulkan-1.dll` is loaded only if
  you switch the renderer to Vulkan (`graphics_api` in the settings file,
  "Settings file" below). The Visual C++ runtime is linked into the
  executable; nothing else has to be installed. The renderer creates its
  Direct3D 12 device at feature level 11_0 and needs shader model 6.0 (its
  Vulkan path needs Vulkan 1.2), which GPU drivers have provided for years;
  no slowest card has been measured. It has run on an AMD Radeon RX 9060 XT
  (mine), an NVIDIA GeForce RTX 4070 Ti Super (a reporter,
  issue #13), the Steam Deck's own GPU under Linux, and Mesa's software
  Vulkan, which draws the game slowly but correctly.
* **Your own dump of the US cartridge**, whose SHA-1 checksum (a fingerprint
  of the file's contents) is `edc7c49cc568c045fe48be0d18011c30f393cbaf`, the
  value the [decompilation project](https://github.com/ethteck/pokemonsnap)
  publishes. Put it next to `Snap64Recomp.exe` named `pokemonsnap.z64`
  (the port reads its own folder, not the working directory; see "Where
  things live"), or start the program without it: it asks for the file,
  checks it, and copies it there under that name in big-endian order,
  whatever order the dump was saved in (`.z64`, `.v64` and `.n64` all
  serve). A dump placed by hand in `.v64` or `.n64` order works too: the
  order is read from the file's header and corrected in memory without
  touching the file, though the name must still be `pokemonsnap.z64`. A file
  that cannot be read or is another revision of the game is refused with
  both the expected and the actual hash (a 64-bit hash of the whole file,
  not the SHA-1 above), so you need not compute anything yourself. The ROM
  is never included with this project.
* Beside `Snap64Recomp.exe`: `SDL2.dll`, `dxcompiler.dll` and `dxil.dll`,
  and optionally `menu_text/recomp_logo.png` for the "Recomp" badge under the
  title logo (no file, no badge). The release ZIP already holds all of them;
  if you build the port yourself, the build places them there
  (`BUILDING.md`, step 12).

## Controls


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
| Start | Enter | | pause |
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
game's Options screen holds the mouse dials: Mouse Aim, Mouse Speed, Zoom Speed
and Camera Tilt (they are `mouse_aim`, `mouse_sensitivity`,
`mouse_zoom_speed` and `mouse_invert_y` in the settings file), and its
Button Setup row opens the page where each key, mouse button and pad
button is set, described below.

A pad with a gyro can aim the same way: turn the pad and the view turns
with it, at natural scale, so a ten-degree turn of the pad is a
ten-degree turn of the view, whatever the zoom, the way a real camera
follows your hands. **Gyro Aim** on the Controls page turns it on (On:
whenever a course runs; Zoomed: only while zoomed in, the way a
photographer raises the camera to aim), **Gyro Speed** scales it, and
Camera Tilt flips the vertical for the gyro as it does for the mouse. It
is off as shipped. Looking around by mouse or gyro also counts as the
Control Stick for the Beach tutorial's one check of it, so the game no
longer asks for the stick from a player who is already looking around;
with both off that check is the game's own. The pads whose gyro reaches
the port are the ones SDL
reads it from: DualSense, DualShock 4, Switch Pro and Joy-Cons, over USB
or Bluetooth, and the Steam Deck's own controls when Steam's controller
layer is not in between (see [STEAM-DECK.md](STEAM-DECK.md)); an Xbox pad has no
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

Every keyboard, mouse and controller binding can be changed in the game.
The Button Setup row of the Controls page (named as the game names such
screens: "Z Button Setup") opens a page with a row for each of the game's
inputs, each row named by its button and what it does ("Z Button: Zoom",
"C-Up: Look Back", "A Button: Photo"), showing what presses it on the
device picked at the top ("Set Up: Keyboard", Mouse or Controller; Left
and Right switch), and the help line says more about what that input does
in the game. A on a row makes the port listen: the next key, mouse button,
wheel tick or pad button pressed on that device becomes the row's
binding, in place of what the device had there (the other devices' stay).
Esc leaves the row as it was, and so does waiting eight seconds; a key the
port answers to itself (the function keys, P, the brackets, Home, End,
Esc) and the pad's Back and Guide are refused. Z clears the row on that
device, unless nothing else would press the input. Restore Defaults, the
last row (Up from the top row wraps to it), asks for a second A and then
puts the shipped bindings of the device shown back. What no table changes
is named on the rows too:
under Controller the stick that aims and the one that works the C
buttons, under Mouse the motion that aims while Mouse Aim is on, and Esc
beside Start on the keyboard. Every change is in force at once and saved.

The same bindings are in the settings file, where a text editor changes
them too. Its `keys` table names each input (`a`, `b`, `z`, `start`, `l`, `r`, `c_up`,
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

### Fast forward

Hold Tab, or the pad's right shoulder button, and the game
runs at the speed the Controls page's Fast Forward row says: 3x as shipped,
2x or 4x, or Off. It is the console run faster, not the game changed: every
clock the game reads runs that many times faster, so it steps through exactly
the frames it would have stepped through anyway, with the same scores and the
same saves; the picture shows the latest frame at the display's rate, with
frame interpolation off for the duration, and the sound plays quicker and
higher, as a tape does. Release the key and everything is back at once. The
key is the `fast_forward` entry of the `keys` table, changed in the file like
any other; it has no row on the Button Setup page, which refuses its sources
as it refuses Back. On an N64-shaped pad, which has no shoulder button of that
kind, only the key works until the file names another button.

### Slow motion

Slow motion is the same thing run the other way. Hold Space, or press the
left stick in on a pad, and the game runs at half or a quarter of its speed,
the Controls page's Slow Motion row (Off as shipped, since a shot lined up at
half speed is an easier shot than the cartridge offered). The picture stays
smooth: frame interpolation keeps running, each game frame stretched over the
longer time it stands for. The sound plays slower and lower. The key is the
`slow_motion` entry of the `keys` table, on the left hand on purpose, so the
right thumb stays free for A; when both keys are down, slow motion wins.

Any SDL game controller overrides the keyboard while attached: left stick is
the control stick, A is A, B or X is B, the left shoulder button is Z, Start
is Start, the D-pad is the D-pad, the triggers are L and R, and the right stick
is the C buttons. **Pad Sticks** on the Controls page swaps the two sticks,
so the right one aims and the left works the C buttons (`pad_sticks_swapped`
in the settings file), and **Dead Zone** is how far the aiming stick moves
before the game sees it, 15 percent as shipped and up to 40 for a pad whose
stick drifts at rest (`pad_deadzone`); it is radial, and the travel past it
is rescaled so the first movement the game sees is the smallest. The Back button (Select, View or Share on most pads) is not
an N64 button: it opens the port's Options on any screen, as Esc does on
the keyboard, and the right stick pressed in saves the photo on screen, as
P does (see "Photos"; until 1.0.9 that was the Back button). The eight buttons -- A, B, Z, Start, the D-pad's four, L and R --
are each a row of the Button Setup page and of the settings file's `keys`
table and can be moved; the two sticks can only be swapped, and the Back
button keeps its job.

### Which controllers work

Anything SDL2 has a mapping for, which is most of
what is sold: Xbox pads (360, One, Series) over USB or Bluetooth, PlayStation
(DualShock 3 and 4, DualSense), Switch Pro and Joy-Con, the Steam Deck's own
controls, the Steam Controller, and a long tail of third-party pads. No pad
rumbles, because the cartridge never asks: the only motor calls in the ROM
are in its reset handler, and the port reports port one as a controller with
nothing in its pak slot, as a console without a Rumble Pak does. Gyro aim
needs a pad with a motion sensor: DualShock 4,
DualSense, Switch Pro, and the Steam Deck. A pad shaped like the N64's -- the
Switch Online N64 controller, over Bluetooth or USB -- is recognised by its
name, and its L, R and Z are L, R and Z, its C buttons the C buttons; the
`pad_layout` key forces either layout. The D-pad walks every menu as the
stick does, since the cartridge never reads it. `pad_enabled: false` in the
settings file makes the port ignore every pad without unplugging one.

The first pad SDL recognises is the one used, and SDL uses only a pad it
has a mapping for. Its own list holds a few thousand; for the rest the port
ships `gamecontrollerdb.txt`, the community's list
([SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)),
beside the executable, and reads it at start-up, saying in the log how many
mappings it added (shipped since 1.0.3; before that a player had to put it
there). A pad neither list knows is named
in the log (`[SNAP-Input] joystick ... has no game controller mapping`) and
does nothing until it is taught: a newer copy of the file from that
project, dropped over the shipped one, is the first thing to try; a single
mapping string in SDL's `SDL_GAMECONTROLLERCONFIG` environment variable,
which SDL itself documents, is the second.

## In-game pages


Options > **Graphics**, in the order the page shows them: Render Scale,
Super Sampling, Anti-Aliasing, Widescreen, Frame Rate, 2D Detail, Filter,
Texture Filter, Color Depth, Buffering, Dither, Fullscreen, Overscan Crop,
Cutscene Fix, Photo Detail, Jynx Recolor. Color Depth and Buffering take
effect after a restart; everything else applies while the page is open. The
page's Frame Rate row runs Original, Display, then 60, 90, 120, 144, 165
and 240: a number is the Manual mode (`fps_mode` 2) held at that rate by
interpolation, and F8 still cycles the three modes.

Options > **Sound**: Master Volume, Music Volume, Sound Effects, Shutter
Volume, Speaker Output (Stereo/Mono), Background Mute.

Options > **Controls**: Z Button (Hold/Switch) and Control Stick
(Normal/Reverse), the game's own two settings, moved here from the Options
list so the list keeps the stock rhythm; then Button Setup, the row that
opens the Button Setup page; Pad Sticks; Dead Zone; Fast Forward (Off, 2x,
3x or 4x: the speed the held key runs the game at); Slow Motion (Off, 2x or
4x slower, the same way); Mouse Aim, Mouse Speed (25 to 400 percent of the
shipped speed), Zoom Speed (the share of that speed used while zoomed in),
Camera Tilt (Normal/Reverse, for the mouse and the gyro alike), Gyro Aim
(Off, On, or Zoomed for only while zoomed in) and Gyro Speed (25 to 400
percent of natural). Thirteen rows, six on screen; the page scrolls for the
last seven, as the Graphics page does. Every change applies as it is made;
B puts the page back as it was opened.

**From anywhere**: Esc on a keyboard, or Select (View, Back, Minus: the
small left button) on a pad, opens the same list on any screen -- the
title, Oak's lab, the map, a course, the Report, the Gallery -- over the
screen as it stands, held still and dimmed, in the Options screen's own
dress: the rules above and below the heading, A OK and B Cancel, the help
box, under the list and under every page. Five rows: Graphics, Sound,
Controls (with Button Setup), Mods and Exit Game, each the row the title's
screen has (Screen, the cartridge's picture-position page, is main menu code
and stays on the title's screen). **Esc, Select or Start on the list or on
any page closes everything and the screen goes on** exactly where it was,
keeping what is on screen (not on Button Setup, where Start is a button to
bind); B leaves a page for the list and the list for the screen. In a course
the same key pauses the ride first, as Start does, and closing resumes it.
Enter is the game's Start: in a course, the pause menu, which has a fourth
pill under Retry, **Options**, green, in the pause menu's own artwork and
letters, opening the same list; from there B brings the pause menu back.
The HUD's item icons step aside while the pause is up; the film counter goes
only behind the pages. Every change applies as it does from the title
screen, with one difference: the Controls page's Z Button and Control Stick
rows, which the title's Options screen applies when it closes, take effect
the moment they are changed here. The key waits out a fade and a screen's
first two seconds, and does not open the pages at all where they would
break something: the attract demo and the credits, which are scripted to
their music, and the photo check after a ride, whose screen fills its
display list on its own and has no room for them (`snap64.log` says so
when it happens). In a course the key follows the game's own rule for
Start. Holding Esc for a second still asks whether to quit.

Options > **Mods**: the list's fifth row, where the stock Return row was
(B returns from the screen, as Return's own help line said, and the list
has no seventh slot above the help box). One row per mod in the `mods/`
folder, its name and version, six on screen, the page scrolling for the
rest; each row's value is On or Off. A, Left or Right turns the selected
mod on or off: the change is written to `mods.json` at once and takes
effect the next time the game starts, and the help line says so under the
mod's own short description, with its author. **L and R move the selected
mod up or down the load order**, which is the order the runtime runs mods'
hooks in. **Z opens a mod's options** when its manifest declares any: one
row per option, Left and Right changing it (an enum to its next choice, a
yes-or-no on or off, a number a step at a time within its range; a text
option is shown and left to its file), each written to the mod's own
settings file at once. The hint at the header's right says which of those
apply to the row. Under the mods, two rows the other recompilations' mod
menus have as buttons: **Open the mods folder**, which shows it in the file
browser, and **Restart the game**, which closes the game and starts it again
with the mods as set. A mod file (`.nrm`) dropped onto the window is copied
into the folder and loads at the next start. With an empty folder the page
says that instead. B leaves; nothing is undone, and there is nothing to
cancel ([Mods](MODS.md)).

Options > **Exit Game**: the list's sixth row, under Mods, in the
screen's own font and rhythm, and the fifth row of the list the pause menu
opens in a course. Its help line says what it does; A turns the help line
into "Press A again to close the game, B to stay", and the second A closes
the program the way the window's close button does. B, or moving off the
row, withdraws the question. It is the way to quit from a pad, on a Steam
Deck in particular.

## Hotkeys


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
| Esc | Tap: the port's Options, on any screen (Esc, Select or Start closes them). Held a second and released: the quit question |
| Tab (held) | Fast forward, at the Controls page's Fast Forward speed; the `fast_forward` entry of `keys` moves it, and names a pad button too (the right shoulder as shipped) |
| Space (held) | Slow motion, at the Controls page's Slow Motion speed, Off as shipped; the `slow_motion` entry of `keys` moves it (the left stick pressed in, on a pad) |

## Photos


**P**, or the pad's **right stick pressed in**, saves the photo on screen as a PNG:
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

## Mods and texture packs

Writing a mod, and what the port does with one, has [its own page](MODS.md).


Two loaders are compiled in, and both run on every start; neither has any
content attached. The runtime this port is built on scans `mods/` for `.nrm`
mod containers, the format the other N64 recompilation projects use, and
loads the ones enabled in `mods.json` beside the executable; a mod must
target the game id `pokemonsnap`, and the game's Options screen has a Mods
page that turns each one on or off, orders them and opens their options
([MODS.md](MODS.md)). The renderer scans `texture_packs/` for RT64
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

## Settings file


`snapsettings.json` is written about 750 ms after the last change (so a slider
costs one write), and on exit if anything is unsaved; the previous file is
kept as `snapsettings.json.bak` and read if the main file is missing or does
not parse. Keys are the field names of `struct Settings` in `src/settings.h`;
the defaults below are that file's.

| Key | Default | Meaning |
| --- | --- | --- |
| `fullscreen` | `false` | saved as set; the window opens windowed and goes fullscreen a moment later when the file says so (a window created fullscreen comes up with broken chrome), and the Snap Station's own relaunches return in the state the print started in |
| `widescreen` | `false` | RT64 Expand: a true 16:9 field of view, not a stretch, in a course; the title, the lab and the other 4:3 screens sit in black bars. Pokémon and effect sprites at the edges are kept ([Known limitations](../README.md#known-limitations)) |
| `msaa` | `0` | 0, 2, 4 or 8 |
| `fps_mode` | `0` | 0 Original, 1 Display refresh, 2 Manual (`fps_manual_target`) |
| `fps_manual_target` | `120` | the rate a number on the Frame Rate row holds; a value not on the row shows as the nearest of its eight and becomes it once the page is edited |
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
| `snap_station` | `false` | keep the Snap Station on port 4 from the title menu on, every start (see [SNAP-STATION.md](SNAP-STATION.md)) |
| `pad_enabled` | `true` | `false` makes the port ignore every pad: none is opened, and the keyboard and mouse carry on |
| `pad_layout` | `0` | how a pad's shoulders and triggers are read: `0` decides by the pad's name, `1` the Xbox-style layout the defaults describe, `2` an N64-shaped pad (the Switch Online N64 controller), whose L, R and Z are L, R and Z |
| `pad_sticks_swapped` | `false` | the right stick aims and the left works the C buttons (Pad Sticks) |
| `pad_deadzone` | `15` | the aiming stick's dead zone in percent of full travel, 0 to 40 in steps of five (Dead Zone) |
| `fast_forward_speed` | `3` | the speed the held fast-forward key runs the game at: 1 Off, 2, 3 or 4 (Fast Forward) |
| `slow_motion_speed` | `1` | how much slower the held slow-motion key runs the game: 1 Off, 2 half speed, 4 a quarter (Slow Motion) |
| `mouse_aim` | `true` | the mouse aims while a course runs and the window has focus ("Controls"); its buttons work whenever the window has focus, through `keys` |
| `mouse_sensitivity` | `1.0` | angle per pixel of mouse: 1 is a full turn in about 2500 pixels, 2 twice as quick, 0.5 half; 0.1 to 10 |
| `mouse_invert_y` | `false` | mouse forward, or the pad's front rising, tilts the view down (Camera Tilt: Reverse) |
| `mouse_zoom_speed` | `0.5` | the mouse's speed while zoomed in, as a share of `mouse_sensitivity`; 0.25 to 1 (Zoom Speed) |
| `gyro_aim` | `0` | the pad's gyro aims the camera: 0 off, 1 in a course, 2 only while zoomed in (Gyro Aim) |
| `gyro_sensitivity` | `1.0` | multiplies the gyro's natural scale; 0.25 to 4 (Gyro Speed) |
| `keys` | the table under "Controls" | what presses each input: SDL key names, the mouse names, and `"Pad "` plus SDL's controller button or axis name; one or a list |
| `graphics_api` | `0` | 0 Direct3D 12, 1 Vulkan (RT64's other backend and the one the Linux build uses; run here on an AMD card through the replays, and an escape hatch if D3D12 fails); restart |
| `downsample` | `1` | Super Sampling factor |
| `resolution_scale` | `0` | 0 follows the window; 1-8 caps the render scale in multiples of 320x240 |
| `present_filter` | `2` | 0 nearest, 1 linear, 2 RT64's anti-aliased pixel scaling |
| `upscale_2d` | `1` | 0 original pixels, 1 only content that scales anyway, 2 everything sharp, which can put a line under a logo and a fringe around a keyed sprite (seen on a Steam Deck) |
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

## Every course open


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
