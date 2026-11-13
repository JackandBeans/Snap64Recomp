# Android (beta)

Snap64 Recomp runs on Android as a beta. The app is the same port as on a
PC: the game's own code, recompiled, drawn by RT64 through Vulkan, with the
same Options pages and the same mods. It has run on one phone, a Samsung
Galaxy S25 Ultra with a Snapdragon chip, and on Google's Android emulator.
It has not been played with a real pad yet. Everything below says what was
run and where; anything not listed is untried.

## What you need

* Android 10 or newer, on a 64-bit phone or handheld (arm64). A 32-bit
  phone cannot run it.
* A GPU with a Vulkan 1.2 driver. On a real device only Qualcomm's Adreno
  830 (the Snapdragon 8 Elite) has run the app. Other Adreno chips, Mali
  (most Samsung Exynos phones and Google's Pixels) and PowerVR are
  untried, and so is how fast the game runs on any other phone. A device
  whose driver offers less than Vulkan 1.2 is told so at start, in a
  dialog naming its GPU.
* Your own ROM of the US cartridge, as on a PC. The port checks it and
  refuses a wrong one.

The app also carries an x86_64 build, for Android's emulator.

## Installing

1. On the phone, download `Snap64Recomp-<version>-android.apk` from the
   [Releases](https://github.com/JackandBeans/Snap64Recomp/releases) page.
   That page is the only official download. The app is not in any store.
2. Open the file. The first time, Android asks you to turn on "Install
   unknown apps" for the app you downloaded it with (your browser or file
   manager). Turn it on, go back, and install.
3. Start Snap64 Recomp. The first start explains the game folder,
   **Internal storage > Snap64**, and asks for "All files access" to use
   it. **Allow** opens the system's setting: turn it on for Snap64 Recomp
   and go back. **Not now** uses the app's own folder instead,
   `Android/data/io.github.jackandbeans.snap64recomp/files`, and asks
   again at the next start. **Don't ask again** keeps the app's own
   folder. A tap beside the question does not close it, and Back closes
   the app without an answer, so the next start asks again.
4. Give it your ROM. Either copy it into the Snap64 folder yourself (from
   a PC over USB, or with a file manager), named `pokemonsnap.z64`, or let
   the game ask: it opens the system's file picker, and the file you pick,
   whatever its name, is copied into the folder. `.z64`, `.v64` and `.n64`
   dumps all work.

An update installs over the app you have, and your saves stay. An update
must come from this project's own releases: Android installs it only over
an app signed with the same key. The APK is signed with that key on the
project's release machine, so unlike the other archives it carries no
GitHub build record; the `.sha256` beside it on the release page is the
check.

## The game folder

Internal storage > Snap64 holds what the port's own folder holds on a PC:
the ROM, `saves/`, `photos/`, `mods/`, `texture_packs/`, `snapsettings.json`
and `snap64.log`. A file manager or a PC over USB reaches it. Uninstalling
the app leaves this folder where it is.

Without "All files access", the game folder is the app's own,
`Android/data/io.github.jackandbeans.snap64recomp/files`. A short message
at every start says so ("Game folder: the app's own, in Android/data"). A
file manager may not open that folder, and Android deletes it with the
app. To move to Internal storage > Snap64 later, turn on "All files
access" for Snap64 Recomp in the system settings. The next start copies
the app's own folder into Snap64, saves and photos included, without
replacing any file Snap64 already has.

## Playing

The game runs full screen and stays in landscape. The system's Back
gesture or button opens the port's quit box.

Leaving the game is safe: Home, the recent apps, turning the screen off,
pulling down the notification shade or turning the phone around. The
game waits where it was, and its clock does not move while you are away.

### Touch controls

<a href="screenshots/28-android-touch.png"><img src="screenshots/28-android-touch.png" width="640" alt="The touch controls on the Beach: Start and the four C buttons on the left, R, Z, A and B on the right"></a>

With no pad, the N64's buttons are drawn in the black bars beside the
picture, colored like the N64 controller's. A, B and Start are always
there. In a course, Z, R and the four C buttons join them.

* In a course, drag a finger on the picture to aim, and press A to take
  a photo.
* Tap Z to zoom in. Z stays held, drawn lit with a second ring, so the
  same thumb can go to A. Tap Z again to zoom out. Hold Z instead and it
  zooms only while held, as on the N64. With Options > Controls > Z
  Button set to Switch, Z is a plain button: the game zooms in or out at
  each tap, as it does with a pad.
* R, the dash, works the same way: a tap starts the dash and the next tap
  stops it, and a hold dashes only while held. The game dashes only while
  R is down, and on a phone R shares the thumb with A.
* The first finger that moves on the picture aims, until it lifts. A thumb
  resting on the picture or holding the phone's edge never takes the aim,
  and while one finger aims, another does not move the view.
* Mouse Speed, Zoom Speed and Camera Tilt on the Controls page apply to
  the drag.
* Outside a course, tap a menu item to choose it.

A press on a button can be felt when the phone's Touch feedback setting
is on, and the moment the game takes a photo is felt firmer. Each button takes
touches over at least 48 dp (about a third of an inch), however small it
is drawn; where two buttons' squares overlap, the nearer button takes the
touch. The buttons hide when a pad or a keyboard is used and come back at
the next touch. They stay clear of Android's Back gesture and of a camera
cutout.

The first course after the app is installed shows one line over the
picture for five seconds: "Drag to aim. Tap Z to zoom. A takes the photo."

Options > Controls has six rows for them on Android, after Pointer Shows:

* Touch Controls: Auto shows them until a pad or keyboard is used (the
  default), Always keeps them, Never hides them while a pad is attached.
  With no pad, Never still shows them: a phone has nothing else to press.
* Touch Size: their size, 50% to 150% (default 100%).
* Touch Opacity: how solid they are, 20% to 100% (default 60%).
* Left-Handed: puts R, Z, A and B on the left, and Start and the C
  buttons on the right. The C buttons keep their own left and right.
* Idle Fade: dims them after four seconds without a press. The next press
  brings them back.
* Move Buttons: shows every button over the page, to drag each one where
  your thumb wants it. Reset puts them all back where they came. Done
  keeps the new places, which Left-Handed mirrors too. Nothing reaches the
  game while the buttons are being moved.

B on the page undoes what the first five rows changed, as on every page.
A place kept with Done stays.

On every Options page a tap on a value's arrows changes it (the left one
down, the right one up), a tap on a label or between the arrows selects the
row, and a swipe up or down scrolls the page, so the rows after Pointer
Shows are two swipes away. A swipe stops at the page's first or last row;
it does not wrap around as the wheel does.

They are kept in `snapsettings.json` in the game folder as
`touch_controls` (0 Auto, 1 Always, 2 Never), `touch_size`,
`touch_opacity`, `touch_mirror`, `touch_fade` and `touch_layout`.

With Widescreen on, the picture fills the bars, so the buttons sit over
its outer edges. While they are shown, the film counter stays where the
cartridge puts it, even with Wide HUD on, so R does not cover it; with a
pad or a keyboard in use, Wide HUD moves it to the corner as on a PC.

On a 16:9 screen (1920 x 1080, as on many handhelds) the bars are
narrower than the buttons, so at the default size the left buttons reach
about 80 pixels into the picture and the right ones about 35. That is read
from the layout's arithmetic; no 16:9 screen has run it yet.

### Gyro aim

The phone itself can aim: turn it and the view turns with it, as a pad
with a gyro does on a PC.

* Options > Controls > Gyro Aim turns it on. Zoomed is the one to try
  first: the phone aims only while zoomed in, so you raise it to aim, as
  with a camera. On aims whenever a course runs.
* Gyro Speed scales the turn; Camera Tilt flips the up and down.
* Left and right are measured about the way up, which the phone's own
  accelerometer gives, so a phone held tilted back still turns the view
  left and right.
* With Gyro Aim off, the app does not read the gyroscope at all. A pad
  with its own gyro is used in place of the phone's.

This has been checked on the emulator's virtual sensors only, with the
phone held upright in one landscape: a turn to the left and a raise of
the phone each turned the view as far as the readings said, and a still
phone turned nothing. The tilted hold and the other landscape are read
from the code. No hand has tried it on a phone yet.

### Pads

Pair a Bluetooth pad in Android's own Bluetooth settings first, or plug in
a USB one. It works as a pad does on a PC: the same layout, the same
Button Setup page, and Select opens the Options. The
[manual's Controls](MANUAL.md#controls) lists the buttons. A pad with sticks
is used in place of any device that has none.

This has been checked only with a pad made in software on the emulator,
which Android and the app take for a real one. No real pad has been tried
yet. A report from any pad and phone is welcome.

### Mods

Mods load as on a PC. Put a mod in the game folder's `mods/` folder, or
open the file with Snap64 Recomp from a file manager ("Open with"): it is
installed as a drop is on a PC. Texture packs install the same way; none
has been tried on Android yet. The Mods page's Install row opens the
system's file picker (untried).

## What has been tested

On a Samsung Galaxy S25 Ultra in Samsung's Remote Test Lab (Snapdragon 8
Elite, Adreno 830, Android 15), on Oct 21 2026: the boot, a mod's hooks,
a ride on the Beach with its photos scored as on the PC, the pacing, the
menus and the pages, the settings file, and the ROM chooser. That was
before the touch controls and the return from Home were added: everything
added since has run on the emulator only.

On Google's Android emulator (Android 15), on Oct 21 and 22 2026: all of
the above again, and also the return from Home, the screen off and the
other events above, the touch controls from the intro to a course,
quick taps, Z and R held by a tap and by a hold, Z in the game's Switch
mode, the shutter felt at the game's own photo, a resting
finger beside one that aims, the touch feedback as Android's vibrator
service records it, the 48 dp touch squares at the smallest size, the
first course's hint, Gyro Aim on the emulator's virtual sensors, a pad
made in software, and a display-size change. On Nov 5 2026, every check
again on the 1.1.7 tree, with the Autosave row On and Off on the save file.

On a handheld, by a player: Rhys Wittingslow ran 1.1.6 on his AYN Odin 3
Pro and wrote on Oct 24 2026. The game ran smoothly with its built-in
controls, the sound was fine, he finished the first course's first run,
and every photo got its points and showed up with Oak. Z was mapped to the
left shoulder button (L1) on its own, and the inverted aim he had asked for
was there. He had not tried widescreen or the other settings yet. He also
found the lost first-start question (CHANGELOG, 1.1.7).

[BUILDING.md, step 16](../BUILDING.md#16-android-cross-build-experimental)
has every run with its numbers.

Not tried yet:

* a Bluetooth or USB pad;
* the touch controls on a real screen, by a hand, and their feel;
* Gyro Aim with a phone's own gyroscope, in a hand;
* any GPU but the Adreno 830;
* Android 10 to 14.

## Known limits

* The Snap Station's print is not available. On a PC the port restarts
  itself twice for it, and an Android app cannot (read from the code, not
  tried).
* The Graphics page's Fullscreen row does nothing: the game is always
  full screen on Android.
* The phone's game tools may not shrink the picture or change the frame
  rate: the app offers Android's standard game mode only.
* The emulator draws slower than a phone, so a run there is slower than
  the game's own pace. That is the emulator, not the port.

## Reporting a bug

Open an issue on the
[repository](https://github.com/JackandBeans/Snap64Recomp/issues) with the
phone's model, its Android version, and `snap64.log` from the game folder
(Internal storage > Snap64). To attach the log, open the issue in the
phone's browser and pick the file from that folder, or copy it to a PC
over USB and drag it into the issue. Each start keeps the previous run's
log as `snap64.prev.log`: if the problem was in the run before, attach
that one too.
