<p align="center"><img src="docs/logo.png" width="640" alt="Snap64 Recomp"></p>

<p align="center">
<a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue" alt="License: GPLv3"></a>
<a href="https://github.com/JackandBeans/Snap64Recomp/releases/latest"><img src="https://img.shields.io/github/v/release/JackandBeans/Snap64Recomp?label=release" alt="Latest release"></a>
<a href="https://github.com/JackandBeans/Snap64Recomp/releases"><img src="https://img.shields.io/github/downloads/JackandBeans/Snap64Recomp/total?label=downloads" alt="Downloads"></a>
<img src="https://img.shields.io/badge/platform-Windows%2010%2F11%20x64%20%7C%20Linux%20x86__64%20%7C%20Steam%20Deck-lightgrey" alt="Platform: Windows 10 or 11 x64, Linux x86_64, Steam Deck">
</p>

# Snap64 Recomp

Pokémon Snap, running natively on Windows and Linux. This is my port of
the Nintendo 64 game (US release). The game's own code was translated to C
and compiled for the PC, a method called static recompilation. You bring
the game's data yourself, as a ROM made from your own cartridge.

Out of the box it plays like the cartridge. I have added widescreen, higher
frame rates, mouse and gyro aim, button rebinding, fast forward, photo
export and the Snap Station's printer. Each stays off until you turn it on,
from the game's own Options screen or with a key. There are builds for
Linux and the Steam Deck too.

The port stands on other people's work:

* [N64Recomp](https://github.com/N64Recomp/N64Recomp) translates the game's
  MIPS code into C.
* [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
  (`librecomp` + `ultramodern`) stands in for the console's operating
  system.
* [RT64](https://github.com/rt64/rt64) draws the picture.
* SDL2 provides the window, the input and the sound.

The game's own code runs; the port changes how it is hosted.

This project is not affiliated with, endorsed by or connected to Nintendo,
Creatures Inc., GAME FREAK inc., HAL Laboratory or The Pokémon Company.
Pokémon and Pokémon Snap are their trademarks, and the game is theirs. No
game data is included; you supply your own ROM. The program itself is
compiled from the game's code, which N64Recomp translated from my own
cartridge dump into C. That is how a static recompilation works, and
[NOTICE.md](NOTICE.md) says what is derived from the game and how.

The credits line on the title screen, `JackandBeans (Snap64 Recomp) · v1.0.9`,
is my name, the port's name and its version. I took my name from Jack and
Beans, the team at HAL Laboratory who made the game ([the game's
history](docs/HISTORY.md)). The people and projects this port stands on are
under [Thanks](#thanks).

**Contents:** [Get it running](#get-it-running) ·
[Screenshots](#screenshots) · [What the port adds](#what-the-port-adds) ·
[What you need](#what-you-need) ·
[The rule the port follows](#the-rule-the-port-follows) ·
[Controls](#controls) · [Linux and Steam Deck](#linux-and-steam-deck) ·
[Known limitations](#known-limitations) ·
[What's next](#whats-next) · [Reporting a bug](#reporting-a-bug) ·
[Contributing](#contributing) ·
[What has been verified](#what-has-been-verified) ·
[Building](#building) · [How I made it](#how-i-made-it) ·
[Thanks](#thanks) · [License](#license)

The full documentation is under `docs/`:

* [The manual](docs/MANUAL.md) -- every control, page, hotkey and setting
* [The Snap Station](docs/SNAP-STATION.md)
* [Linux and the Steam Deck](docs/STEAM-DECK.md)
* [What has been verified](docs/VERIFICATION.md)
* [Mods](docs/MODS.md)
* [The game's history](docs/HISTORY.md)
* [The screenshots](docs/SCREENSHOTS.md)

## Get it running

> **In a hurry?** The download holds a `START HERE.txt` with the short
> version. The one thing to know: this port has no launcher and no overlay.
> Everything it adds is inside the game's own **Options** screen. Press Esc,
> or Select on a pad, and it opens over the screen you are on: the title,
> the lab, the course map, a paused course. Widescreen, the frame rate and
> the other enhancements are under Options > Graphics. They all start off,
> set to what the console did.

You need a 64-bit Windows 10 or 11 PC with a graphics driver that supports
Direct3D 12, and your own ROM of the US cartridge. Nothing has to be
installed. (Linux and the Steam Deck are [further
down](#linux-and-steam-deck).) Then:

1. Download `Snap64Recomp-1.0.9-win64.zip` from the
   [Releases](https://github.com/JackandBeans/Snap64Recomp/releases/latest)
   page and unpack it anywhere. It holds one folder,
   `Snap64Recomp-1.0.9-win64`, with `Snap64Recomp.exe` inside.
2. Have your ROM of the US cartridge ready. (A ROM is the cartridge's
   contents, read out into one file.) Put it next to `Snap64Recomp.exe`
   with the name `pokemonsnap.z64`, or just start the program. If no ROM is
   there, the program asks for the file, checks it, and copies it into
   place under that name. It only asks once. You do not have to check the
   file yourself: a wrong one is refused, and the message shows the
   checksum it expected and the one your file has. (That checksum is a
   64-bit hash of the file, not the SHA-1 under "What you need".)
3. Start `Snap64Recomp.exe`. The program is not signed, so Windows may
   first show a "Windows protected your PC" box (SmartScreen). Click "More
   info", then "Run anyway", and it will not ask again. The first start
   takes a little longer than later ones, because the renderer builds the
   shader programs your GPU needs. They are kept in `cache/`, so the next
   start is quick.

A few keys to know first:

* **Esc opens the port's Options** on almost any screen, and closes them
  again. On a pad it is Select. The Graphics and Sound pages are in there.
  [In-game pages](docs/MANUAL.md#in-game-pages) in the manual names the few
  screens where the key does nothing.
* **Enter** is the game's Start. In a course it opens the pause menu:
  Continue, Quit Course, Retry, Options.
* **F11** switches fullscreen on and off, in a course or anywhere else.
  The window's maximize button goes to fullscreen too.
* **To quit**, choose **Exit Game**, the last row of the game's Options
  screen: A asks, a second A closes, B stays. Or **hold Esc for a second
  and let go**. A box asks first, and Enter or Esc keeps you playing, so a
  hand resting on the key cannot end a run.

Saves live in `saves/` next to the program, so keep that folder when you
update. Keyboard and controller mappings are under [Controls](#controls).
If something goes wrong, [Running](docs/MANUAL.md#running) in the manual
says what Windows or an antivirus may object to, and what to attach to a
bug report.

## Screenshots

<table><tr>
<td><a href="docs/screenshots/01j-title-109.png"><img src="docs/screenshots/01j-title-109.png" width="400" alt="The title screen"></a></td>
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

Every picture is the port's own render at 1440p, with Render Scale and
Anti-Aliasing both at 8x on the Graphics page, cropped to the game's
picture. The full set is in [docs/SCREENSHOTS.md](docs/SCREENSHOTS.md),
and each caption there names the release the picture was taken from. It
covers the title and its menu, the courses, the lab, and every page of the
Options screen, Button Setup included. It also has the printer's marks,
Oak's check from the photo choice to the score sheet, the Camera Check, and
the Beach in Widescreen.

## What the port adds

Each of these is a row on the game's own Options screen, or a key. Each
starts off, or at the console's setting. [The manual](docs/MANUAL.md) has
every one in full.

* **The Graphics page** is a new item on the game's Options screen, drawn
  in the game's own font. Its rows: Render Scale, Super Sampling,
  Anti-Aliasing up to 8x, Widescreen, Frame Rate, 2D Detail, Filter,
  Texture Filter, Color Depth, Buffering, Dither, Fullscreen, Overscan
  Crop, Cutscene Fix, Photo Detail and Jynx Recolor.
* **Widescreen** shows more of the course: a wider 16:9 field of view, not
  a stretched picture. Pokémon and effect sprites are kept at the wider
  edges, where the cartridge would have cut them. The title, the lab and
  the menus stay 4:3.
* **Higher frame rates**, matched to your display or set to a rate you
  choose. The game's own logic still runs at its thirty frames a second;
  the renderer draws the frames in between (frame interpolation).
* **Keyboard and mouse.** W A S D and the mouse aim the camera in a course,
  the way a mouse does in any first-person game. Every key, mouse button
  and pad button can be changed on the Button Setup page, or in the
  settings file.
* **Pads.** Anything SDL has a mapping for works, and the community's list
  of more pads ships beside the program. The Switch Online N64 controller
  is recognised by name. A DualSense, a DualShock 4, a Switch Pro
  controller and the Steam Deck can aim by gyro: turn the pad and the view
  turns with it.
* **Fast forward and slow motion**, each while a key is held. The console's
  own clocks run faster or slower, so the game steps through the same
  frames it would have anyway, with the same scores and the same saves.
* **Photo export.** P, or the pad's right stick pressed in, saves the photo
  on screen as a PNG at the game's own resolution. The Wii Virtual Console
  release could post a photo to the Message Board in much the same way.
* **Photo Detail** shows Oak's photos and the album from the renderer's
  full-resolution render, instead of the console's 320x210 pixels.
* **The Snap Station** was the 1999 kiosk that printed your photos as a
  sheet of sixteen stickers. The port emulates it on controller port 4,
  from the cartridge's own code for it. Print gives you the sheet as PNG
  files, with the printer's own display on the way
  ([SNAP-STATION.md](docs/SNAP-STATION.md)).
* **Two things the re-releases changed**, reproduced and off by default.
  One is the Virtual Console's Jynx recolour. The other is a Cutscene Fix
  for the one frame the console drew from inside the player model, at the
  end of two intros.
* **Linux and the Steam Deck.** There is a native Linux build, which I have
  played on a Deck in Desktop Mode and in Gaming Mode. The Windows build
  also runs under Proton ([STEAM-DECK.md](docs/STEAM-DECK.md)).
* **Mods and texture packs.** The runtime's `.nrm` mod loader and RT64's
  texture-pack loader are built in, and run at every start. The game's
  Options screen has a Mods page for them. A
  [mod template](https://github.com/JackandBeans/Snap64RecompModTemplate)
  and the game's [symbol files](https://github.com/JackandBeans/Snap64RecompSyms)
  let anyone write a mod ([MODS.md](docs/MODS.md)). No mods or packs ship
  with the port.

## What you need

* One of these three. Nothing has to be installed, and the download holds
  everything but the game.
  * A 64-bit Windows 10 or 11 PC with a graphics driver that supports
    Direct3D 12.
  * A Linux x86_64 machine with a Vulkan 1.2 driver. The Linux build is
    experimental.
  * A Steam Deck.
* **Your own ROM of the US cartridge.** Its SHA-1 checksum is
  `edc7c49cc568c045fe48be0d18011c30f393cbaf`, the value the
  [decompilation project](https://github.com/ethteck/pokemonsnap)
  publishes. `.z64`, `.v64` and `.n64` files all work. The port checks the
  file for you, and refuses a wrong one with both checksums shown. The ROM
  is not included with this project.

What the executable imports, which GPUs it has run on, and the files that
sit beside it are in [the manual](docs/MANUAL.md#what-you-need-in-detail).

## The rule the port follows

The port behaves like the console by default, and every enhancement is
something you turn on. Frame rate, aspect ratio, anti-aliasing, overscan,
the intro's camera hand-off, texture filtering and dithering all start as
the console had them. Only what you turn on changes, on the in-game
**Graphics** page (a new item on the game's own Options screen) or with the
hotkeys.

Three defaults are not literally the console's, so they are worth knowing
about. Each is one setting away from the original. The name in parentheses
is its key in [the settings file](docs/MANUAL.md#settings-file).

* The 3D picture is drawn at the window's resolution (`resolution_scale` 0;
  set 1 for 320x240).
* 2D content that would be scaled anyway is drawn sharp (`upscale_2d` 1;
  set 0 for the original pixels).
* The finished frame is put on screen with RT64's anti-aliased pixel
  scaling, not raw nearest pixels (`present_filter` 2; set 0 for the
  blocks).

### Higher frame rates and photo scoring

Two parts of the game read back the frame it has just drawn. Photo scoring
draws the photographed Pokémon again and counts its pixels. The
viewfinder's red focus dot is found by copying tiles of the colour buffer.

Frame interpolation (Frame Rate set to Display or Manual) shows frames the
game never drew. While it is on, the window title says
`interpolation ON (F8)`. Colours the game steps once per frame are blended
too. The fade to black between screens is one: it is a full-screen
rectangle whose transparency the game moves once per tick. When the
interpolation matches that draw to the same draw in the frame before, it
blends the colour between the two, so the fade moves at the display's rate
like everything behind it.

I measured photo scoring with interpolation on: five photos, and the game's
own pixel counts came out exactly the same. The readback uses the frames
the game draws, not the ones made in between. The focus dot has not been
measured again under interpolation, so it is still unverified. Original,
the Frame Rate row's first choice, is the default because it is the
console's rate.

## Controls

Keyboard and mouse, as the port ships them:

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

A pad works as soon as it is plugged in:

| N64 | Pad |
| --- | --- |
| Control stick | left stick |
| A | A |
| B | B or X |
| Z | left shoulder button |
| L and R | the triggers |
| C buttons | right stick |

On a pad, the right stick pressed in saves the photo on screen, and Select
opens the port's Options.

Hold Tab, or the right shoulder button, for fast forward. Hold Space, or
press the left stick in, for slow motion. A tap on Esc opens the port's
Options on almost any screen. Esc held for a second and released asks
whether to quit. From a pad, Exit Game, the last row of the Options screen, quits.

Every binding can be changed in the game, on the Controls page's Button
Setup row, or in the settings file's `keys` table. The mouse's speed, the
gyro, the dead zone, the pad's stick layout and which controllers work are
in [the manual](docs/MANUAL.md#controls).

## Linux and Steam Deck

The release page has a native Linux build, marked experimental, beside the
Windows one. On a Steam Deck either route works. Players ran the Windows
build through Proton on the day of the first release. I have played the
native build on my own Deck, in Desktop Mode and in Gaming Mode. The native
build needs the system's SDL2, GTK 3 and a Vulkan 1.2 driver, and renders
through Vulkan only. [STEAM-DECK.md](docs/STEAM-DECK.md) has the steps for
each route, the Deck's controls and gyro, its screen, quitting from a pad,
and the icon.

## Known limitations

* **Some 2D pictures still move at the game's rate** when the frame rate is
  raised. The interpolation pairs what it draws by name, and a few things
  are drawn without one: the photo panels, Oak's thumbnails, and
  full-screen backgrounds while they slide during a transition. Named
  sprites and menu frames, the HUD and the fades do interpolate.
* **Parts of the Snap Station printer's look are estimates.** No source
  records the printer's own character set, so its lettering is set from a
  typeface of the same construction. Its pass timings are estimated from
  footage without a clock.
* **A Pokémon can pop out of the picture before it has fully left it.** The
  console does the same. Each frame, the cartridge decides whether to draw
  a Pokémon. It projects the Pokémon's collision point and tests it against
  a box 1.5 times the half-screen each way (±240 by ±180 pixels around the
  centre; `func_80364618_504A28`). The seven `renderPokemonModelType*`
  wrappers skip any Pokémon that fails. A big, close one still has part of
  its body in the picture when its centre crosses that line. Snorlax on the
  Beach shows it: with the camera pitched up to the 45-degree limit, it
  vanishes, and returns as the camera comes down. A television's overscan
  hid part of the last sliver. The port keeps the rule as the cartridge has
  it; the Widescreen patch widens its horizontal bound to the wider
  picture, and nothing else.
* **A Nintendo pad over Bluetooth may answer a moment late after a pause.**
  The Switch Online N64 controller is one of these. SDL's own driver
  handles them. It puts the pad in the report mode that only speaks when a
  button or stick moves. After three seconds of silence it declares the pad
  gone, and at the next touch it takes it back with a handshake of several
  commands.
  Since 1.0.2 that runs on the port's pad thread and holds nothing else.
  Even so, the first press after a pause may arrive a moment late, and each
  return is a fresh `Opened game controller` line in `snap64.log`. I read
  this from SDL's code and have not seen it on such a pad here; a report
  from one would settle it.
* **Vulkan on Windows is lightly tested.** Vulkan (`graphics_api` 1) is
  RT64's other backend, and the one the Linux build uses. On Windows I have
  run it through the replays on my AMD card, and not played it through by
  hand. It exists for a machine whose Direct3D 12 path fails. To try it,
  add `"graphics_api": 1` to `snapsettings.json` next to the executable,
  and start the port again. If the file does not exist, create it with just
  `{"graphics_api": 1}` in it; a key the file lacks keeps its default.
* **With Overscan Crop off, a thin light strip shows at the left of the
  lab's translucent panel.** With the crop off, the whole 320x240 frame is
  on screen, including the columns and rows a television hid, and some of
  the game's own art has edges there. The lab backdrop's leftmost pixel
  column and top row are pale in the picture itself. The console drew the
  same pixels; I checked them against the picture as the game holds it in
  memory. Overscan Crop (F2) is the television's view.

## What's next

Roughly in the order I plan to work on it.

1. **Reports from machines other than mine.** Each release from 1.0.1 to
   1.0.8 was made of what players reported, and later ones will be too. The
   issue form is the way to send one, with `snap64.log` attached.
2. **A Linux desktop.** The native build has run under WSL and on a Deck,
   and on no Linux desktop yet. I would welcome the first report from one.

The same list, with a place to reply, is pinned under
[Discussions](https://github.com/JackandBeans/Snap64Recomp/discussions/1).

## Reporting a bug

Open an issue at
[github.com/JackandBeans/Snap64Recomp/issues](https://github.com/JackandBeans/Snap64Recomp/issues).
The issue form asks for these:

* `snap64.log` from the run that went wrong; it is written next to the
  executable
* `Snap64Recomp.map`, if the log has `[SNAP-AV]` lines
* your `snapsettings.json`
* what you were doing, and which system, GPU and driver you have

What Windows or an antivirus may object to, and where the port's files
live, are under [Running](docs/MANUAL.md#running) in the manual.

## Contributing

Bug reports, pull requests and mods are all welcome. Most of the releases
since the first were made of what players reported, and the
[changelog](CHANGELOG.md) names who reported each fault beside its fix.
For code, [CONTRIBUTING.md](CONTRIBUTING.md) says what a pull request
needs. For a mod, [MODS.md](docs/MODS.md) and the
[mod template](https://github.com/JackandBeans/Snap64RecompModTemplate)
are the way in. Questions and requests have a place under
[Discussions](https://github.com/JackandBeans/Snap64Recomp/discussions).

## What has been verified

I have play-tested the entire game on my own PC. That is every course from
the Beach to Rainbow Cloud, and every course again with Widescreen on. It
is also Oak's evaluations, the report, the album, the Gallery and a Snap
Station print. On a Steam Deck I have played the Beach, the menus, the
settings and the station.

Every release also goes through the automated suite in
`tools/release_check.py` before it ships. The suite runs:

* the Beach replay, under the player's own conditions
* the recorded run to Oak's evaluation, scoring every photo
* the Options pages, staged from the harvested font
* the first start with no ROM
* the archive's contents
* the two speed keys
* the key that opens the pages from anywhere
* the Snap Station's print, through both relaunches

On the 1.0.9 executable the suite passed 28 of 28 checks, and the station's
5 of 5. [VERIFICATION.md](docs/VERIFICATION.md) says what each check does,
what every release reported, and what has only been read from the code and
not seen.

## Building

See [BUILDING.md](BUILDING.md). Short version: the decompilation and IDO
under WSL, N64Recomp for the game and the patches, CMake and MSVC on
Windows, and a list of things git does not carry
([What a clean checkout is missing](BUILDING.md#what-a-clean-checkout-is-missing)).
`cpack -C Release` in the build directory then writes
`Snap64Recomp-1.0.9-win64.zip` ([step 13](BUILDING.md#13-package)).

## How I made it

### How the port works

N64Recomp reads the game's code out of my own cartridge dump and writes it
out as C, one function at a time, at build time. None of that output is in
this repository. That C is compiled and linked with three other things:

* librecomp and ultramodern, which give it the console's operating system
  calls
* RT64, which turns the game's display lists into Direct3D 12 or Vulkan
* the port's own code under `src/`

The port's own code is the window, input, audio and settings. It is also
the identity the frame interpolation pairs objects and sprites by, the menu
pages composed from the game's own sprite font, the photo export, and the
Snap Station.

For some features the game's own behaviour had to change: the Graphics page
on its Options screen, the fifth title entry, the intro's camera fix. Each
changed function is a copy of its decompiled source under `patches/src`,
compiled with the decompilation's own IDO toolchain and loaded over the
original. [BUILDING.md](BUILDING.md) has the whole chain, with the tools
and inputs at each step.

### Made with Claude Code

I made this port with Claude Code, an AI coding tool. I decide what the
port does, test it on my own PC and Steam Deck, and turn what players
report into the next release. Claude writes the code, the tools and the
documentation in sessions I direct, and every commit made that way names
the model in its `Co-Authored-By` line.

## Thanks

None of this would exist without:

* The [Pokémon Snap decompilation](https://github.com/ethteck/pokemonsnap)
  and everyone who worked on it. The port's patches are compiled against
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
  Snap Station protocol from the cartridge, without a station to test
  against. The port's emulation of the device follows it and the
  decompilation.
* Leonhart, whose recording of a working Snap Station kiosk, ["Using
  Pokemon Snap Station For First Time In 20
  Years!"](https://youtu.be/lCnvpIEVpqo), was the main video reference for
  how the station behaved in use. The printer's display was measured from
  it: the sticker grid, the marks and stars, and the pace of the three
  passes all come from those frames.
* [ido-static-recomp](https://github.com/decompals/ido-static-recomp)
  by the decompals, which lets the decompilation's compiler, and so the
  port's patches, build on a modern machine.
* [SDL2](https://libsdl.org), the DirectX Shader Compiler, and the libraries
  named in `NOTICE.md`.
* The Roboto Project Authors, for the typeface the printer's lettering is
  set from.
* Claude Code, the tool I built this with ([How I made it](#how-i-made-it)).
* Jack and Beans, the team at HAL Laboratory who made the game. Their name,
  shown in the game's opening and at the head of its credits, gave me the
  name I use here.
* [Video Game Esoterica](https://www.youtube.com/@VideoGameEsoterica), whose video on the port,
  ["Pokemon Snap Recomp Out NOW! More Pokemon PC Ports"](https://youtu.be/1ds9leciGU4?si=iLBwSeI-OqSO8NsI), asked
  for a speed multiplier. Fast forward and slow motion came from that ask.
  Thank you for the video, and for the idea.
* Everyone who plays it and reports what they see. The first reports from
  other machines are what 1.0.1 to 1.0.8 were made of, and the next ones
  are what the release after will be.

## License

Copyright (C) 2026 JackandBeans. GPLv3: see `LICENSE` and `NOTICE.md`. The port's own code is mine
and GPLv3; the game is Nintendo's, Creatures', GAME FREAK's and HAL's, and
nothing of it is here.
