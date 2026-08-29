/**
 * @file version.h
 * @brief The port's identity, in one place.
 *
 * "Jack & Beans" is the project the game itself began as at HAL -- the
 * unused JACK and BEANS prototype logo still ships inside the title
 * screen's own data -- which makes it the one name tied to this game
 * that is ours to use. Shown in the log banner and on the title
 * screen's credits line; bump the version here and every surface
 * follows.
 */
#ifndef SNAP_VERSION_H
#define SNAP_VERSION_H

#define SNAP_PORT_NAME    "Jack & Beans"
#define SNAP_PORT_DESC    "Snap64 Recomp"
#define SNAP_PORT_VERSION "1.0.0"

// The title-screen credits line; \x01 is the credits face's middle dot.
#define SNAP_PORT_CREDITS SNAP_PORT_NAME " (" SNAP_PORT_DESC ") \x01 v" SNAP_PORT_VERSION

#endif
