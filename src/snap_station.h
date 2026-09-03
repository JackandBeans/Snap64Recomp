/**
 * @file snap_station.h
 * @brief The Pokemon Snap Station, emulated on controller port 4.
 *
 * The Snap Station was the Blockbuster Video (and, in Japan, Lawson) kiosk
 * of 1999 that printed a player's photos as a sheet of sixteen stickers.
 * Inside it a Nintendo 64 ran the ordinary retail cartridge with the
 * Expansion Pak, and the printer hung off controller port 4, where the game
 * talks to it as if it were a Controller Pak: 32-byte reads and writes at
 * two addresses, 0x8000 to identify the device and 0xC000 to exchange one
 * message byte. Nothing of the kiosk is in the ROM except the code that
 * drives it, and all of that code is in every retail cartridge (decomp
 * src/sys/cont.c, src/gallery/9FAC10.c, src/AA18E0.c, src/app_render/46270.c).
 * The protocol itself was recovered without a station by James Chambers
 * in 2021 (jamchamb.net/2021/08/17/snap-station.html) and matches the
 * decompilation line for line.
 *
 * What the game does, and what this file answers:
 *
 *   detection   contInitialize at boot and contDetectDevices every few ticks
 *               probe each port whose status byte says a pak is present:
 *               write 32 bytes of 0xFE to 0x8000, read back (a pak echoes
 *               FE; the station must not), write 32 bytes of 0x85, read
 *               back, and when the last byte is 0x85 the port is the
 *               printer (CONT_DEV_TYPE_PRINTER). contIsPrinterAvailable()
 *               asks whether port 4 is one. The Gallery watches that every
 *               frame and shows its Print button when it becomes true.
 *   printing    Print saves the four chosen photos into the cartridge save
 *               (func_800BF244_5C0E4) between message bytes 0xCC and 0x33,
 *               then sends 0x5A and waits, reading 0xC000 until the byte is
 *               no longer 0x08, for the station to reset the console.
 *   display     After the reset, with the station present at boot,
 *               start_scene_manager runs the photo display mode instead of
 *               the title: a 640x480 scene (the reason the kiosk needed the
 *               Expansion Pak, and the reason for the RAM test in
 *               func_8009B2BC) that draws the sixteen sticker slots one
 *               after another, sending 0x01 before the first, 0x02 after
 *               each is on screen, and 0x04 when done; the station's
 *               printer captured the video output at each 0x02. The slot
 *               layout is the game's own table (ROM 0xAAA508): a 4x4 sheet
 *               where each of the four photos fills a 2x2 block. With fewer
 *               than four photos the game sends 0x10 instead and stops.
 *   busy        A 0xC000 read answered with 0x08 makes the game wait and
 *               ask again; the station uses it to hold the game while it
 *               captures, and forever after 0x5A until the reset.
 *
 * Here the reset is a relaunch of this executable: the four photos are in
 * the save file, which the runtime has flushed before the relaunch happens,
 * and a marker file beside the executable tells the new process to have the
 * station present from its first instruction so that the game's own boot
 * chooses the display mode. Each 0x02 captures the frame the game has on
 * screen in two forms: the 640x480 RGBA16 framebuffer the VI is scanning
 * out, which is what the printer received, and the renderer's presented
 * frame at the player's resolution. After 0x04 the sixteen captures are
 * laid out into the sheet and the game is relaunched again into a normal
 * boot, as the kiosk reset the console a second time. Everything the sheet
 * shows is drawn by the game from the player's own save; no artwork ships
 * with the port.
 *
 * Off by default (settings.h, snap_station); the console had no station.
 * The title screen's fifth item, "Snap Station", attaches it for one run
 * without the setting (patches/src/graphics_menu_patch.c, the title section),
 * and goes to the Gallery, where the game's own Print button appears. When
 * the print's second relaunch boots normally, the sheet's folder is opened
 * for the player, the way the kiosk handed over the stickers.
 */
#ifndef SNAP_STATION_H
#define SNAP_STATION_H

#include <cstdint>

#include "recomp.h"

namespace snap {

// Reads the setting and the job marker; must run once before the game
// starts, on the main thread. Waits for the previous instance named in the
// marker to exit, so the two never share the save and cache files.
void station_init();

// Follows the setting (settings.cpp on load, the Graphics page on an edit).
void station_set_enabled(bool enabled);

// The title screen's Snap Station item was chosen (menu_assets.cpp relays the
// patch's mailbox byte): port 4 carries the station from now until this
// process ends, whatever the setting says. Nothing is written to the settings.
void station_request_from_title();

// Whether port 4 reports a controller with a pak right now. True from the
// first instruction when a print job is pending, so the game's boot sees the
// station; otherwise only from five seconds after start, so a boot with the
// setting on goes to the title screen and the game's periodic detection finds
// the station afterwards, the way the kiosk's own enable switch was used.
bool station_port4_present();

// The Controller Pak RAM calls the game makes through __osContRamWrite and
// __osContRamRead (src/os_stubs.cpp). Return true when the call was port 4's
// station and *result holds the libultra return value (0 on success).
bool station_ram_write(uint8_t* rdram, int32_t channel, uint32_t address, gpr buffer, int32_t* result);
bool station_ram_read(uint8_t* rdram, int32_t channel, uint32_t address, gpr buffer, int32_t* result);

} // namespace snap

#endif
