# The headset


Snap64 Recomp can draw for a VR headset. It is off as shipped and changes
nothing when off; on, the port opens an OpenXR session on whatever runtime
is active on the PC (Meta Quest Link for a Quest over a cable or Air Link,
SteamVR for an Index or anything SteamVR drives, Virtual Desktop's own
runtime) and the same game is played from inside it. Windows and Direct3D
12 only for now; the Linux build and the Vulkan renderer run flat.

## What it does

The game's own camera follows your head. Each tick the port writes the
head's turn into the yaw and pitch the cartridge's camera code reads from
the Control Stick, so the ride camera does exactly what it does for a stick
held at that angle: the photo you take is the game's own frame, scored by
the game's own rules, and every Pokemon reacts as it did on the console.
The pitch keeps the limits the course set (about 22 degrees down and 45
up), so the photo cannot look further down than the cartridge allowed; your
eyes can.

The world is stereo. The renderer draws every frame twice more, once per
eye, with the ride camera's view and projection replaced by the eye's own,
at the size and field of view the headset asks for, with the same
antialiasing as the desktop picture. Everything the game draws flat -- the
film counter, the item icons, the fades -- sits on a plane two metres in
front of you. Frame interpolation keeps the world moving at the headset's
rate (72, 80, 90 or 120 Hz on a Quest, whatever the Link app is set to);
the game itself runs at its thirty.

Outside a course, and during a course's opening and closing shots, the
picture is a 4:3 screen three metres ahead of the seat, at eye height, the
menus at the size the game drew them. Zoom in (the right grip) and the
game's picture opens as a window in front of your eyes, the viewfinder,
brackets and all, at the size the world is behind it: what the window
shows is what the photo will be. Let the grip go and the window closes.

Everything the game culls by its own rule is drawn wherever your head
turns: the port widens the cartridge's on-screen test for drawing while
the headset shows the world, and leaves it as the cartridge had it for the
photo's list of subjects, so the score is the console's. What is behind
you is what the console kept in memory; a course loads its blocks around
the cart as it always did, and far behind the cart there is nothing to
see.

## Setting it up

1. Have the headset's PC software installed and its OpenXR runtime set as
   the active one: in the Meta Quest Link app, Settings, General, "Set Meta
   Quest Link as active"; SteamVR has the same switch under its OpenXR
   settings. Either works with a Quest over Link.
2. Put the headset on, start Link (or Air Link), and start the game with
   the headset on: `"vr": true` in `snapsettings.json`, or `SNAP_VR=1` in
   the environment (`SNAP_VR=0` forces it off whatever the file says). The
   first line the log writes about it names the runtime, the second the
   headset; a run started without a headset says so and runs flat.
3. Sit facing the way you want to face. The view is recentred when the
   headset first focuses, and again whenever you hold the left stick's
   click for a second.

The controllers, on a Quest's Touch: the right trigger takes the photo (A)
and throws an apple when not zoomed, as A does; the right grip zooms (Z);
the right A button throws a pester ball (B); the right B button plays the
flute (C-Down); the left grip is the Dash Engine (R); Y or the menu button
is Start; the left stick is the Control Stick in the menus, and its click
held recentres. Index and Vive controllers get the same layout on their
own buttons. The keyboard, the mouse and a pad keep working beside them.

The settings file's `vr_world_scale` is the game's units per metre, which
sets how far apart your eyes are in the world and how far your head moves
when you lean: eighty puts the eyes at a seated height above the cart, a
smaller number makes the world larger. `vr_render_scale` scales the eye
pictures against what the runtime recommends, and `vr_screen_width` is the
menu screen's width in metres. `SNAP_VR_TRACE=1` in the environment writes
a line every two seconds with the session's state, the head's turn and
what each frame carried.

## What has and has not been done

This is work in progress, on the branch and not in a release. I have run
it three times in my own Quest 3 over a Link cable, on the Beach and in the
Tunnel, and each run found faults the next build fixed: the first ride was
white flashing over the course and garbled static over the intro, which
was the world being drawn through the plane meant for flat content
whenever the ride camera was not recognised; the second froze when the
zoom was toggled, which was the game's own display list buffer overrun by
a cull I had widened too far, and showed effect sprites at arm's length
whatever their distance. The third build has the ride itself smooth, the
intro on the flat screen, the sprites at their own depth, and the cull
bounded; what it looks like on the next ride is what this page will say
next. Without a headset connected the port names the runtime, says so, and
runs the scoring replay flat with the same 45 photos as with the setting
off, which the release suite checks. The hands, a camera held in one of
them, apples thrown by hand and the ZERO-ONE around you are the stage
after this one.
