# The Snap Station


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
[the game's history](HISTORY.md).

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
(Leonhart's recording of a working kiosk, [Thanks](../README.md#thanks) in the README) has no
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
