/**
 * Replaces func_80354FB8_4F53C8 from src/app_level/player.c -- the Beach
 * tutorial's watch for the Control Stick.
 *
 * The original counts frames and reports whether the stick left its centre
 * by six or more on any of them:
 *
 *     while (duration > 0) {
 *         if (ABS(gContInputStickX) >= 6 || ABS(gContInputStickY) >= 6) s1 = true;
 *         if (!IsPaused) duration--;
 *         ohWait(1);
 *     }
 *
 * The tutorial calls it twice: ninety frames after "Try to take a lot of
 * Pokemon pictures!", and six hundred frames after that; if the stick was
 * never used in those ten seconds, the game pauses and asks "Please use the
 * Control Stick."
 *
 * The port's mouse and gyro aiming turn the view directly (src/input.cpp,
 * apply_mouse_look writes the yaw and pitch the stick would have moved),
 * so a player already looking around that way never touched the stick and
 * was asked for it (the port's author, on a Steam Deck with the gyro,
 * 2026-09-12). The port reports a turn it applied in mailbox byte
 * 0x80C0003B on every frame -- one while the view turned by a deliberate
 * amount, zero otherwise -- and this check takes that as the stick. With
 * the mouse and the gyro off the byte stays zero and the check is the
 * ROM's; the counts, the pause and the message are the ROM's in every
 * case.
 */

#include "common.h"
#include "sys/cont.h"
#include "sys/oh.h"

extern u8 IsPaused;

/* The port's settings mailbox at 0x80C00000; this byte is the view turned
 * by mouse or gyro this frame, written by the port (src/input.cpp). */
#define SNAP_VIEW_TURNED (*(volatile u8*) 0x80C0003B)

s32 func_80354FB8_4F53C8(s32 duration) {
    s32 s1 = false;

    while (duration > 0) {
        if (ABS(gContInputStickX) >= 6 || ABS(gContInputStickY) >= 6 || SNAP_VIEW_TURNED != 0) {
            s1 = true;
        }
        if (!IsPaused) {
            duration--;
        }
        ohWait(1);
    }
    return s1;
}
