# Known issues

What is known not to be right, or not to be finished, and the reason for
each. The short list is on the [front page](../README.md#known-issues). If
you meet something that is not here, [a report](../README.md#reporting-a-bug)
is welcome.

## Some 2D pictures still move at the game's rate

This shows when the frame rate is raised. The interpolation pairs what it
draws by name, and a few things are drawn without one: the photo panels,
Oak's thumbnails, and full-screen backgrounds while they slide during a
transition. They step at the game's own rate when they move. Named sprites
and menu frames, the HUD and the fades do interpolate.

## Parts of the Snap Station printer's look are estimates

No source records the printer's own character set, so its lettering is set
from a typeface of the same construction. Its pass timings are estimated
from footage without a clock. [SNAP-STATION.md](SNAP-STATION.md) says what
the display was measured from.

## A Pokémon can pop out of the picture before it has fully left it

The console does the same. Each frame, the cartridge decides whether to
draw a Pokémon. It projects the Pokémon's collision point and tests it
against a box 1.5 times the half-screen each way (±240 by ±180 pixels around
the centre; `func_80364618_504A28`). The seven `renderPokemonModelType*`
wrappers skip any Pokémon that fails.

A big, close one still has part of its body in the picture when its centre
crosses that line. Snorlax on the Beach shows it: with the camera pitched up
to the 45-degree limit, it vanishes, and returns as the camera comes down. A
television's overscan hid part of the last sliver. The port keeps the rule
as the cartridge has it; the Widescreen patch widens its horizontal bound to
the wider picture, and nothing else.

## A Nintendo pad over Bluetooth may answer a moment late after a pause

The Switch Online N64 controller is one of these pads. SDL's own driver
handles them. It puts the pad in the report mode that only speaks when a
button or stick moves. After three seconds of silence it declares the pad
gone, and at the next touch it takes it back with a handshake of several
commands.

Since 1.0.2 that runs on the port's pad thread and holds nothing else. Even
so, the first press after a pause may arrive a moment late, and each return
is a fresh `Opened game controller` line in `snap64.log`. This is read from
SDL's code and has not been seen on such a pad here; a report from one would
settle it.

## Vulkan on Windows is lightly tested

Vulkan (`graphics_api` 1) is RT64's other backend, and the one the Linux
build uses. On Windows it has been run through the replays on an AMD card,
and not played through by hand. It exists for a machine whose Direct3D 12
path fails.

To try it, add `"graphics_api": 1` to `snapsettings.json` next to the
executable, and start the port again. If the file does not exist, create it
with just `{"graphics_api": 1}` in it; a key the file lacks keeps its
default.

## With Overscan Crop off, a thin light strip shows at the left of the lab's panel

With the crop off, the whole 320x240 frame is on screen, including the
columns and rows a television hid, and some of the game's own art has edges
there. The lab backdrop's leftmost pixel column and top row are pale in the
picture itself. The console drew the same pixels; they were checked against
the picture as the game holds it in memory. Overscan Crop (F2) is the
television's view.
