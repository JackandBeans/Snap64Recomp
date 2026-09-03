# Snap64 Recomp

A native Windows port of the Nintendo 64 game *Pokémon Snap* (US release),
made by static recompilation. [N64Recomp](https://github.com/N64Recomp/N64Recomp)
translates the game's MIPS code into C, [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
(`librecomp` + `ultramodern`) stands in for the console's operating system,
[RT64](https://github.com/rt64/rt64) renders, and SDL2 provides the window,
input and audio. The game's own code runs; the port changes how it is hosted,
and every change to how it *looks* is off unless you turn it on.

The title screen's credits line reads `Jack & Beans (Snap64 Recomp) · v1.0.0 rc1`:
"Jack & Beans" is the codename this project uses for itself, an homage to
the team behind the original game (`src/version.h.in`
explains why), "Snap64 Recomp" is the port's name, and `1.0.0-rc1` is its
version -- the credits face has no hyphen, so the line sets the tag off with a
space. This project is not affiliated with
Nintendo, HAL Laboratory or The Pokémon Company; their trademarks are theirs.

## Status: prerelease

* Built and run on one machine: Windows 11, MSVC 2019, a Direct3D 12 GPU. No
  other platform, GPU or compiler has been tried.
* **Not buildable from a clean checkout.** Vendored trees, the recompiled
  game and the recompiler's inputs are missing from git; `BUILDING.md` lists
  exactly what, and gives the full pipeline as it exists today.
* No automated tests, no CI, no installer. The release archive is the ZIP
  that `cpack` writes (`BUILDING.md`, step 13); none has been published.
* Version `1.0.0-rc1`, typed once in `CMakeLists.txt` and shown in the title
  bar, the log banner, the credits line, the executable's file properties and
  the ZIP's name.
* Licensed under the GPLv3 (`LICENSE`); `NOTICE.md` lists every third-party
  component. No file in the tree carries the game's bytes: the menu font is
  cut from the game's own sprites in memory at run time, and the audio
  microcode is recompiled from the builder's ROM at build time (`NOTICE.md`,
  last section).

## What you need

* A 64-bit Windows PC. The executable loads `d3d12.dll`, `dxgi.dll` and
  `vulkan-1.dll` from the system, so the GPU driver must provide Direct3D 12
  and the Vulkan loader.
* **Your own dump of the US cartridge**, SHA-1
  `edc7c49cc568c045fe48be0d18011c30f393cbaf` (the checksum the
  [decompilation project](https://github.com/ethteck/pokemonsnap) publishes).
  Name it `pokemonsnap.z64` and put it next to `Snap64Recomp.exe` (the
  port reads its own folder, not the working directory; see "Where things
  live"). Byte-swapped `.v64` and little-endian
  `.n64` dumps are accepted -- the runtime detects the order from the header
  and corrects it in memory without touching the file -- but the file name is
  fixed, so rename such a dump to `pokemonsnap.z64`. A missing, unreadable or
  wrong-revision file produces a dialog before the window opens; a wrong dump
  shows both the expected and the actual hash. The ROM is never included
  with this project.
* Beside `Snap64Recomp.exe`: `SDL2.dll`, `dxcompiler.dll`, `dxil.dll` (the
  build places them there; `BUILDING.md`, step 12), and optionally
  `menu_text/recomp_logo.png` for the "Recomp" badge under the title logo (no
  file, no badge).

## Running

Start `Snap64Recomp.exe`; a shortcut works from anywhere, because the port
reads and writes the folder the executable is in, whatever the working
directory (`src/paths.cpp`). It opens a 1280x960 window titled
`Snap64 Recomp 1.0.0-rc1`; `SNAP_WINDOW=WxH` in the environment opens it at
an exact size instead (at least 320x240). The window's maximize button is the
fullscreen switch; the in-game Graphics page and F11 do the same. **Esc
quits.** Saves go to `saves/` and settings to `snapsettings.json`, both next
to the executable. A console window opens beside the game window; it is the
log, and the first thing to include in a bug report.

### Where things live

Everything is in the folder with the executable.

| File or folder | What it is |
| --- | --- |
| `pokemonsnap.z64` | your ROM (you provide it) |
| `snapsettings.json`, `snapsettings.json.bak` | settings, written by the in-game Graphics and Sound pages and by the hotkeys |
| `saves/pokemonsnap.bin`, `saves/pokemonsnap.bin.bak` | the game's save data (its EEPROM image) |
| `photos/` | the photos you save with P or the controller's Back button (see "Photos"); created on the first save |
| `cache/` | RT64's compiled shaders, the driver's pipeline cache and the seen-shader list; safe to delete, the next start is slower |
| `mods/`, `mod_config/` | the runtime's mod folders; created empty, unused by this release |
| `menu_text/recomp_logo.png` | the "Recomp" wordmark on the title screen |
| `Snap64Recomp.map` | the linker map; include it with crash reports (the `[SNAP-AV]` lines in the log are decoded against it) |
| `LICENSE`, `NOTICE.md`, `licenses/` | licences |

Coming from an earlier build: settings, saves and the ROM were already next to
the executable and carry over as they are. Earlier builds kept the shader cache
in `%LOCALAPPDATA%\pokemonsnap`; that folder is no longer read and can be
deleted. The first start after the change rebuilds the cache once.

### Controls

Keyboard (`src/input.cpp`):

| N64 | Key |
| --- | --- |
| Control stick | W A S D |
| A / B | X / Z |
| Z | Left Shift |
| Start | Enter |
| D-pad | Arrow keys |
| L / R | Q / E |
| C-Up / C-Down / C-Left / C-Right | I / K / J / L |

Any SDL game controller overrides the keyboard while attached: left stick is
the control stick, A is A, B or X is B, the left shoulder button is Z, Start
is Start, the D-pad is the D-pad, the triggers are L and R, and the right stick
is the C buttons. The Back button (Select, View or Share on most pads) is not
an N64 button: it saves the photo on screen, as P does on the keyboard (see
"Photos").

### The rule the port follows

**Console behaviour by default; every enhancement is opt-in.** Frame rate,
aspect ratio, anti-aliasing, overscan, the intro's camera hand-off, texture
filtering, dithering: all start as the console had them. What you turn on in
the in-game **Graphics** page (a new item on the game's own Options screen) or
with the hotkeys is what changes, and only that. Two defaults are worth
knowing about because they are not literally the console's: the 3D render
resolution follows the window (`resolution_scale` 0; set 1 for 320x240), and
2D content that would be scaled anyway is drawn sharp (`upscale_2d` 1; set 0
for the original pixels). Both are one setting away from the original.

Two mechanics depend on the game reading back its own rendered frame: photo
scoring re-renders the photographed Pokémon and counts pixels, and the
viewfinder's red focus dot is found by copying tiles of the colour buffer.
Frame interpolation (Frame Rate set to Display or Manual) presents frames the
game never drew; the window title says `interpolation ON (F8)` while it is on.
Photo scoring was measured working with it on (five photos, the game's own
pixel counts reproduced exactly), because the readback uses the frames the
game draws, not the synthetic ones between them. The focus dot under
interpolation has not been re-measured and should be treated as unverified.
Original is the default because it is the console's rate.

### In-game pages

Options > **Graphics**: Render Scale, Anti-Aliasing, Widescreen, Frame Rate,
2D Detail, Filter, Dither, Fullscreen, Super Sampling, Texture Filter, Color
Depth, Buffering, Overscan Crop, Cutscene Fix, Photo Detail, Jynx Recolour.
Color Depth and Buffering take effect after a restart; everything else
applies while the page is open.

Options > **Sound**: Master Volume, Music Volume, Sound Effects, Shutter
Volume, Speaker Output (Stereo/Mono), Background Mute.

### Hotkeys

From `handle_settings_hotkey` in `src/settings.cpp`. Settings hotkeys also
mark the file for writing. Keys marked *diagnostic* exist for investigating
the renderer and are not features.

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
| P | Save the photo on screen as a PNG in `photos/` (see "Photos") |
| Esc | Quit |

### Photos

Every photo the game shows you -- the picks after a course, Oak's check, the
album, the report -- is drawn the same way: the game rebuilds the photo's saved
state as objects and renders them once into a 320x210 buffer in memory (the
size it asks for varies by screen, up to that), then shows that buffer as a
sprite. With render-to-RAM on, which it is unless you turn it off, the rendered
pixels are written back into that buffer, which is what lets the game score
photos at all. **P**, or the controller's **Back** button, saves that buffer's
rendered region as a PNG: the photo at the game's own resolution, pixel for
pixel, with no scaling, no frame and no text over it. Files go to `photos/`
next to the executable, named `snap_YYYYMMDD_HHMMSS_<course>_NN.png` (the
course is left out if the game's own record of it cannot be read), and the log
prints `[SNAP] photo saved: <path>` or the reason it was not: no photo has been
rendered yet, no photo is on screen, or render-to-RAM is off. Nintendo's 2007
Wii Virtual Console release added the same thing -- Select in the album posted
the photo on screen to the Wii Message Board -- so this is an enhancement with
a precedent, and one that draws nothing on screen. The code is
`src/photo_export.cpp`.

### Settings file

`snapsettings.json` is written about 750 ms after the last change (so a slider
costs one write), and on exit if anything is unsaved; the previous file is
kept as `snapsettings.json.bak` and read if the main file is missing or does
not parse. Keys are the field names of `struct Settings` in `src/settings.h`;
the defaults below are that file's.

| Key | Default | Meaning |
| --- | --- | --- |
| `fullscreen` | `false` | not persisted across runs: every boot starts windowed |
| `widescreen` | `false` | RT64 Expand: a true 16:9 field of view, not a stretch |
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
| `jynx_vc` | `false` | Jynx Recolour, Jynx's face and hands: Off: the cartridge's black; On: the purple of the re-releases, from the official artwork |
| `interpolate_camera` | `true` | interpolate the view as well as objects (F4) |
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
(presented-frame capture), and `SNAP_PHOTO_AUTOEXPORT` (with `SNAP_STATS`:
every photo the game renders is saved to `photos/` without a key press, so a
replay can prove the export). They are development switches; the source is
their documentation.

## What has been verified, and what has not

* Verified only in the sense that the developer has played it on the one
  machine above and the renderer, audio, saving, the Graphics and Sound
  pages and the hotkeys listed here all come from the code as it stands
  (`src/settings.cpp`, `src/settings.h`, `src/input.cpp`, `src/main.cpp`).
* There is no test suite, no CI run, and no build on any other machine
  recorded in this repository. Anything not listed here should be assumed
  untried.
* The photo export (P, the controller's Back button, `photos/`) is checked
  by `SNAP_PHOTO_AUTOEXPORT` on an input replay that reaches Oak's check,
  not by hand: saving from the keyboard and from the controller has not been
  tried.
* The recompiled game is generated from a specific decompilation build; the
  chain of tools and inputs is spelled out in `BUILDING.md`, including one
  stale input on the developer's machine that must be regenerated before the
  recompiled code is.

## Building

See `BUILDING.md`. Short version: the decompilation and IDO under WSL,
N64Recomp for the game and the patches, CMake and MSVC on Windows, and a list
of things git does not carry. `cpack -C Release` in the build directory then
writes `Snap64Recomp-1.0.0-rc1-win64.zip` (step 13).

## License

GPLv3. See `LICENSE` and `NOTICE.md`.
