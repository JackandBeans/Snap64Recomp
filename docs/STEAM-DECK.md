# Linux and Steam Deck


The release page carries the Windows build and, from 1.0.1, a native Linux
build marked experimental. Two ways onto a Deck exist: the Windows build
through Proton, which players ran on the day of the 1.0.0 release, and the
native build, which has been played on a Steam Deck in Desktop Mode by me
on 2026-09-06 -- the menus, the Beach, gyro aim and the settings all as on
Windows, with one known blemish noted below. Both are described here as
they stand on that date; the Gaming Mode instructions follow what the other
N64 recompilations' players do, and every "untested" below means exactly
that.

## The Windows build through Proton

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
the native build stuttered in Gaming Mode on an OLED Deck in a 1.0.4
test, part of it the 16:9 surface the Deck steps below explain, and what
remains is unmeasured.

## The native Linux build

The same source builds on Linux with Clang or GCC, rendering through
Vulkan, and packs as `Snap64Recomp-<version>-linux-x86_64.tar.gz`
([BUILDING, step 14](../BUILDING.md#14-linux-build-experimental)). It has
played the Beach replay under WSL and, on 2026-09-06, a Steam Deck in
Desktop Mode. The blemish known there since 1.0.1 -- a chunk in the
top-left corner during a course's first seconds in Widescreen -- was the
Deck's Mesa driver culling one triangle of the sky in its NGG stage;
from 1.0.5 the port asks the driver to leave that culling off
(`RADV_DEBUG=nonggc`, set before it starts Vulkan and kept alongside
anything you set yourself), which my Deck confirmed draws the
corner whole. No Linux desktop has run the build yet. It is used like
this:

1. Unpack the tarball under your home folder and put `pokemonsnap.z64`
   beside `Snap64Recomp`. That folder is where everything lives, as on
   Windows. If the folder cannot be written (a read-only mount, a system
   directory), the files go to `~/.config/Snap64Recomp` (or
   `$XDG_CONFIG_HOME/Snap64Recomp`) and the ROM is looked for there; the
   log's first line names the folder in use, and `SNAP_DATA_DIR` in the
   environment names one outright. The files the port ships beside
   `Snap64Recomp` (`gamecontrollerdb.txt`, `menu_text/`, the window icon)
   are still read from beside it in that case, the seen-shader list is copied
   into the data directory once, and a `gamecontrollerdb.txt` or a
   `menu_text/` put in the data directory is read too and wins. The log is
   `snap64.log` in the data directory on every launch, whatever the port
   was started from; a terminal shows the lines as well.
2. On a Deck, run it from Desktop Mode; or right-click `Snap64Recomp`,
   choose **Add to Steam**, and launch it from Gaming Mode. Steam gives a
   non-Steam shortcut a screen of its own choosing there (3840x2160 on an
   OLED Deck in the port's tests), which gamescope scales onto the 16:10
   panel with bars, and which the port would render at its full 8x cap;
   the port asks gamescope for the panel's own size itself at every
   start, the same request the shortcut's Game Resolution set to Native
   makes, and the log says what it asked, what the screen then is, and
   each size the surface takes. If the picture still shows bars top and
   bottom after a few seconds, set that Game Resolution to Native and
   send the log.
   In Gaming Mode Steam offers the game only its own virtual pad, so gyro
   aim has to come from Steam's layout ([Controls](MANUAL.md#controls)). The port boots
   fullscreen on a Deck (F11 or the maximize button leaves it); any
   other machine boots as its settings file says, windowed until the
   file says fullscreen.

It needs the system's SDL2 (2.26 or newer), GTK 3 and a Vulkan 1.2 driver,
and a glibc no older than the one it was built against (2.39; SteamOS 3.8
ships 2.41). The C++ runtime is inside the binary. Rendering is Vulkan
only, whatever `graphics_api` in the settings file says. If a Linux
desktop shows no controller under Steam Input, that is a known problem of
the whole family of ports outside the Deck; the Deck itself is reported
fine.

## On a Deck, either way

* **Controls.** The Deck's controls arrive as an Xbox-style pad: A is A,
  B or X is B, the left bumper is Z, Start is Start, the triggers are L
  and R, the right stick is the C buttons and, pressed in, saves the photo
  on screen, the D-pad is the D-pad, and the View button opens the port's
  Options ([Controls](MANUAL.md#controls)). Back paddles,
  trackpads and gyro reach the game only as whatever Steam's controller
  layout maps them to.
* **The icon.** A Linux binary cannot carry one, where the Windows
  executable carries two: the whole logo on a tile, and the film canister
  alone for the title bar. So both ship beside the binary. The window
  takes `Snap64Recomp-window.png`, the canister, at start-up. A Steam
  shortcut shows `Snap64Recomp.png`, the tile, once you pick that file in
  the shortcut's Properties (the artwork Steam shows in Gaming Mode is a
  separate choice there, and yours to make). And the port writes a
  `Snap64Recomp.desktop` launcher on the first start, beside itself when
  that folder can be written and in the data directory otherwise, with
  the tile as its icon and the executable's folder as its working
  directory: that is what a file manager or an application menu can show
  a logo for -- run it as it is, or copy it to
  `~/.local/share/applications`.
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
  what Steam's switched-off sensor looks like ([Controls](MANUAL.md#controls)); the port
  now sends the switch itself, and the second run's readings came alive
  within a dozen of it. Which of the two you are in shows in the log's
  controller line.
* **Quitting.** **Exit Game**, the last row of the game's Options screen,
  closes the port from the pad (A asks, a second A closes, B stays). The
  Steam menu's Exit Game and a held Esc on a keyboard still work too. The
  quit question of a held Esc and any start-up error appear
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
