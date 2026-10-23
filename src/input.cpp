/**
 * @file input.cpp
 * @brief SDL2-based input implementation for Snap64 Recomp.
 *
 * Maps SDL2 game controller, keyboard and mouse input to N64 controller
 * state. Supports a single controller (port 0); the keyboard and the mouse
 * are always attached.
 *
 * The keyboard and the mouse go through one binding table, rewritable from
 * the settings file ("keys"; input.h names the inputs and the sources). The
 * layout the port ships with:
 *   A       = X / Mouse Left            (the photo when zoomed, an apple when not)
 *   B       = Z / Mouse Middle          (the pester ball)
 *   Z       = Left Shift / Mouse Right  (zoom: hold or switch, the game's own option)
 *   START   = Return
 *   D-pad   = the arrow keys
 *   L / R   = Q / E                     (R is the dash)
 *   C-Up    = I / Wheel Up              (turn to face behind)
 *   C-Down  = K / Wheel Down            (the Poke Flute)
 *   C-Left  = J / Mouse X1              (turn left)
 *   C-Right = L / Mouse X2              (turn right)
 *   Stick   = W A S D, and the mouse's motion while a course runs
 * The game controller keeps its own fixed mapping: A, B (X as well), the
 * left shoulder as Z, Start, the D-pad, the triggers as L and R, the right
 * stick as the C buttons, the left stick as the stick.
 *
 * The mouse's buttons and wheel work whenever the window has focus, so a
 * click advances Oak's text the way A does. A menu that handles the
 * mouse itself (the title's list, the lab's panel, the pause menu, the
 * Options pages) claims it, and then a click reaches that menu as a click
 * on the item under the pointer instead of as A (the pointer block below).
 * Its motion feeds the game only while it is captured: mouse aim on in the
 * settings, the window focused, a course running (.app_level resident,
 * src/overlay_hook.cpp), and no menu claiming it. Then the cursor is hidden and its motion turns
 * the view directly, the way a mouse does in any first-person game: each
 * pixel is an angle, added to the game's own view yaw and pitch in memory
 * (apply_mouse_look below), not a stick deflection. The stick is a rate in
 * this game, at most about forty degrees a second, and no mapping of
 * motion onto it can feel like a mouse. Outside a course the cursor is
 * free and the motion does nothing. The click that
 * gives the window focus is not a press: buttons are ignored for a quarter
 * second after focus arrives. A click is latched for a little over one
 * game frame so a tap between two of the game's reads is never lost, and
 * lands as exactly one press.
 *
 * Not N64 buttons: SDL_CONTROLLER_BUTTON_BACK (Select on most pads) opens
 * the port's Options from anywhere, as it does in the other
 * recompilations, and while they are up it is Start, which closes them;
 * the right stick pressed in saves the photo on screen (src/photo_export.cpp;
 * until 1.0.9 that was Select, as on the Wii Virtual Console release). The
 * keyboard's Esc and P do the same, through main.cpp and the hotkey table
 * in src/settings.cpp.
 */

#include "input.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <memory>
#include <mutex>
#include <deque>
#include <string>
#include "hle/rt64_snap_diag.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <thread>
#include <SDL2/SDL.h>

#include "ultramodern/ultramodern.hpp"
#include "paths.h"
#include "photo_export.h"
#include "settings.h"
#include "snap_station.h"
#include "touch_controls.h"

// Pokemon Snap port: how many more presented images to photograph. Lives in
// RT64's present queue, where the pictures actually leave for the screen;
// armed from here on a schedule counted in controller readings, because under
// SNAP_REPLAY the reading index is the only clock that lands on the same game
// moment every run.
extern "C" std::atomic<int32_t> snap_frame_dump_pending;

// N64 controller button bits (matching libultra OS_CONT_* defines).
#define N64_BTN_A       0x8000
#define N64_BTN_B       0x4000
#define N64_BTN_Z       0x2000
#define N64_BTN_START   0x1000
#define N64_BTN_DU      0x0800
#define N64_BTN_DD      0x0400
#define N64_BTN_DL      0x0200
#define N64_BTN_DR      0x0100
// 0x0080 and 0x0040 are unused reset/reserved
#define N64_BTN_L       0x0020
#define N64_BTN_R       0x0010
#define N64_BTN_CU      0x0008
#define N64_BTN_CD      0x0004
#define N64_BTN_CL      0x0002
#define N64_BTN_CR      0x0001

namespace snap {

// settings.cpp; set by the overlay hook on the first overlay load, which is
// long before any photo can exist.
extern uint8_t* g_rdram;
// overlay_hook.cpp: true while a course's code overlay is resident.
extern std::atomic<bool> g_app_level_resident;

// The pad. Every SDL controller call the port makes -- opening, polling,
// closing, the sensors, rumble -- runs on one thread of the port's own, the
// pad thread (pad_thread_main below), and nowhere else. It used to run on
// the game's controller thread, the one the game's main thread waits on for
// each frame's input, so a slow controller driver held the whole game:
// SDL's HIDAPI driver for a Nintendo pad over Bluetooth spends seconds in
// its synchronous open and close handshakes, and a Switch Online N64
// controller opened a black window that never drew the boot logo (issue
// #7; the game's side of that is in the CHANGELOG). The game's threads
// read a copy of the pad's state, taken by the pad thread after each poll,
// and the window's thread pumps no controller state at all
// (SDL_HINT_AUTO_UPDATE_JOYSTICKS is off, main.cpp).
static SDL_GameController* game_controller = nullptr;   // pad thread only

// What the game's threads see of the pad: taken by the pad thread, read by
// input_get. The buttons are a bit per SDL_GameControllerButton.
struct PadSnapshot {
    bool attached = false;
    bool n64_layout = false;
    bool nso_n64 = false;       // the Switch Online N64 controller through SDL's driver (g_pad_nso_n64)
    bool r_on_trigger = false;  // its R is its right trigger, under the N64-shaped layout (g_pad_r_on_trigger)
    uint32_t buttons = 0;
    int16_t axes[SDL_CONTROLLER_AXIS_MAX] = {};
};
static std::mutex g_pad_mutex;
static PadSnapshot g_pad;   // guarded by g_pad_mutex

static PadSnapshot pad_snapshot() {
    std::lock_guard<std::mutex> lock(g_pad_mutex);
    return g_pad;
}

#if defined(__ANDROID__)
// The phone's own gyroscope is open (phone_gyro_sync below): its turning
// reaches the view even with the touch controls off (touch_controls.cpp).
std::atomic<bool> g_phone_gyro_on{false};
#endif

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

namespace {

enum InputIndex : int {
    IN_A, IN_B, IN_Z, IN_START, IN_DU, IN_DD, IN_DL, IN_DR, IN_L, IN_R,
    IN_CU, IN_CD, IN_CL, IN_CR, IN_STICK_UP, IN_STICK_DOWN, IN_STICK_LEFT, IN_STICK_RIGHT,
    IN_COUNT
};

const char* const kInputNames[IN_COUNT] = {
    "a", "b", "z", "start", "d_up", "d_down", "d_left", "d_right", "l", "r",
    "c_up", "c_down", "c_left", "c_right", "stick_up", "stick_down", "stick_left", "stick_right",
};

// The port's own keys, the table's nineteenth and twentieth names (input.h).
static const char* const kFastForwardName = "fast_forward";
static const char* const kSlowMotionName = "slow_motion";

const uint16_t kInputBits[IN_CR + 1] = {
    N64_BTN_A, N64_BTN_B, N64_BTN_Z, N64_BTN_START, N64_BTN_DU, N64_BTN_DD, N64_BTN_DL, N64_BTN_DR,
    N64_BTN_L, N64_BTN_R, N64_BTN_CU, N64_BTN_CD, N64_BTN_CL, N64_BTN_CR,
};

enum class SourceKind : uint8_t { Key, MouseButton, WheelUp, WheelDown, PadButton, PadAxis };

// The pad's layout. The defaults name SDL's Xbox-style parts: the left
// shoulder is Z, the triggers are L and R. A pad shaped like the N64's --
// the Switch Online N64 controller, which SDL calls "Nintendo N64
// Controller" -- reports its L and R as the shoulders and its Z as the left
// trigger, so under the defaults its L acted as Z, its Z as L and its R as
// nothing. Under this layout shoulders and triggers change roles, and every
// binding that names one reads the other; the C buttons already arrive as
// the right stick, which the port has always read as C. Chosen at the
// pad's opening, and again when pad_layout changes (decide_pad_layout).
// Written by the pad thread, read by the window's (capture_event).
static std::atomic<bool> g_pad_layout_n64{false};
// The Switch Online N64 controller through SDL's own driver (its name is
// "Nintendo N64 Controller" there; through the mapping table it is the
// "NSO N64 Controller", with the C buttons as a right stick): it has no
// right stick, and its C buttons arrive as Y, X, Back and the right
// trigger. They are the C buttons here, whatever the tables say (issue
// #26, Succulent-Puppet: C-down ran the fast forward, C-left threw a
// pester ball, C-right opened the Options).
static std::atomic<bool> g_pad_nso_n64{false};
// Under the N64-shaped layout, a pad whose mapping has a right trigger and
// no right shoulder. An N64 pad has one button on that side, so that is its
// R, and the swap left R reading nothing and the fast forward on R (the
// Mayflash N64 Adapter 0079:1879 in the shipped list). That side is read as
// it is.
static std::atomic<bool> g_pad_r_on_trigger{false};
// A pad with no right stick to press in: its Capture or Share button (SDL's
// misc1) saves the photo instead (photo_button_watch), which is the Switch
// Online N64 controller's Capture. Where the stick can be pressed in, misc1
// stays unread: it is the DualSense's microphone button and the Deck's ...
// button.
static std::atomic<bool> g_pad_misc1_photo{false};
// The pad_layout the flags above were decided under, -1 before any pad has
// opened (pad thread only).
static int g_pad_layout_applied = -1;

// On a Steam Deck whose own controller is attached, the keys Steam's
// desktop layout sends for the pad's buttons are ignored. Launched outside
// a Steam shortcut while Steam runs, the layout sends Enter for A and
// Escape for B along with the pad's own buttons, so a shot opened the
// pause menu (Return is Start) and B kept bringing it back (Escape is the
// pause key). It also sends Tab for Select, Space for Y and Left Shift for
// the upper left paddle (the layout as the Deck ships it), so Select ran
// the fast forward as it opened the Options (seen by JackandBeans on his
// Deck, Oct 20 2026, the 1.1.5 Flatpak in Desktop Mode), and Y and the
// paddle would run the slow motion and Z the same way. The arrow keys it
// sends for the D-pad are the D-pad's own and stay. No key or mouse press
// changes the device the help lines name while that pad is attached
// (note_press_device): scrolling a page flickered the Button Setup row's
// "Press A" to the keyboard's "Press X" (JackandBeans's Deck, Oct 22 2026,
// Desktop Mode). Through a Steam shortcut the layout is the gamepad one and
// nothing is sent; the pad is then Steam's virtual one, not the Deck's, and
// the keys stay live.
// Written by the pad thread at open and close, read by the game's threads
// (source_down) and the window's (main.cpp).
static std::atomic<bool> g_deck_pad_keys_ignored{false};

// The keys that layout sends for a button, as the Deck ships.
static bool deck_layout_key(int scancode) {
    return (scancode == SDL_SCANCODE_RETURN) || (scancode == SDL_SCANCODE_ESCAPE) ||
           (scancode == SDL_SCANCODE_TAB) || (scancode == SDL_SCANCODE_SPACE) ||
           (scancode == SDL_SCANCODE_LSHIFT);
}

// The layout also clicks the mouse for the triggers: the left button for
// the right trigger, the right button for the left one (his Deck's trace,
// Oct 22 2026: a left click 12 ms after the right trigger's own reading).
// The port reads those buttons as A and Z, and out of a course as the
// menus' click and Back, so R2 could press A and L2 zoom. While the Deck's
// own pad is attached, a press of either with its trigger past an eighth of
// its travel is that echo. SDL 2 gives every mouse the same id, so the
// click alone cannot tell; a click with no trigger held is a hand's.
static bool deck_trigger_click(int button) {
    if (!g_deck_pad_keys_ignored.load(std::memory_order_relaxed)) {
        return false;
    }
    const int axis = (button == SDL_BUTTON_LEFT)    ? SDL_CONTROLLER_AXIS_TRIGGERRIGHT
                     : (button == SDL_BUTTON_RIGHT) ? SDL_CONTROLLER_AXIS_TRIGGERLEFT
                                                    : -1;
    if (axis < 0) {
        return false;
    }
    const PadSnapshot pad = pad_snapshot();
    return pad.attached && (pad.axes[axis] > 4096);
}

// The buttons whose press was a trigger's click, so that their release is
// dropped as well (the window's thread only).
static uint32_t g_trigger_clicks = 0;

// The device of the last press (input_last_device). The window's thread writes
// it from the events; the game's thread reads it.
static std::atomic<int> g_last_device{kBindPad};

// Presses of the Button Setup page's clear keys (input_take_clear_key).
static std::atomic<uint32_t> g_clear_key_presses{0};

// The input trace (input_trace_on): SNAP_INPUT_TRACE=1, and by itself while
// the Deck's own pad is attached, where Steam's desktop layout echoes the
// pad as keys and a pointer. Each press, wheel turn and stick past half its
// travel, with the device the help lines then name; the first 300.
static const bool g_input_trace_env = (getenv("SNAP_INPUT_TRACE") != nullptr);

static bool trace_on() {
    return g_input_trace_env || g_deck_pad_keys_ignored.load(std::memory_order_relaxed);
}

static const char* device_word(int device) {
    return (device == kBindPad) ? "pad" : (device == kBindMouse) ? "mouse" : "keyboard";
}

static const char* or_unnamed(const char* name) {
    return ((name != nullptr) && (name[0] != '\0')) ? name : "(unnamed)";
}

static void note_press_device(const SDL_Event& event) {
    int device = -1;
    char what[80] = {};
    switch (event.type) {
        case SDL_KEYDOWN:
            if (!event.key.repeat) {
                device = kBindKeyboard;
                snprintf(what, sizeof(what), "key %s", or_unnamed(SDL_GetScancodeName(event.key.keysym.scancode)));
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.which != SDL_TOUCH_MOUSEID) {
                device = kBindMouse;
                snprintf(what, sizeof(what), "mouse button %d", int(event.button.button));
            }
            break;
        case SDL_MOUSEWHEEL:
            if (event.wheel.which != SDL_TOUCH_MOUSEID) {
                device = kBindMouse;
                snprintf(what, sizeof(what), "wheel %d,%d", int(event.wheel.x), int(event.wheel.y));
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            device = kBindPad;
            snprintf(what, sizeof(what), "pad %s", or_unnamed(SDL_GameControllerGetStringForButton(SDL_GameControllerButton(event.cbutton.button))));
            break;
        case SDL_CONTROLLERAXISMOTION: {
            // Past half its travel: a stick at rest, or drifting, is no press.
            // The trace names a stick or trigger as it goes past, not after.
            static bool past[SDL_CONTROLLER_AXIS_MAX] = {};
            const bool now = (event.caxis.value > 16000) || (event.caxis.value < -16000);
            if (now) {
                device = kBindPad;
            }
            if ((event.caxis.axis < SDL_CONTROLLER_AXIS_MAX) && (now != past[event.caxis.axis])) {
                past[event.caxis.axis] = now;
                if (now) {
                    snprintf(what, sizeof(what), "pad %s %d", or_unnamed(SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis(event.caxis.axis))),
                             int(event.caxis.value));
                }
            }
            break;
        }
        default:
            break;
    }
    // On a Steam Deck whose own pad is attached, the pad is the hand: Steam's
    // desktop layout sends keys and a pointer for its buttons, D-pad and
    // sticks, and each one named the keyboard's or the mouse's buttons for an
    // instant. A key or mouse still does its job; the words stay the pad's.
    const bool deck = g_deck_pad_keys_ignored.load(std::memory_order_relaxed);
    if (deck && ((device == kBindKeyboard) || (device == kBindMouse))) {
        device = -1;
    }
    const int was = g_last_device.load(std::memory_order_relaxed);
    if (device >= 0) {
        g_last_device.store(device, std::memory_order_relaxed);
    }
    static int traced = 0;
    if ((what[0] != '\0') && (traced < 300) && trace_on()) {
        traced++;
        const int named = (device >= 0) ? device : was;
        printf("[SNAP-Input] trace %u ms: %s; the help lines name the %s%s\n", unsigned(SDL_GetTicks()), what,
               device_word(named), (named != was) ? " (changed)" : "");
        fflush(stdout);
    }
}

// The pad's buttons and axes, out of the snapshot the pad thread took.
static bool snapshot_button(const PadSnapshot& pad, int code) {
    return (code >= 0) && (code < SDL_CONTROLLER_BUTTON_MAX) && (((pad.buttons >> code) & 1u) != 0);
}

static int16_t snapshot_axis(const PadSnapshot& pad, int code) {
    return ((code >= 0) && (code < SDL_CONTROLLER_AXIS_MAX)) ? pad.axes[code] : int16_t(0);
}

static bool pad_button_held(const PadSnapshot& pad, int code) {
    if (!pad.attached) {
        return false;
    }
    if (pad.n64_layout) {
        if (code == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            return snapshot_axis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
        }
        if (code == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            // The Switch Online N64 controller's right trigger is C-down,
            // and its ZR comes as the left stick pressed in (SDL's driver;
            // Linux's hid-nintendo reads the same bit as ZR).
            if (pad.nso_n64) {
                return snapshot_button(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK);
            }
            if (pad.r_on_trigger) {
                return false;   // that trigger is R, read by the trigger's name
            }
            return snapshot_axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        }
        if ((code == SDL_CONTROLLER_BUTTON_LEFTSTICK) && pad.nso_n64) {
            return false;       // that pad has no stick to press in: the bit is its ZR
        }
    }
    return snapshot_button(pad, code);
}

static bool pad_axis_held(const PadSnapshot& pad, int code) {
    if (!pad.attached) {
        return false;
    }
    if (pad.n64_layout) {
        if (code == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            return snapshot_button(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        }
        if ((code == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) && !pad.r_on_trigger) {
            return snapshot_button(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        }
    }
    // A trigger counts as held past the same eighth of its travel the
    // hardcoded mapping used. A stick axis bound to a button behaves the
    // same way, in its positive direction.
    return snapshot_axis(pad, code) > 8000;
}

struct Source {
    SourceKind kind;
    int code;   // SDL_Scancode, the SDL mouse button index, or the pad's
                // SDL_GameControllerButton / SDL_GameControllerAxis
};

struct Resolved {
    std::vector<Source> sources[IN_COUNT];
    std::vector<Source> fast_forward;   // the fast_forward entry's (input_fast_forward_held)
    std::vector<Source> slow_motion;    // the slow_motion entry's (input_slow_motion_held)
};

const Bindings& defaults() {
    static const Bindings table = {
        // Keyboard, mouse and pad in one list per input: a name that starts
        // with "Pad " is the controller's, everything else is a key or a
        // mouse button. The pad entries below are exactly the mapping that
        // used to be written into input_get, so a player who never edits the
        // file feels no difference.
        {"a",           {"X", "Mouse Left", "Pad A"}},
        {"b",           {"Z", "Mouse Middle", "Pad B", "Pad X"}},
        {"z",           {"Left Shift", "Mouse Right", "Pad LeftShoulder"}},
        {"start",       {"Return", "Pad Start"}},
        {"d_up",        {"Up", "Pad DPUp"}},
        {"d_down",      {"Down", "Pad DPDown"}},
        {"d_left",      {"Left", "Pad DPLeft"}},
        {"d_right",     {"Right", "Pad DPRight"}},
        {"l",           {"Pad LeftTrigger"}},   // the game never reads L: no key on the keyboard
        {"r",           {"E", "Pad RightTrigger"}},
        {"c_up",        {"I", "Wheel Up"}},
        {"c_down",      {"K", "Wheel Down"}},
        {"c_left",      {"J", "Mouse X1"}},
        {"c_right",     {"L", "Mouse X2"}},
        {"stick_up",    {"W"}},
        {"stick_down",  {"S"}},
        {"stick_left",  {"A"}},
        {"stick_right", {"D"}},
        // Not an N64 input: the port's own fast-forward key
        // (src/fast_forward.cpp). Tab is the emulators' key for it; the
        // right shoulder is the one pad button the game leaves free (the
        // triggers are L and R, the left shoulder Z); on an N64-shaped pad
        // it reads the right trigger, its ZR where it has one, and on the
        // Switch Online N64 controller that pad's ZR (pad_button_held).
        {kFastForwardName, {"Tab", "Pad RightShoulder"}},
        // The slow-motion key: held while the right thumb lines up A, so
        // it lives on the left hand -- Space, and the left stick pressed
        // in, the one unused control there (none on the Switch Online N64
        // controller, where that bit is its ZR).
        {kSlowMotionName, {"Space", "Pad LeftStick"}},
    };
    return table;
}

bool resolve_source(const std::string& name, Source& out) {
    struct { const char* name; SourceKind kind; int code; } mouse[] = {
        {"Mouse Left",   SourceKind::MouseButton, SDL_BUTTON_LEFT},
        {"Mouse Right",  SourceKind::MouseButton, SDL_BUTTON_RIGHT},
        {"Mouse Middle", SourceKind::MouseButton, SDL_BUTTON_MIDDLE},
        {"Mouse X1",     SourceKind::MouseButton, SDL_BUTTON_X1},
        {"Mouse X2",     SourceKind::MouseButton, SDL_BUTTON_X2},
        {"Wheel Up",     SourceKind::WheelUp,     0},
        {"Wheel Down",   SourceKind::WheelDown,   0},
    };
    for (const auto& m : mouse) {
        if (SDL_strcasecmp(name.c_str(), m.name) == 0) {
            out = {m.kind, m.code};
            return true;
        }
    }
    // "Pad " and then SDL's own name for the button or axis: A, B, X, Y,
    // Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
    // DPUp, DPDown, DPLeft, DPRight, LeftTrigger, RightTrigger. SDL parses
    // them, so the names a player writes are the ones SDL documents and no
    // table here can fall out of date with it.
    if (SDL_strncasecmp(name.c_str(), "Pad ", 4) == 0) {
        const char* what = name.c_str() + 4;
        while (*what == ' ') what++;
        const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(what);
        if (b != SDL_CONTROLLER_BUTTON_INVALID) {
            out = {SourceKind::PadButton, int(b)};
            return true;
        }
        const SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString(what);
        if (a != SDL_CONTROLLER_AXIS_INVALID) {
            out = {SourceKind::PadAxis, int(a)};
            return true;
        }
        return false;
    }
    const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) {
        return false;
    }
    out = {SourceKind::Key, int(sc)};
    return true;
}

std::mutex g_bindings_mutex;
std::shared_ptr<const Resolved> g_resolved;   // read by the game's thread
Bindings g_bindings_in_force;                 // the names, for the file
std::atomic<uint32_t> g_bindings_gen{0};      // bumped by every table put in force
static std::vector<Source> g_extra_sources;   // the fast_forward and slow_motion entries in force, for the page's refusals

std::shared_ptr<const Resolved> resolved() {
    std::lock_guard<std::mutex> lock(g_bindings_mutex);
    return g_resolved;
}

// ---------------------------------------------------------------------------
// The mouse: written on the main thread (events), read on the game's thread
// ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

std::atomic<bool> g_focused{true};
std::atomic<bool> g_captured{false};
std::atomic<bool> g_fast_forward_held{false};   // input_fast_forward_held
std::atomic<bool> g_slow_motion_held{false};    // input_slow_motion_held
// The click that focuses the window arrives with the focus; until this
// moment, buttons are not presses.
std::atomic<int64_t> g_buttons_from{0};
constexpr int64_t FocusSettleUs = 250000;
std::atomic<uint32_t> g_mouse_held{0};                 // bit (1 << button index)
std::atomic<int64_t> g_mouse_press_until[8] = {};       // latched presses, microseconds
std::atomic<int64_t> g_wheel_up_until{0};
std::atomic<int64_t> g_wheel_down_until{0};
std::atomic<int64_t> g_esc_start_until{0};
// A press of the menu key not yet taken by the game thread.
std::atomic<int> g_menu_request{0};

// The pointer, for the game's menus. A menu that handles the mouse itself
// (patches/src/graphics_menu_patch.c, snap_mouse_*) raises the claim byte
// of the mailbox every frame it runs; while it is up, the mouse's buttons
// and wheel leave the binding table -- a click is no longer A, which chose
// whatever the menu had highlighted wherever the pointer was -- and reach
// the menu instead as counts: the menu turns a click into the item under
// the pointer. Where nothing claims it, the mouse is what it always was
// (a click advances Oak's text as A does). The pointer's window position
// is turned into the game's 320 by 240 with the viewport the renderer last
// drew with (snapdiag::PresentedView). Under SNAP_REPLAY the real mouse is
// left out, so a hand on the desk cannot steer a replayed run; the
// scripted one (SNAP_MOUSE_TEST) stands in for it.
std::atomic<int32_t> g_ptr_wx{-1};            // window points; -1: not over the window
std::atomic<int32_t> g_ptr_wy{-1};
std::atomic<int32_t> g_ptr_win_w{0};          // the window's size in points at that moment
std::atomic<int32_t> g_ptr_win_h{0};
std::atomic<uint32_t> g_ptr_moved{0};         // counts, a byte of each reaches the game
std::atomic<uint32_t> g_ptr_click{0};
std::atomic<uint32_t> g_ptr_back{0};
std::atomic<uint32_t> g_ptr_wheel_up{0};
std::atomic<uint32_t> g_ptr_wheel_down{0};
std::atomic<int64_t> g_ptr_moved_at{0};       // for the pointer shown in fullscreen
std::atomic<int64_t> g_ptr_quiet_until{0};    // the capture just changed: its warp is no move
std::atomic<bool> g_menu_claimed{false};
std::atomic<int32_t> g_test_gx{-1};           // SNAP_MOUSE_TEST: the pointer, game coordinates
// N64 buttons a menu's mouse asked to press for one reading (input_inject_press).
std::atomic<uint32_t> g_inject_buttons{0};
std::atomic<int32_t> g_test_gy{-1};
std::atomic<bool> g_test_held{false};         // SNAP_MOUSE_TEST's press and release, for a drag

// A press is reported for at least this long. The game samples its pad
// once per frame (33 ms), so a click shorter than a frame could fall
// between two reads; held this long it is seen by one frame or two, and
// the game's own edge detection makes it one press either way.
constexpr int64_t PressHoldUs = 45000;
// Radians of view per pixel of mouse at sensitivity 1: a full turn in
// about 2500 pixels. Zoomed in, the view is narrower, so the same motion
// turns less: mouse_zoom_speed, half by default.
constexpr float RadiansPerPixel = 0.0025f;

// The game's view, in its memory (patches/game_syms.ld). The .app_level
// overlay, which owns them, is resident whenever the mouse is captured.
constexpr uint32_t ADDR_PlayerViewYaw           = 0x80382CC8;  // f32, radians
constexpr uint32_t ADDR_ViewPitch               = 0x80382C0C;  // f32, radians, up positive
constexpr uint32_t ADDR_MinPitch                = 0x80382CEC;  // f32
constexpr uint32_t ADDR_MaxPitch                = 0x80382CF0;  // f32
// The player's object and its stick process (app_level/player.c,
// handleAnalogStick), read to know when the game itself takes the stick:
// the game pauses that process wherever the stick must not move the view.
constexpr uint32_t ADDR_gObjPlayer              = 0x80382C00;  // GObj*
constexpr uint32_t ADDR_handleAnalogStick       = 0x80350AE8;  // the process's function
constexpr uint32_t GObj_processListHead         = 0x18;        // sys/om.h: GObj
constexpr uint32_t GObjProcess_next             = 0x00;        // GObjProcess
constexpr uint32_t GObjProcess_paused           = 0x15;
constexpr uint32_t GObjProcess_function         = 0x20;
constexpr uint32_t ADDR_gDirectionIndex         = 0x80382BFC;  // s32: 0..3 facing, -1 zoomed in, -2 changing
constexpr uint32_t ADDR_TargetDirectionZoomedIn = 0x80382C4C;  // s32: nonzero while a C button turns the view
constexpr uint32_t ADDR_ZoomedInCameraHeld      = 0x80382D08;  // s32 (D_80382D08_523118): nonzero holds the zoomed camera
constexpr uint32_t ADDR_IsInputDisabled         = 0x80382D0C;  // s32
constexpr uint32_t ADDR_IsPaused                = 0x80382D20;  // u8
// IdleScript*: set while the title's attract demo plays a course from a
// recorded script (app_level/player.c updateIdle; the cartridge ends the
// demo on A or Start alone, never on the stick).
constexpr uint32_t ADDR_gIdleScript             = 0x803AE518;

// The recompiled memory keeps each 32-bit word in host order, so a word is
// read in place; bytes sit at their address XOR 3 (recomp.h's MEM_W and
// MEM_B).
uint32_t* word_at(uint8_t* rdram, uint32_t addr) {
    return reinterpret_cast<uint32_t*>(rdram + (addr - 0x80000000u));
}
float read_f32(uint8_t* rdram, uint32_t addr) {
    float f; std::memcpy(&f, word_at(rdram, addr), sizeof f); return f;
}
void write_f32(uint8_t* rdram, uint32_t addr, float f) {
    std::memcpy(word_at(rdram, addr), &f, sizeof f);
}
int32_t read_s32(uint8_t* rdram, uint32_t addr);
uint32_t read_u32(uint8_t* rdram, uint32_t addr) {
    return (uint32_t) read_s32(rdram, addr);
}

int32_t read_s32(uint8_t* rdram, uint32_t addr) {
    return static_cast<int32_t>(*word_at(rdram, addr));
}
// The Beach tutorial's watch for the Control Stick (patches/src/
// tutorial_patch.c) reads this byte of the port's mailbox: one on a frame
// the view was turned by mouse or gyro, zero otherwise.
constexpr uint32_t ADDR_MailboxViewTurned       = 0x80C0003B;  // u8, written here every frame
// The mouse block (the patch's map at SNAP_GFX_MAILBOX): the pointer as
// x << 16 | y, each a signed halfword, all ones when it is off the picture;
// then a byte apiece of the move, click, right-click and wheel counts; the
// claim, which a menu raises and each reading here lowers.
constexpr uint32_t ADDR_MailboxPointer          = 0x80C00088;  // u32
constexpr uint32_t ADDR_MailboxPointerCounts    = 0x80C0008C;  // u32: moved, click, back, wheel up
constexpr uint32_t ADDR_MailboxWheelDown        = 0x80C00090;  // u8
constexpr uint32_t ADDR_MailboxMouseClaim       = 0x80C00091;  // u8
constexpr uint32_t ADDR_MailboxMouseHeld        = 0x80C00092;  // u8: 1 while the left button is down, for a drag
// Typing on the photographer card (patches/src/mouse_patch.inc): the card
// raises the claim every frame it is up and each reading here lowers it;
// while it is up, the keys that type are the card's, and each typed
// character is handed over one at a time in the slot (Latin-1: printable
// ASCII and 0xE9 for e acute; 0x08 a letter back, 0x0D done), which the
// card empties when it takes it.
constexpr uint32_t ADDR_MailboxTypeClaim        = 0x80C0009D;  // u8
constexpr uint32_t ADDR_MailboxTypeChar         = 0x80C0009E;  // u8
void write_u8(uint8_t* rdram, uint32_t addr, uint8_t v) {
    rdram[(addr - 0x80000000u) ^ 3u] = v;
}
uint8_t read_u8(uint8_t* rdram, uint32_t addr) {
    return rdram[(addr - 0x80000000u) ^ 3u];
}

std::mutex g_motion_mutex;
float g_motion_dx = 0.0f;   // pixels since the last apply
float g_motion_dy = 0.0f;
// The gyro's turning since the last apply, in radians and in the game's
// senses (yaw positive to the right, pitch positive upward). SDL reports
// rates by the right-hand rule about its axes, so its positive yaw is a
// turn to the left and its positive pitch is the pad's front rising.
float g_gyro_yaw = 0.0f;
float g_gyro_pitch = 0.0f;
std::atomic<bool> g_gyro_enabled{false};       // the sensor is on and its events are wanted
std::atomic<int32_t> g_gyro_instance{-1};      // the joystick the readings must come from
uint64_t g_gyro_last_us = 0;                   // the sensor's own clock, when it has one
float g_gyro_rate_hz = 0.0f;                   // else its nominal rate spaces the readings
// SNAP_GYRO_DEBUG: what the sensor sent and what became of it. Guarded by
// g_motion_mutex, which the reading path already takes.
const bool g_gyro_debug = (getenv("SNAP_GYRO_DEBUG") != nullptr);
float g_dbg_peak[3] = {0.0f, 0.0f, 0.0f};   // largest |rate| per axis, rad/s
uint32_t g_dbg_n = 0;                        // readings counted this second
float g_dbg_last_yaw = 0.0f;                 // angle handed over, radians
float g_dbg_last_pitch = 0.0f;
float g_dbg_dt_sum = 0.0f;                   // seconds the readings claimed to span
uint32_t g_dbg_clock = 0;                    // readings whose own clock was believed
float g_dbg_grav[3] = {0.0f, 0.0f, 0.0f};    // where up was, last reading

// Which way is UP, in the pad's own frame, low-passed out of the
// accelerometer (about a quarter second). Touched only on the event thread.
// SDL's accelerometer measures proper acceleration, so a pad at rest reads
// +9.8 along the axis pointing away from the earth (SDL_sensor.h): the
// vector points up, and calling it "down" is what put a sign error in the
// first version of this.
// A turn of the pad is a rotation about the world's vertical, not about the
// pad's own up axis; the two are the same only when the pad is held upright,
// and a Steam Deck is held tilted back, which put the whole of a left-right
// turn onto an axis the port was not reading (found on a Deck, 2026-09-06).
float g_grav[3] = {0.0f, 0.0f, 0.0f};
const char* g_dbg_stop = "not called";       // the test that dropped it

std::atomic<int64_t> g_gyro_live_us{0};        // when a reading last carried any turning at all
std::atomic<uint32_t> g_gyro_readings{0};      // readings since the gyro was turned on

#if defined(__ANDROID__)
// The phone's own gyroscope and accelerometer (SDL's sensors, over Android's
// sensor manager), for Gyro Aim when no pad's gyro is in use. SDL reads
// Android's sensors on the thread that started them, which must be the one
// that pumps events: they are opened, closed and read on the window's
// thread only. Android's readings reach SDL without their own clock, so each
// gyroscope reading counts for the time since the one before it.
SDL_Sensor* g_phone_gyro = nullptr;
SDL_Sensor* g_phone_accel = nullptr;
int64_t g_phone_gyro_last_us = 0;
float g_phone_up[3] = {0.0f, 0.0f, 0.0f};   // which way is up, in the screen's axes
uint32_t g_phone_readings = 0;
bool g_phone_live_logged = false;
// SNAP_GYRO_DEBUG: the angle the readings made, in all since the sensor was
// opened (radians, the game's senses, before Gyro Speed).
float g_phone_sum_yaw = 0.0f;
float g_phone_sum_pitch = 0.0f;
#endif

void clear_mouse() {
    g_mouse_held.store(0, std::memory_order_relaxed);
    for (auto& p : g_mouse_press_until) p.store(0, std::memory_order_relaxed);
    g_wheel_up_until.store(0, std::memory_order_relaxed);
    g_wheel_down_until.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_motion_mutex);
    g_motion_dx = 0.0f;
    g_motion_dy = 0.0f;
    g_gyro_yaw = 0.0f;
    g_gyro_pitch = 0.0f;
}

// Adds the mouse's motion since the last call to the game's view. Runs on
// the game's thread, from input_get, at the moment the game reads its pad:
// the game's own camera code runs after that in the same frame, on the
// same thread, so there is no race with its writes to the same words. The
// game's states are honored: nothing moves while paused, while input is
// disabled (cutscenes, the end of a course), while the view is changing
// direction (-2) or a C button is turning it, or while the zoomed camera
// is held for a photo. Zoomed out, the game folds a yaw past 0.3 pi into
// the next facing direction itself; zoomed in it wraps at a full turn; the
// pitch is clamped to the game's own limits here, as the stick path does.
void apply_mouse_look(uint8_t* rdram) {
    float dx, dy, gyaw, gpitch;
    {
        std::lock_guard<std::mutex> lock(g_motion_mutex);
        dx = g_motion_dx; dy = g_motion_dy;
        gyaw = g_gyro_yaw; gpitch = g_gyro_pitch;
        g_motion_dx = 0.0f; g_motion_dy = 0.0f;
        g_gyro_yaw = 0.0f; g_gyro_pitch = 0.0f;
    }
    if (g_gyro_debug) {
        std::lock_guard<std::mutex> lock(g_motion_mutex);
        g_dbg_last_yaw = gyaw;
        g_dbg_last_pitch = gpitch;
    }
#define SNAP_GYRO_DROP(why) do { if (g_gyro_debug) g_dbg_stop = (why); return; } while (0)
    if (rdram != nullptr) {
        // Cleared first: a frame that turns nothing, or is dropped below,
        // must not leave the previous frame's one behind.
        write_u8(rdram, ADDR_MailboxViewTurned, 0);
    }
    if (dx == 0.0f && dy == 0.0f && gyaw == 0.0f && gpitch == 0.0f) SNAP_GYRO_DROP("no angle arrived");
    if (rdram == nullptr) SNAP_GYRO_DROP("no rdram");
    if (!g_app_level_resident.load(std::memory_order_relaxed)) SNAP_GYRO_DROP("not in a course");
    // The title's attract demo is a course played from a script: on the
    // cartridge the stick does nothing to it and A or Start ends it, so the
    // mouse and the gyro leave its camera alone (the view followed the mouse
    // through the demo: JackandBeans, Oct 3 2026). A click is A, and ends it.
    if (read_s32(rdram, ADDR_gIdleScript) != 0) SNAP_GYRO_DROP("the attract demo");
    if (read_u8(rdram, ADDR_IsPaused) != 0) SNAP_GYRO_DROP("paused");
    if (read_s32(rdram, ADDR_IsInputDisabled) != 0) SNAP_GYRO_DROP("input disabled");
    // IsInputDisabled covers the Beach tutorial only. Everywhere else the
    // game stops the stick by pausing the player's stick process -- the
    // course's scripted start, a cutscene, a message, a turn to a new
    // direction, the pause menu -- and the mouse went on turning the view
    // through all of it: a course began looking straight up after a hand on
    // the mouse during the Zero-One's run-in (JackandBeans, Oct 5 2026). The
    // mouse moves the view only while that process runs.
    {
        const uint32_t player = read_u32(rdram, ADDR_gObjPlayer);
        if (player == 0) SNAP_GYRO_DROP("no player");
        uint32_t proc = read_u32(rdram, player + GObj_processListHead);
        bool stickRuns = false;
        for (int n = 0; (proc != 0) && (n < 64); n++) {
            if (read_u32(rdram, proc + GObjProcess_function) == ADDR_handleAnalogStick) {
                stickRuns = (read_u8(rdram, proc + GObjProcess_paused) == 0);
                break;
            }
            proc = read_u32(rdram, proc + GObjProcess_next);
        }
        if (!stickRuns) SNAP_GYRO_DROP("the stick process is paused");
    }
    const int32_t direction = read_s32(rdram, ADDR_gDirectionIndex);
    if (direction < -1) SNAP_GYRO_DROP("direction changing");
    const bool zoomedIn = (direction == -1);
    if (zoomedIn && (read_s32(rdram, ADDR_TargetDirectionZoomedIn) != 0)) SNAP_GYRO_DROP("zoom turning");
    if (zoomedIn && (read_s32(rdram, ADDR_ZoomedInCameraHeld) != 0)) SNAP_GYRO_DROP("photo held");

    const Settings& s = settings();
    const float sens = std::fmax(0.1f, std::fmin(10.0f, s.mouse_sensitivity));
    const float zoom = std::fmax(0.25f, std::fmin(1.0f, s.mouse_zoom_speed));
    const float k = RadiansPerPixel * sens * (zoomedIn ? zoom : 1.0f);
    // The gyro at natural scale, a turn of the pad turning the view as far,
    // whatever the zoom: that is what makes a pad feel like a camera. Mode 2
    // listens only zoomed in, the way a photographer raises the camera.
    const bool gyroOn = (s.gyro_aim == 1) || ((s.gyro_aim == 2) && zoomedIn);
    const float gs = gyroOn ? std::fmax(0.25f, std::fmin(4.0f, s.gyro_sensitivity)) : 0.0f;

    float yaw = read_f32(rdram, ADDR_PlayerViewYaw) + dx * k + gyaw * gs;
    write_f32(rdram, ADDR_PlayerViewYaw, yaw);

    // SDL's y grows downward; the game's pitch grows upward. The gyro's
    // pitch already grows upward; the same tilt setting flips both.
    const float dpitch = (s.mouse_invert_y ? dy : -dy) * k + (s.mouse_invert_y ? -gpitch : gpitch) * gs;
    float pitch = read_f32(rdram, ADDR_ViewPitch) + dpitch;
    const float lo = read_f32(rdram, ADDR_MinPitch);
    const float hi = read_f32(rdram, ADDR_MaxPitch);
    if (pitch < lo) pitch = lo;
    if (pitch > hi) pitch = hi;
    write_f32(rdram, ADDR_ViewPitch, pitch);

    // The Beach tutorial asks for the Control Stick after ten seconds
    // without one, and a player looking around by mouse or gyro never
    // touches it. Told here, for the tutorial's own check to take as the
    // stick (patches/src/tutorial_patch.c). Two thousandths of a radian
    // in a frame is a slow, deliberate look; the gyro's rest is far below.
    const float turned = std::fabs(dx * k + gyaw * gs) + std::fabs(dpitch);
    if (turned > 0.002f) {
        write_u8(rdram, ADDR_MailboxViewTurned, 1);
    }
    if (g_gyro_debug) g_dbg_stop = "applied to the view";
}
#undef SNAP_GYRO_DROP

#if defined(__ANDROID__)
// A reading in the phone's own axes (x to the right, y up, z out of the
// screen, as the panel stands in its natural portrait) put into the
// screen's axes as the picture stands now (Android's own rule, the one its
// AccelerometerPlay sample uses). SDL names Android's ROTATION_90, the
// phone turned a quarter turn to the left, LANDSCAPE: its x then points up
// and its y to the left. Works for rates and for accelerations alike.
void phone_to_screen(const float* d, float* out) {
    switch (SDL_GetDisplayOrientation(0)) {
        case SDL_ORIENTATION_LANDSCAPE:
            out[0] = -d[1];
            out[1] = d[0];
            break;
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
            out[0] = d[1];
            out[1] = -d[0];
            break;
        case SDL_ORIENTATION_PORTRAIT_FLIPPED:
            out[0] = -d[0];
            out[1] = -d[1];
            break;
        default:
            out[0] = d[0];
            out[1] = d[1];
            break;
    }
    out[2] = d[2];
}

const char* orientation_name() {
    switch (SDL_GetDisplayOrientation(0)) {
        case SDL_ORIENTATION_LANDSCAPE:         return "landscape (the phone turned left)";
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return "landscape (the phone turned right)";
        case SDL_ORIENTATION_PORTRAIT:          return "portrait";
        case SDL_ORIENTATION_PORTRAIT_FLIPPED:  return "portrait, upside down";
        default:                                return "unknown";
    }
}

// One reading of the phone's gyroscope or accelerometer: the same sums the
// pad's path makes (SDL_CONTROLLERSENSORUPDATE below), in the screen's axes.
// Window thread.
void phone_sensor_reading(const SDL_SensorEvent& e) {
    float s[3];
    phone_to_screen(e.data, s);
    if ((g_phone_accel != nullptr) && (e.which == SDL_SensorGetInstanceID(g_phone_accel))) {
        // Which way is up, while the phone is not being accelerated (the
        // pad's rule); followed over about a quarter second at 60 Hz.
        const float am = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        if ((am < 6.5f) || (am > 13.0f)) {
            return;
        }
        const float have = std::sqrt(g_phone_up[0] * g_phone_up[0] + g_phone_up[1] * g_phone_up[1] +
                                     g_phone_up[2] * g_phone_up[2]);
        for (int i = 0; i < 3; i++) {
            g_phone_up[i] = (have < 0.1f) ? s[i] : (g_phone_up[i] + 0.06f * (s[i] - g_phone_up[i]));
        }
        return;
    }
    if ((g_phone_gyro == nullptr) || (e.which != SDL_SensorGetInstanceID(g_phone_gyro))) {
        return;
    }
    const int64_t t = now_us();
    float dt = (g_phone_gyro_last_us != 0) ? float(t - g_phone_gyro_last_us) * 1e-6f : 0.0f;
    g_phone_gyro_last_us = t;
    if (dt > 0.1f) dt = 0.1f;   // a gap (the app away, a stall) is not a turn
    g_phone_readings++;
    if (!g_phone_live_logged && ((s[0] != 0.0f) || (s[1] != 0.0f) || (s[2] != 0.0f))) {
        printf("[SNAP-Input] gyro: the phone's readings carry turning (%u readings since it was turned on)\n",
               g_phone_readings);
        fflush(stdout);
        g_phone_live_logged = true;
    }
    // Pitch about the screen's right axis; yaw about the world's vertical,
    // the reading projected on the up the accelerometer gives (the screen's
    // own up when there is none yet), so a phone held tilted back turns the
    // view as far as one held upright.
    float pitchRate = s[0];
    float yawRate = s[1];
    const float um = std::sqrt(g_phone_up[0] * g_phone_up[0] + g_phone_up[1] * g_phone_up[1] +
                               g_phone_up[2] * g_phone_up[2]);
    if (um > 1.0f) {
        yawRate = (s[0] * g_phone_up[0] + s[1] * g_phone_up[1] + s[2] * g_phone_up[2]) / um;
    }
    // The pad's tightening: under a degree per second the rate is scaled
    // toward zero, so a phone at rest does not creep.
    constexpr float Tight = 0.01745f;
    const float mag = std::sqrt(pitchRate * pitchRate + yawRate * yawRate);
    if (mag < Tight) {
        const float f = mag / Tight;
        pitchRate *= f;
        yawRate *= f;
    }
    std::lock_guard<std::mutex> lock(g_motion_mutex);
    g_gyro_yaw += -yawRate * dt;
    g_gyro_pitch += pitchRate * dt;
    g_phone_sum_yaw += -yawRate * dt;
    g_phone_sum_pitch += pitchRate * dt;
}

void phone_gyro_close() {
    if (g_phone_accel != nullptr) {
        SDL_SensorClose(g_phone_accel);
        g_phone_accel = nullptr;
    }
    if (g_phone_gyro != nullptr) {
        SDL_SensorClose(g_phone_gyro);
        g_phone_gyro = nullptr;
    }
    g_phone_gyro_on.store(false, std::memory_order_relaxed);
}

// Opens the phone's gyroscope (and its accelerometer, for which way is up)
// while Gyro Aim is on and no pad's own gyro is, and closes them otherwise,
// so a phone with Gyro Aim off reads no sensor at all. Window thread, once
// per pass of the window's loop, before it pumps the events.
void phone_gyro_sync() {
    static bool none = false;   // no gyroscope, or SDL could not open it: said once
    const bool want = (settings().gyro_aim != 0) && !g_gyro_enabled.load(std::memory_order_relaxed);
    if (!want) {
        none = false;
        if (g_phone_gyro != nullptr) {
            phone_gyro_close();
            printf("[SNAP-Input] gyro: the phone's own sensor is off\n");
            fflush(stdout);
        }
        return;
    }
    if ((g_phone_gyro == nullptr) && !none) {
        none = true;
        if (!SDL_WasInit(SDL_INIT_SENSOR) && (SDL_InitSubSystem(SDL_INIT_SENSOR) != 0)) {
            printf("[SNAP-Input] gyro: the phone's sensors could not be started: %s\n", SDL_GetError());
            fflush(stdout);
            return;
        }
        // The first of each kind Android lists: the hardware's own, ahead of
        // the system's corrected and fused ones.
        int gyro = -1, accel = -1;
        for (int i = 0; i < SDL_NumSensors(); i++) {
            const SDL_SensorType type = SDL_SensorGetDeviceType(i);
            if ((type == SDL_SENSOR_GYRO) && (gyro < 0)) gyro = i;
            if ((type == SDL_SENSOR_ACCEL) && (accel < 0)) accel = i;
        }
        if (gyro < 0) {
            printf("[SNAP-Input] gyro: the phone has no gyroscope; Gyro Aim needs a pad with one\n");
            fflush(stdout);
            return;
        }
        g_phone_gyro = SDL_SensorOpen(gyro);
        if (g_phone_gyro == nullptr) {
            printf("[SNAP-Input] gyro: the phone's gyroscope could not be opened: %s\n", SDL_GetError());
            fflush(stdout);
            return;
        }
        g_phone_accel = (accel >= 0) ? SDL_SensorOpen(accel) : nullptr;
        none = false;
        g_phone_gyro_last_us = 0;
        g_phone_up[0] = g_phone_up[1] = g_phone_up[2] = 0.0f;
        g_phone_readings = 0;
        g_phone_live_logged = false;
        {
            std::lock_guard<std::mutex> lock(g_motion_mutex);
            g_phone_sum_yaw = g_phone_sum_pitch = 0.0f;
        }
        g_phone_gyro_on.store(true, std::memory_order_relaxed);
        printf("[SNAP-Input] gyro: the phone's own sensor is on: its turning aims the camera (%s; up from %s; "
               "the screen %s)\n", SDL_SensorGetName(g_phone_gyro),
               (g_phone_accel != nullptr) ? SDL_SensorGetName(g_phone_accel) : "the screen alone",
               orientation_name());
        fflush(stdout);
    }
    // SNAP_GYRO_DEBUG read here, not when the library loads (g_gyro_debug):
    // on Android the launching intent's extras become the environment only
    // after that.
    static const bool debug = (getenv("SNAP_GYRO_DEBUG") != nullptr);
    if (debug && (g_phone_gyro != nullptr)) {
        static int64_t said = 0;
        const int64_t t = now_us();
        if (t - said >= 500000) {
            said = t;
            float sy, sp;
            {
                std::lock_guard<std::mutex> lock(g_motion_mutex);
                sy = g_phone_sum_yaw;
                sp = g_phone_sum_pitch;
            }
            const bool view = (g_rdram != nullptr) && g_app_level_resident.load(std::memory_order_relaxed);
            // The game's states apply_mouse_look honors, as they stand.
            int stick = -1;
            if (view) {
                const uint32_t player = read_u32(g_rdram, ADDR_gObjPlayer);
                uint32_t proc = (player != 0) ? read_u32(g_rdram, player + GObj_processListHead) : 0;
                for (int n = 0; (proc != 0) && (n < 64); n++) {
                    if (read_u32(g_rdram, proc + GObjProcess_function) == ADDR_handleAnalogStick) {
                        stick = (read_u8(g_rdram, proc + GObjProcess_paused) == 0) ? 1 : 0;
                        break;
                    }
                    proc = read_u32(g_rdram, proc + GObjProcess_next);
                }
            }
            printf("[SNAP-GYRO] phone: %u readings; made yaw=%.4f pitch=%.4f rad in all; up=(%.1f %.1f %.1f); "
                   "the view yaw=%.4f pitch=%.4f; the screen %s; game: paused %d, input off %d, stick runs %d, "
                   "direction %d, zoom turning %d, photo held %d\n",
                   g_phone_readings, sy, sp, g_phone_up[0], g_phone_up[1], g_phone_up[2],
                   view ? read_f32(g_rdram, ADDR_PlayerViewYaw) : 0.0f,
                   view ? read_f32(g_rdram, ADDR_ViewPitch) : 0.0f, orientation_name(),
                   view ? int(read_u8(g_rdram, ADDR_IsPaused)) : -1,
                   view ? int(read_s32(g_rdram, ADDR_IsInputDisabled)) : -1, stick,
                   view ? int(read_s32(g_rdram, ADDR_gDirectionIndex)) : -9,
                   view ? int(read_s32(g_rdram, ADDR_TargetDirectionZoomedIn)) : -1,
                   view ? int(read_s32(g_rdram, ADDR_ZoomedInCameraHeld)) : -1);
            fflush(stdout);
        }
    }
}
#endif

// A source that reads one of the Switch Online N64 controller's C buttons
// as SDL's driver sends them: Y, X, Back and the right trigger. It is the
// part read that counts, not the name: under the N64-shaped layout the
// right shoulder's name reads the pad's ZR and the right trigger's name its
// own R (pad_button_held, pad_axis_held), so only under Standard does a
// name reach the right trigger. 1.1.5 counted the right trigger's name
// under both, and R did nothing (issue #26).
static bool nso_c_source(const Source& src, const PadSnapshot& pad) {
    if (src.kind == SourceKind::PadButton) {
        return (src.code == SDL_CONTROLLER_BUTTON_Y) || (src.code == SDL_CONTROLLER_BUTTON_X) ||
               (src.code == SDL_CONTROLLER_BUTTON_BACK);
    }
    return (src.kind == SourceKind::PadAxis) && !pad.n64_layout && (src.code == SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
}

bool source_down(const Source& src, const uint8_t* keys, uint32_t held, int64_t t, const PadSnapshot& pad,
                 bool mouse_live = true) {
    if (!mouse_live && ((src.kind == SourceKind::MouseButton) || (src.kind == SourceKind::WheelUp) ||
                        (src.kind == SourceKind::WheelDown))) {
        return false;
    }
    switch (src.kind) {
        case SourceKind::Key:
            if (g_deck_pad_keys_ignored.load(std::memory_order_relaxed) && deck_layout_key(src.code)) {
                return false;
            }
            return keys != nullptr && src.code >= 0 && src.code < SDL_NUM_SCANCODES && keys[src.code];
        case SourceKind::MouseButton:
            return (src.code >= 0) && (src.code < 8) &&
                   (((held >> src.code) & 1u) || (t < g_mouse_press_until[src.code].load(std::memory_order_relaxed)));
        case SourceKind::WheelUp:
            return t < g_wheel_up_until.load(std::memory_order_relaxed);
        case SourceKind::WheelDown:
            return t < g_wheel_down_until.load(std::memory_order_relaxed);
        case SourceKind::PadButton:
            if (pad.nso_n64 && nso_c_source(src, pad)) {
                return false;       // the C buttons themselves (input_get)
            }
            return pad_button_held(pad, src.code);
        case SourceKind::PadAxis:
            if (pad.nso_n64 && nso_c_source(src, pad)) {
                return false;
            }
            return pad_axis_held(pad, src.code);
    }
    return false;
}

} // namespace

static bool is_steam_deck_pad(SDL_GameController* gc);

bool input_deck_keys_ignored() {
    return g_deck_pad_keys_ignored.load(std::memory_order_relaxed);
}

bool input_trace_on() {
    return trace_on();
}

const Bindings& input_default_bindings() {
    return defaults();
}

// True when a binding names a controller button or axis.
static bool is_pad_name(const std::string& name) {
    return SDL_strncasecmp(name.c_str(), "Pad ", 4) == 0;
}

// A file written before the pad joined this table names no pad source at all,
// and a file's list REPLACES the default rather than adding to it -- so
// reading one as it stands would leave the controller dead, buttons and
// triggers both, for every player who had ever run the port before. Such a
// table is migrated: the pad half of each input's default is put back, the
// player's own keys and mouse buttons untouched. A table that names any pad
// source was written by someone who knew about them and is left exactly as it
// is, so unbinding a pad button stays possible.
static Bindings migrate_pad_bindings(const Bindings& in) {
    for (const auto& entry : in) {
        for (const std::string& src : entry.second) {
            if (is_pad_name(src)) {
                return in;
            }
        }
    }
    Bindings out = in;
    int added = 0;
    for (const auto& def : defaults()) {
        auto it = out.find(def.first);
        if (it == out.end()) {
            continue;   // the file never mentioned this input; the default applies
        }
        for (const std::string& src : def.second) {
            if (is_pad_name(src)) {
                it->second.push_back(src);
                added++;
            }
        }
    }
    if (added > 0) {
        printf("[SNAP-Input] the settings file predates controller bindings; "
               "%d pad defaults added back, keys and mouse left as they were\n", added);
        fflush(stdout);
    }
    return out;
}

// Puts a table in force. The migration is for a file written before the pad
// joined the table (above); the BUTTON SETUP page's tables are complete and skip
// it, so clearing the last pad name from the page does not bring every pad
// default back.
static void set_bindings(const Bindings& given, bool migrate) {
    const Bindings bindings = migrate ? migrate_pad_bindings(given) : given;
    auto r = std::make_shared<Resolved>();
    Bindings in_force = defaults();
    for (int i = 0; i < IN_COUNT; i++) {
        const std::string name = kInputNames[i];
        auto it = bindings.find(name);
        const std::vector<std::string>& wanted = (it != bindings.end()) ? it->second : defaults().at(name);
        std::vector<Source> sources;
        std::vector<std::string> accepted;
        for (const std::string& src_name : wanted) {
            Source src{};
            if (resolve_source(src_name, src)) {
                sources.push_back(src);
                accepted.push_back(src_name);
            } else {
                printf("[SNAP-Input] keys.%s: \"%s\" is not a name SDL knows; skipped. "
                       "Keys are SDL key names (\"X\", \"Left Shift\", \"Return\"); the mouse is "
                       "\"Mouse Left/Right/Middle/X1/X2\", \"Wheel Up/Down\"; a controller "
                       "is \"Pad \" and one of A B X Y Back Guide Start LeftStick RightStick "
                       "LeftShoulder RightShoulder DPUp DPDown DPLeft DPRight LeftTrigger RightTrigger\n",
                       name.c_str(), src_name.c_str());
            }
        }
        if (sources.empty()) {
            // Nothing usable was given: the default stays, so no input can
            // be left with no way to press it.
            for (const std::string& src_name : defaults().at(name)) {
                Source src{};
                if (resolve_source(src_name, src)) sources.push_back(src);
            }
            accepted = defaults().at(name);
            if (it != bindings.end()) {
                printf("[SNAP-Input] keys.%s: no usable source; the default stays\n", name.c_str());
            }
        }
        r->sources[i] = std::move(sources);
        in_force[name] = std::move(accepted);
    }
    // The port's own keys (input.h): fast_forward and slow_motion are not
    // N64 inputs, so the Button Setup page never lists them and a table
    // from the page carries the eighteen inputs only. Absent from the table
    // given, the sources in force stay; nothing in force yet (the first
    // load) or nothing usable means the shipped pair, as for the inputs.
    {
        std::vector<Source> extra;
        for (int e = 0; e < 2; e++) {
            const char* const ename = (e == 0) ? kFastForwardName : kSlowMotionName;
            std::vector<std::string> wanted;
            auto it = bindings.find(ename);
            if (it != bindings.end()) {
                wanted = it->second;
            } else {
                std::lock_guard<std::mutex> lock(g_bindings_mutex);
                auto cur = g_bindings_in_force.find(ename);
                wanted = (cur != g_bindings_in_force.end()) ? cur->second : defaults().at(ename);
            }
            std::vector<Source> sources;
            std::vector<std::string> accepted;
            for (const std::string& src_name : wanted) {
                Source src{};
                if (resolve_source(src_name, src)) {
                    sources.push_back(src);
                    accepted.push_back(src_name);
                } else {
                    printf("[SNAP-Input] keys.%s: \"%s\" is not a name SDL knows; skipped\n", ename, src_name.c_str());
                }
            }
            if (sources.empty()) {
                for (const std::string& src_name : defaults().at(ename)) {
                    Source src{};
                    if (resolve_source(src_name, src)) sources.push_back(src);
                }
                accepted = defaults().at(ename);
                if (it != bindings.end()) {
                    printf("[SNAP-Input] keys.%s: no usable source; the default stays\n", ename);
                }
            }
            extra.insert(extra.end(), sources.begin(), sources.end());
            ((e == 0) ? r->fast_forward : r->slow_motion) = std::move(sources);
            in_force[ename] = std::move(accepted);
        }
        {
            std::lock_guard<std::mutex> lock(g_bindings_mutex);
            g_extra_sources = std::move(extra);
        }
    }
    for (const auto& entry : bindings) {
        bool known = (entry.first == kFastForwardName) || (entry.first == kSlowMotionName);
        for (const char* k : kInputNames) known = known || (entry.first == k);
        if (!known) {
            printf("[SNAP-Input] keys.%s: not an input this port has; ignored (input.h lists them)\n", entry.first.c_str());
        }
    }
    fflush(stdout);
    std::lock_guard<std::mutex> lock(g_bindings_mutex);
    g_resolved = std::move(r);
    g_bindings_in_force = std::move(in_force);
    g_bindings_gen.fetch_add(1, std::memory_order_release);
}

void input_set_bindings(const Bindings& given) {
    set_bindings(given, true);
}

Bindings input_bindings() {
    std::lock_guard<std::mutex> lock(g_bindings_mutex);
    if (g_bindings_in_force.empty()) {
        return defaults();
    }
    return g_bindings_in_force;
}

uint32_t input_bindings_generation() {
    return g_bindings_gen.load(std::memory_order_acquire);
}

bool input_fast_forward_held() {
    return g_fast_forward_held.load(std::memory_order_relaxed);
}

bool input_slow_motion_held() {
    return g_slow_motion_held.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// The BUTTON SETUP page (patches/src/graphics_menu_patch.c, snap_bind_page): the
// names its rows show, the edits it makes, and the capture of the next
// press. The page runs on the game's thread and reaches this through the
// mailbox (src/menu_assets.cpp, poll_bind_bank); the events that carry a
// press arrive on the window's thread (input_handle_sdl_event).
// ---------------------------------------------------------------------------

namespace {

// The page's device a source name belongs to.
int source_device(const std::string& name) {
    if (is_pad_name(name)) {
        return kBindPad;
    }
    Source src{};
    if (resolve_source(name, src) &&
        ((src.kind == SourceKind::MouseButton) || (src.kind == SourceKind::WheelUp) ||
         (src.kind == SourceKind::WheelDown))) {
        return kBindMouse;
    }
    return kBindKeyboard;
}

const char* device_name(int device) {
    return (device == kBindPad) ? "controller" : (device == kBindMouse) ? "mouse" : "keyboard";
}

// The name the settings file and the README spell a pad part with, for a
// captured press: SDL's own spelling is lowercase ("leftshoulder"), which
// resolve_source reads just as well, but the file should carry the
// documented one.
std::string pad_source_name(SDL_GameControllerButton b) {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_A: return "Pad A";
        case SDL_CONTROLLER_BUTTON_B: return "Pad B";
        case SDL_CONTROLLER_BUTTON_X: return "Pad X";
        case SDL_CONTROLLER_BUTTON_Y: return "Pad Y";
        case SDL_CONTROLLER_BUTTON_BACK: return "Pad Back";
        case SDL_CONTROLLER_BUTTON_GUIDE: return "Pad Guide";
        case SDL_CONTROLLER_BUTTON_START: return "Pad Start";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "Pad LeftStick";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "Pad RightStick";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "Pad LeftShoulder";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "Pad RightShoulder";
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return "Pad DPUp";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "Pad DPDown";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "Pad DPLeft";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "Pad DPRight";
        default: {
            const char* s = SDL_GameControllerGetStringForButton(b);
            return (s != nullptr) ? (std::string("Pad ") + s) : std::string();
        }
    }
}

// The name a row shows for one source. The pad's parts are named as the
// layout in force reads them: "Pad LeftShoulder" is read from the left
// trigger's travel on an N64-shaped pad, which is that pad's Z, and from
// the shoulder button on the rest, a bumper (pad_button_held).
std::string display_name(const std::string& name, bool n64) {
    Source src{};
    if (!resolve_source(name, src)) {
        return name;
    }
    switch (src.kind) {
        case SourceKind::Key: {
            const char* n = SDL_GetScancodeName(SDL_Scancode(src.code));
            std::string s = ((n != nullptr) && (n[0] != '\0')) ? n : name;
            // SDL's GUI is the Windows or Command key.
            if (s == "Left GUI") s = "Left Win";
            if (s == "Right GUI") s = "Right Win";
            return s;
        }
        case SourceKind::MouseButton:
            switch (src.code) {
                case SDL_BUTTON_LEFT: return "Left";
                case SDL_BUTTON_RIGHT: return "Right";
                case SDL_BUTTON_MIDDLE: return "Middle";
                case SDL_BUTTON_X1: return "Back";
                default: return "Forward";
            }
        case SourceKind::WheelUp: return "Wheel Up";
        case SourceKind::WheelDown: return "Wheel Down";
        case SourceKind::PadButton:
            switch (src.code) {
                case SDL_CONTROLLER_BUTTON_A: return "A";
                case SDL_CONTROLLER_BUTTON_B: return "B";
                case SDL_CONTROLLER_BUTTON_X: return "X";
                case SDL_CONTROLLER_BUTTON_Y: return "Y";
                case SDL_CONTROLLER_BUTTON_BACK: return "Back";
                case SDL_CONTROLLER_BUTTON_GUIDE: return "Guide";
                case SDL_CONTROLLER_BUTTON_START: return "Start";
                case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "L Stick";
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "R Stick";
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return n64 ? "Z" : "L Bumper";
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return n64 ? "ZR" : "R Bumper";
                case SDL_CONTROLLER_BUTTON_DPAD_UP: return "D Up";
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "D Down";
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "D Left";
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D Right";
                default: {
                    const char* n = SDL_GameControllerGetStringForButton(SDL_GameControllerButton(src.code));
                    return (n != nullptr) ? n : name;
                }
            }
        case SourceKind::PadAxis:
            switch (src.code) {
                case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return n64 ? "L" : "L Trigger";
                case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return n64 ? "R" : "R Trigger";
                default: {
                    const char* n = SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis(src.code));
                    return (n != nullptr) ? n : name;
                }
            }
    }
    return name;
}

void bind_publish(const Bindings& table) {
    set_bindings(table, false);
    settings_mark_dirty();
}

// The capture. One at a time, owned by the page; the state is read and
// written under the mutex on both threads, the two flags are the fast
// checks the input path and the window's loop make on every event.
struct Capture {
    std::mutex mutex;
    int device = kBindKeyboard;
    int64_t startedUs = 0;
    CaptureState state = CaptureState::Idle;
    std::string name;
    bool triggerAbove[2] = {false, false};   // the triggers at arming, for a rising edge
    int64_t testDueUs = 0;                   // SNAP_BIND_TEST: when the scripted press lands
    std::string testName;
};
Capture g_capture;
std::atomic<bool> g_capture_active{false};
// After a capture nothing reaches the game until every source is let go:
// the key just bound may still be down, and the page would read it as a
// press of the button it now is.
std::atomic<bool> g_capture_settle{false};
constexpr int64_t CaptureTimeoutUs = 8000000;
constexpr int64_t CaptureTestDelayUs = 300000;

// SNAP_VPAD_TEST: a virtual game controller attached through SDL's own
// joystick layer and opened in place of any real pad, whose parts the
// captures on the controller press on a schedule: a comma-separated list
// of SDL's names ("y", "lefttrigger", "dpup"), one per capture, each
// pressed three tenths of a second in and released a little later. Every
// piece of the port's pad path -- SDL's mapping and event queue, the pad
// thread's poll and snapshot, the capture's decode, the settle after --
// runs as for a real pad; only the driver beneath SDL is not there. For
// the replay that proves the Button Setup page (2026-09-13).
const bool g_vpad_test = (getenv("SNAP_VPAD_TEST") != nullptr);
SDL_Joystick* g_vpad = nullptr;          // opened by the pad thread; pressed from the game thread
std::vector<std::string> g_vpad_names;   // parsed once, under g_capture.mutex
size_t g_vpad_next = 0;
int64_t g_vpad_due_us = 0;               // when the next scripted press lands (0: none)
int64_t g_vpad_release_us = 0;           // when the pressed part is let go (0: none)
int g_vpad_button = -1;
int g_vpad_axis = -1;

// The scripted press and its release, on the game thread: input_get calls
// this every reading, so the release lands whether or not a capture is
// still polling, and the settle after the capture sees the part let go.
void vpad_tick() {
    if (g_vpad == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_capture.mutex);
    const int64_t t = now_us();
    if ((g_vpad_due_us != 0) && (t >= g_vpad_due_us)) {
        g_vpad_due_us = 0;
        const std::string n = g_vpad_names[g_vpad_next++];
        const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(n.c_str());
        const SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString(n.c_str());
        if (b != SDL_CONTROLLER_BUTTON_INVALID) {
            g_vpad_button = int(b);
            SDL_JoystickSetVirtualButton(g_vpad, int(b), SDL_PRESSED);
        } else if (a != SDL_CONTROLLER_AXIS_INVALID) {
            g_vpad_axis = int(a);
            SDL_JoystickSetVirtualAxis(g_vpad, int(a), 32767);
        }
        g_vpad_release_us = t + 150000;
        printf("[SNAP-Input] SNAP_VPAD_TEST: \"%s\" pressed on the virtual pad\n", n.c_str());
        fflush(stdout);
    }
    if ((g_vpad_release_us != 0) && (t >= g_vpad_release_us)) {
        g_vpad_release_us = 0;
        if (g_vpad_button >= 0) {
            SDL_JoystickSetVirtualButton(g_vpad, g_vpad_button, SDL_RELEASED);
        }
        if (g_vpad_axis >= 0) {
            SDL_JoystickSetVirtualAxis(g_vpad, g_vpad_axis, 0);
        }
        g_vpad_button = -1;
        g_vpad_axis = -1;
    }
}

// A source the fast_forward or slow_motion entry has (by its name as the
// page would bind it): it has a job of its own, like the keys below.
static bool port_key_uses(const std::string& name) {
    Source s{};
    if (!resolve_source(name, s)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_bindings_mutex);
    for (const Source& f : g_extra_sources) {
        if ((f.kind == s.kind) && (f.code == s.code)) {
            return true;
        }
    }
    return false;
}

// Keys the port answers to itself (src/settings.cpp handle_settings_hotkey,
// main.cpp's Esc): a game button cannot be one of them.
bool key_has_job(int sc) {
    switch (sc) {
        case SDL_SCANCODE_F1: case SDL_SCANCODE_F2: case SDL_SCANCODE_F3: case SDL_SCANCODE_F4:
        case SDL_SCANCODE_F5: case SDL_SCANCODE_F6: case SDL_SCANCODE_F7: case SDL_SCANCODE_F8:
        case SDL_SCANCODE_F9: case SDL_SCANCODE_F10: case SDL_SCANCODE_F11: case SDL_SCANCODE_F12:
        case SDL_SCANCODE_P: case SDL_SCANCODE_LEFTBRACKET: case SDL_SCANCODE_RIGHTBRACKET:
        case SDL_SCANCODE_HOME: case SDL_SCANCODE_END: case SDL_SCANCODE_ESCAPE:
            return true;
        default:
            return false;
    }
}

void capture_end_with(CaptureState state, const std::string& name) {
    std::lock_guard<std::mutex> lock(g_capture.mutex);
    if (g_capture.state != CaptureState::Listening) {
        return;
    }
    g_capture.state = state;
    g_capture.name = name;
}

// The window's thread: an event while the capture listens. True when the
// event was the capture's to take, whatever it made of it; a press on a
// device other than the one the page shows is taken and ignored, and no
// press reaches the game either way.
bool capture_event(const SDL_Event& event) {
    int device;
    {
        std::lock_guard<std::mutex> lock(g_capture.mutex);
        if (g_capture.state != CaptureState::Listening) {
            return false;
        }
        device = g_capture.device;
    }
    const bool n64 = pad_snapshot().n64_layout;
    switch (event.type) {
        case SDL_KEYDOWN: {
            if (event.key.repeat) {
                return true;
            }
            const int sc = event.key.keysym.scancode;
            // The Deck's desktop layout sends these for the pad's buttons.
            if (g_deck_pad_keys_ignored.load(std::memory_order_relaxed) && deck_layout_key(sc)) {
                return true;
            }
            if (sc == SDL_SCANCODE_ESCAPE) {
                capture_end_with(CaptureState::Canceled, "");
                return true;
            }
            if (device != kBindKeyboard) {
                return true;
            }
            const char* n = SDL_GetScancodeName(SDL_Scancode(sc));
            if ((n == nullptr) || (n[0] == '\0')) {
                return true;
            }
            capture_end_with((key_has_job(sc) || port_key_uses(n)) ? CaptureState::RefusedJob : CaptureState::Bound, n);
            return true;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if ((event.button.which == SDL_TOUCH_MOUSEID) || (device != kBindMouse) ||
                deck_trigger_click(event.button.button)) {
                return true;
            }
            const char* n = nullptr;
            switch (event.button.button) {
                case SDL_BUTTON_LEFT: n = "Mouse Left"; break;
                case SDL_BUTTON_RIGHT: n = "Mouse Right"; break;
                case SDL_BUTTON_MIDDLE: n = "Mouse Middle"; break;
                case SDL_BUTTON_X1: n = "Mouse X1"; break;
                case SDL_BUTTON_X2: n = "Mouse X2"; break;
                default: break;
            }
            if (n != nullptr) {
                capture_end_with(CaptureState::Bound, n);
            }
            return true;
        }
        case SDL_MOUSEWHEEL: {
            if (device != kBindMouse) {
                return true;
            }
            const int y = (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -event.wheel.y : event.wheel.y;
            if (y > 0) {
                capture_end_with(CaptureState::Bound, "Wheel Up");
            } else if (y < 0) {
                capture_end_with(CaptureState::Bound, "Wheel Down");
            }
            return true;
        }
        case SDL_CONTROLLERBUTTONDOWN: {
            if (device != kBindPad) {
                return true;
            }
            const SDL_GameControllerButton b = SDL_GameControllerButton(event.cbutton.button);
            // Back opens the port's Options, the right stick's click (or
            // Capture, on a pad without one) saves the photo on screen, and
            // Guide is the platform's own; none can be a game button.
            if ((b == SDL_CONTROLLER_BUTTON_BACK) || (b == SDL_CONTROLLER_BUTTON_GUIDE) ||
                (b == SDL_CONTROLLER_BUTTON_RIGHTSTICK) ||
                (g_pad_misc1_photo && (b == SDL_CONTROLLER_BUTTON_MISC1)) ||
                (g_pad_nso_n64 && ((b == SDL_CONTROLLER_BUTTON_Y) || (b == SDL_CONTROLLER_BUTTON_X)))) {
                // (On the Switch Online N64 controller Y, X and Back are C buttons already.)
                capture_end_with(CaptureState::RefusedJob, pad_source_name(b));
                return true;
            }
            // The name that reads this part under the layout in force: an
            // N64-shaped pad's shoulder buttons are read through the
            // trigger names (pad_axis_held), and the Switch Online N64
            // controller's ZR through the right shoulder's (pad_button_held).
            std::string n;
            if (n64 && (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
                n = "Pad LeftTrigger";
            } else if (n64 && (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
                n = "Pad RightTrigger";
            } else if (n64 && g_pad_nso_n64 && (b == SDL_CONTROLLER_BUTTON_LEFTSTICK)) {
                n = "Pad RightShoulder";
            } else {
                n = pad_source_name(b);
            }
            // The fast-forward and slow-motion keys' buttons (input.h)
            // have jobs of their own, like Back.
            if (!n.empty() && port_key_uses(n)) {
                capture_end_with(CaptureState::RefusedJob, n);
                return true;
            }
            if (!n.empty()) {
                capture_end_with(CaptureState::Bound, n);
            }
            return true;
        }
        case SDL_CONTROLLERAXISMOTION: {
            if (device != kBindPad) {
                return true;
            }
            // Only the triggers, and only as they cross the eighth of their
            // travel a held one counts from: a stick's rest would bind at
            // once, and a trigger already held when the capture began is
            // not a press.
            int idx;
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                idx = 0;
            } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                idx = 1;
            } else {
                return true;
            }
            const bool above = event.caxis.value > 8000;
            bool rose;
            {
                std::lock_guard<std::mutex> lock(g_capture.mutex);
                rose = above && !g_capture.triggerAbove[idx];
                g_capture.triggerAbove[idx] = above;
            }
            if (rose && g_pad_nso_n64 && (idx == 1)) {
                capture_end_with(CaptureState::RefusedJob, "Pad RightTrigger");     // the Switch Online N64 controller's C-down
                return true;
            }
            if (rose) {
                const char* n = (idx == 0) ? (n64 ? "Pad LeftShoulder" : "Pad LeftTrigger")
                                           : ((n64 && !g_pad_r_on_trigger) ? "Pad RightShoulder" : "Pad RightTrigger");
                // Under the N64-shaped layout that can be the fast forward's
                // name, as for a button.
                capture_end_with(port_key_uses(n) ? CaptureState::RefusedJob : CaptureState::Bound, n);
            }
            return true;
        }
        default:
            return false;
    }
}

} // namespace

bool input_pad_attached() {
    return pad_snapshot().attached;
}

int input_last_device() {
    return g_last_device.load(std::memory_order_relaxed);
}

// True when the table in force binds a key to an input (or to the port's
// fast forward or slow motion).
static bool key_in_table(int sc) {
    const std::shared_ptr<const Resolved> table = resolved();
    if (!table) {
        return false;
    }
    auto has = [sc](const std::vector<Source>& v) {
        for (const Source& s : v) {
            if ((s.kind == SourceKind::Key) && (s.code == sc)) {
                return true;
            }
        }
        return false;
    };
    for (int i = 0; i < IN_COUNT; i++) {
        if (has(table->sources[i])) {
            return true;
        }
    }
    return has(table->fast_forward) || has(table->slow_motion);
}

bool input_take_clear_key() {
    return g_clear_key_presses.exchange(0, std::memory_order_relaxed) != 0;
}

std::string input_clear_key_name() {
    if (!key_in_table(SDL_SCANCODE_DELETE)) {
        return "Delete";
    }
    if (!key_in_table(SDL_SCANCODE_BACKSPACE)) {
        return "Backspace";
    }
    return "";
}

bool input_pad_n64_layout() {
    return pad_snapshot().n64_layout;
}

std::string input_bind_display(const char* input, int device) {
    const Bindings table = input_bindings();
    const auto it = table.find(input);
    if (it == table.end()) {
        return "None";
    }
    const bool n64 = pad_snapshot().n64_layout;
    const Settings& cfg = settings();
    std::string out;
    // What no table changes, named first so a row never says "None" of a
    // thing that works it: the two sticks under the controller (input_get
    // reads one as the stick and the other as the C buttons, swapped by
    // Pad Sticks), and the mouse's motion under the mouse while Mouse Aim
    // is on (apply_mouse_look turns the view with it).
    const bool cRow = std::strncmp(input, "c_", 2) == 0;
    const bool stickRow = std::strncmp(input, "stick_", 6) == 0;
    if (device == kBindPad) {
        if (cRow) {
            out = cfg.pad_sticks_swapped ? "L Stick" : "R Stick";
        }
        else if (stickRow) {
            out = cfg.pad_sticks_swapped ? "R Stick" : "L Stick";
        }
    }
    else if ((device == kBindMouse) && stickRow && cfg.mouse_aim) {
        out = "Motion";
    }
    // Two names on a row are joined with "or": "B or X" says that either
    // presses the button, where "B, X" read as a puzzle (me, on the
    // release's own screenshot, 2026-09-13).
    for (const std::string& s : it->second) {
        if (source_device(s) != device) {
            continue;
        }
        if (!out.empty()) {
            out += " or ";
        }
        out += display_name(s, n64);
    }
    // Esc's tap is Start whatever the table says (input_tap_start), so the
    // row says so last; it cannot be bound or cleared.
    if ((device == kBindKeyboard) && (std::strcmp(input, "start") == 0)) {
        if (!out.empty()) {
            out += " or ";
        }
        out += "Esc";
    }
    return out.empty() ? std::string("None") : out;
}

void input_bind_set(const char* input, int device, const std::string& source) {
    Bindings table = input_bindings();
    std::vector<std::string>& list = table[input];
    std::vector<std::string> kept;
    for (const std::string& s : list) {
        if (source_device(s) != device) {
            kept.push_back(s);
        }
    }
    kept.push_back(source);
    list = kept;
    printf("[SNAP-Input] keys.%s: \"%s\" bound on the Button Setup page (the %s)\n", input, source.c_str(), device_name(device));
    bind_publish(table);
}

bool input_bind_clear(const char* input, int device) {
    Bindings table = input_bindings();
    std::vector<std::string>& list = table[input];
    std::vector<std::string> kept;
    for (const std::string& s : list) {
        if (source_device(s) != device) {
            kept.push_back(s);
        }
    }
    if (kept.empty()) {
        printf("[SNAP-Input] keys.%s: not cleared on the Button Setup page; nothing else would press it\n", input);
        fflush(stdout);
        return false;
    }
    if (kept.size() == list.size()) {
        return true;   // nothing of that device to clear
    }
    list = kept;
    printf("[SNAP-Input] keys.%s: the %s cleared on the Button Setup page\n", input, device_name(device));
    bind_publish(table);
    return true;
}

void input_bind_reset(int device) {
    Bindings table = input_bindings();
    for (const auto& def : defaults()) {
        std::vector<std::string>& list = table[def.first];
        std::vector<std::string> kept;
        for (const std::string& s : list) {
            if (source_device(s) != device) {
                kept.push_back(s);
            }
        }
        for (const std::string& s : def.second) {
            if (source_device(s) == device) {
                kept.push_back(s);
            }
        }
        list = kept;
    }
    printf("[SNAP-Input] the shipped %s bindings put back from the Button Setup page\n", device_name(device));
    bind_publish(table);
}

void input_capture_begin(int device) {
    const PadSnapshot pad = pad_snapshot();
    // SNAP_BIND_TEST: a comma-separated list of source names, each taken
    // as the press of one capture three tenths of a second in, so a replay
    // can exercise the page with nobody at the keys.
    static const std::vector<std::string> testNames = [] {
        std::vector<std::string> v;
        if (const char* e = getenv("SNAP_BIND_TEST")) {
            std::string cur;
            for (const char* p = e; ; p++) {
                if ((*p == ',') || (*p == '\0')) {
                    if (!cur.empty()) v.push_back(cur);
                    cur.clear();
                    if (*p == '\0') break;
                } else {
                    cur.push_back(*p);
                }
            }
        }
        return v;
    }();
    static size_t testNext = 0;
    {
        std::lock_guard<std::mutex> lock(g_capture.mutex);
        g_capture.device = device;
        g_capture.startedUs = now_us();
        g_capture.state = CaptureState::Listening;
        g_capture.name.clear();
        g_capture.triggerAbove[0] = snapshot_axis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
        g_capture.triggerAbove[1] = snapshot_axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        g_capture.testDueUs = 0;
        if (testNext < testNames.size()) {
            g_capture.testName = testNames[testNext++];
            g_capture.testDueUs = g_capture.startedUs + CaptureTestDelayUs;
        }
        if (g_vpad_test && (device == kBindPad)) {
            if (g_vpad_names.empty()) {
                std::string cur;
                for (const char* p = getenv("SNAP_VPAD_TEST"); ; p++) {
                    if ((*p == ',') || (*p == '\0')) {
                        if (!cur.empty()) g_vpad_names.push_back(cur);
                        cur.clear();
                        if (*p == '\0') break;
                    } else {
                        cur.push_back(*p);
                    }
                }
            }
            if (g_vpad_next < g_vpad_names.size()) {
                g_vpad_due_us = g_capture.startedUs + CaptureTestDelayUs;
            }
        }
    }
    g_capture_active.store(true, std::memory_order_relaxed);
    printf("[SNAP-Input] Button Setup page: listening for a press on the %s\n", device_name(device));
    fflush(stdout);
}

CaptureState input_capture_poll(std::string* name) {
    std::lock_guard<std::mutex> lock(g_capture.mutex);
    if (g_capture.state == CaptureState::Listening) {
        const int64_t t = now_us();
        if ((g_capture.testDueUs != 0) && (t >= g_capture.testDueUs)) {
            g_capture.testDueUs = 0;
            Source src{};
            const bool usable = resolve_source(g_capture.testName, src) &&
                                (source_device(g_capture.testName) == g_capture.device);
            g_capture.state = usable ? CaptureState::Bound : CaptureState::RefusedJob;
            g_capture.name = g_capture.testName;
            printf("[SNAP-Input] SNAP_BIND_TEST: \"%s\" taken as the press (%s)\n",
                   g_capture.testName.c_str(), usable ? "usable" : "not a name of this device; refused");
            fflush(stdout);
        } else if (t - g_capture.startedUs >= CaptureTimeoutUs) {
            g_capture.state = CaptureState::TimedOut;
            printf("[SNAP-Input] Button Setup page: nothing pressed in eight seconds; the row stays as it was\n");
            fflush(stdout);
        }
    }
    if (name != nullptr) {
        *name = g_capture.name;
    }
    return g_capture.state;
}

void input_capture_end() {
    {
        std::lock_guard<std::mutex> lock(g_capture.mutex);
        g_capture.state = CaptureState::Idle;
        g_capture.name.clear();
    }
    g_capture_active.store(false, std::memory_order_relaxed);
    g_capture_settle.store(true, std::memory_order_relaxed);
}

// --- the text editor ---------------------------------------------------
struct TextEdit {
    std::mutex mutex;
    std::string text;
    size_t maxBytes = 64;
    int result = 0;   // 0 typing, 1 kept, 2 left
};
TextEdit g_text;
std::atomic<bool> g_text_active{false};
std::atomic<bool> g_text_hold_keys{false};
std::atomic<bool> g_typing{false};   // the photographer card has the keyboard (below)

void input_text_begin(const std::string& value, size_t maxBytes) {
    std::lock_guard<std::mutex> lock(g_text.mutex);
    g_text.text = value.substr(0, maxBytes);
    g_text.maxBytes = maxBytes;
    g_text.result = 0;
    g_text_active.store(true, std::memory_order_relaxed);
}

bool input_text_active() {
    return g_text_active.load(std::memory_order_relaxed);
}

int input_text_poll(std::string& text) {
    std::lock_guard<std::mutex> lock(g_text.mutex);
    text = g_text.text;
    return g_text.result;
}

void input_text_end() {
    g_text_active.store(false, std::memory_order_relaxed);
    g_text_hold_keys.store(true, std::memory_order_relaxed);
}

void input_update_text() {
    static bool on = false;
    static uint32_t testEndAt = 0;
    const bool card = g_typing.load(std::memory_order_relaxed);
    const bool want = g_text_active.load(std::memory_order_relaxed) || card;
    if (want && !on) {
        SDL_StartTextInput();
        on = true;
        // SNAP_TEXT_TEST=<text> (SNAP_TEXT_TEST_ESC=1 to leave instead):
        // SDL's own events once typing begins -- the text and one character
        // more, a Backspace for that one, and Enter or Esc 1.5 s later -- so
        // a replay types through the same path as a keyboard. SNAP_TYPE_TEST
        // does the same on the photographer card ("^e" stands for e acute,
        // which a Windows environment cannot carry as UTF-8), ending on Enter.
        const char* test = std::getenv(card ? "SNAP_TYPE_TEST" : "SNAP_TEXT_TEST");
        std::string typed = (test != nullptr) ? test : "";
        for (size_t at = typed.find("^e"); at != std::string::npos; at = typed.find("^e")) {
            typed.replace(at, 2, "\xC3\xA9");
        }
        test = typed.c_str();
        if ((test != nullptr) && (test[0] != '\0')) {
            SDL_Event e{};
            e.type = SDL_TEXTINPUT;
            SDL_snprintf(e.text.text, sizeof(e.text.text), "%sx", test);
            if (card) {
                printf("[SNAP-Input] SNAP_TYPE_TEST: typing \"%s\" (and an x, then Backspace) on the card\n", test);
                fflush(stdout);
            }
            SDL_PushEvent(&e);
            SDL_Event bs{};
            bs.type = SDL_KEYDOWN;
            bs.key.keysym.scancode = SDL_SCANCODE_BACKSPACE;
            SDL_PushEvent(&bs);
            testEndAt = SDL_GetTicks() + 1500;
        }
    } else if (want && (testEndAt != 0) && (SDL_GetTicks() >= testEndAt)) {
        testEndAt = 0;
        SDL_Event e{};
        e.type = SDL_KEYDOWN;
        const char* esc = card ? nullptr : std::getenv("SNAP_TEXT_TEST_ESC");
        e.key.keysym.scancode = ((esc != nullptr) && (esc[0] == '1')) ? SDL_SCANCODE_ESCAPE : SDL_SCANCODE_RETURN;
        SDL_PushEvent(&e);
    } else if (!want && on) {
#if defined(__linux__)
        SDL_StopTextInput();
#endif
        on = false;
    }
}

// A key or a typed text while the editor is on: every one is the editor's.
static bool text_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_TEXTINPUT: {
            std::lock_guard<std::mutex> lock(g_text.mutex);
            if (g_text.result != 0) {
                return true;
            }
            std::string add;
            for (const char* c = event.text.text; *c != '\0'; c++) {
                if (uint8_t(*c) >= 0x20) {
                    add += *c;
                }
            }
            if (g_text.text.size() + add.size() <= g_text.maxBytes) {
                g_text.text += add;
            }
            return true;
        }
        case SDL_KEYDOWN: {
            const int sc = event.key.keysym.scancode;
            std::lock_guard<std::mutex> lock(g_text.mutex);
            if (g_text.result != 0) {
                return true;
            }
            if (sc == SDL_SCANCODE_BACKSPACE) {
                // One character, whole: the bytes of a UTF-8 sequence after
                // its first all start 10.
                while (!g_text.text.empty() && ((uint8_t(g_text.text.back()) & 0xC0) == 0x80)) {
                    g_text.text.pop_back();
                }
                if (!g_text.text.empty()) {
                    g_text.text.pop_back();
                }
            } else if ((sc == SDL_SCANCODE_RETURN) || (sc == SDL_SCANCODE_KP_ENTER)) {
                g_text.result = 1;
            } else if (sc == SDL_SCANCODE_ESCAPE) {
                g_text.result = 2;
            }
            return true;
        }
        case SDL_KEYUP:
        case SDL_TEXTEDITING:
            return true;
        default:
            return false;
    }
}

// --- typing on the photographer card --------------------------------------
// The card's letters are picked from a grid with the stick; a keyboard can
// type them as well. While the card claims the keyboard (the mailbox claim,
// raised by the card every frame), every character typed is queued here and
// handed to the card one at a time; the keys that type, Backspace and Enter
// are the card's, so X is the letter X and not the A button, and the rest
// (the arrows, Esc, the function keys) keep their bindings.
std::mutex g_type_mutex;
std::deque<uint8_t> g_type_queue;

// The keys that type: their bindings rest while the card is up.
static bool typing_key(int sc) {
    if ((sc >= SDL_SCANCODE_A) && (sc <= SDL_SCANCODE_0)) {          // letters, digits
        return true;
    }
    if ((sc >= SDL_SCANCODE_SPACE) && (sc <= SDL_SCANCODE_SLASH)) {  // space, punctuation
        return true;
    }
    if ((sc >= SDL_SCANCODE_KP_DIVIDE) && (sc <= SDL_SCANCODE_KP_PERIOD)) {   // the keypad
        return true;
    }
    return (sc == SDL_SCANCODE_RETURN) || (sc == SDL_SCANCODE_BACKSPACE) || (sc == SDL_SCANCODE_NONUSBACKSLASH);
}

static void type_push(uint8_t c) {
    std::lock_guard<std::mutex> lock(g_type_mutex);
    if (g_type_queue.size() < 32) {
        g_type_queue.push_back(c);
    }
}

// A key or a typed text while the card has the keyboard.
static bool type_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_TEXTINPUT: {
            // UTF-8 in; the card's letters are ASCII's and one more, e acute.
            const uint8_t* p = reinterpret_cast<const uint8_t*>(event.text.text);
            while (*p != 0) {
                if ((*p >= 0x20) && (*p < 0x7F)) {
                    type_push(*p);
                    p++;
                } else if ((p[0] == 0xC3) && ((p[1] == 0xA9) || (p[1] == 0x89))) {
                    type_push(0xE9);   // e acute, either case: the card has the one
                    p += 2;
                } else {
                    p++;
                    while ((*p & 0xC0) == 0x80) {
                        p++;
                    }
                }
            }
            return true;
        }
        case SDL_KEYDOWN: {
            const int sc = event.key.keysym.scancode;
            if (sc == SDL_SCANCODE_BACKSPACE) {
                type_push(0x08);
                return true;
            }
            if ((sc == SDL_SCANCODE_RETURN) || (sc == SDL_SCANCODE_KP_ENTER)) {
                // The Deck's desktop layout sends Return for its pad's A.
                const bool deckA = (sc == SDL_SCANCODE_RETURN) &&
                                   g_deck_pad_keys_ignored.load(std::memory_order_relaxed);
                if (!event.key.repeat && !deckA) {
                    type_push(0x0D);
                }
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

bool input_typing_takes(int scancode) {
    return g_typing.load(std::memory_order_relaxed) && typing_key(scancode);
}

// Each reading: the claim lowered, the mode followed, and the next character
// handed over when the card has taken the last. On the game's thread.
static void type_update(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    const uint8_t claim = read_u8(rdram, ADDR_MailboxTypeClaim);
    const bool on = claim != 0;
    if (on) {
        write_u8(rdram, ADDR_MailboxTypeClaim, uint8_t(claim - 1));
    }
    const bool was = g_typing.exchange(on, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_type_mutex);
    if (!on) {
        g_type_queue.clear();
        if (was) {
            // Every key up before any reaches the game again: the Enter that
            // finished the card would otherwise be a fresh Start.
            g_text_hold_keys.store(true, std::memory_order_relaxed);
        }
        return;
    }
    if ((read_u8(rdram, ADDR_MailboxTypeChar) == 0) && !g_type_queue.empty()) {
        write_u8(rdram, ADDR_MailboxTypeChar, g_type_queue.front());
        g_type_queue.pop_front();
    }
}

bool input_capture_active() {
    return g_capture_active.load(std::memory_order_relaxed);
}

void input_tap_start() {
    g_esc_start_until.store(now_us() + PressHoldUs, std::memory_order_relaxed);
}

// A click on one of the game's photo screens (src/menu_mouse.cpp): the
// item is selected at once, and its A (or B) is pressed on the next reading
// and released on the one after, so the screen's own edge detection sees
// one press, whatever order it reads its buttons in.
void input_inject_press(uint16_t n64_buttons) {
    g_inject_buttons.fetch_or(n64_buttons, std::memory_order_relaxed);
}

void input_request_menu() {
    g_menu_request.store(1, std::memory_order_relaxed);
}

bool input_take_menu_request() {
    return g_menu_request.exchange(0, std::memory_order_relaxed) != 0;
}

// Android's touch controls (touch_controls.cpp): a drag in a course turns
// the view as the mouse's motion does.
void input_add_touch_look(float dx, float dy) {
    std::lock_guard<std::mutex> lock(g_motion_mutex);
    g_motion_dx += dx;
    g_motion_dy += dy;
}

void input_release_mouse() {
    if (g_captured.load(std::memory_order_relaxed)) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        g_captured.store(false, std::memory_order_relaxed);
        clear_mouse();
    }
}

static bool replaying_run() {
    static const bool replaying = (getenv("SNAP_REPLAY") != nullptr);
    return replaying;
}

// Where the pointer is, from an event: its window position and the window's
// size then. A touch is not a pointer (a Steam Deck's screen under the
// thumbs), and neither is the warp SDL makes when the capture changes.
static void pointer_seen(uint32_t windowID, int32_t x, int32_t y, uint32_t which, bool motion) {
    if (((which == SDL_TOUCH_MOUSEID) && !touch_as_pointer(g_menu_claimed.load(std::memory_order_relaxed))) ||
        replaying_run()) {
        return;
    }
    int w = 0, h = 0;
    if (SDL_Window* win = SDL_GetWindowFromID(windowID)) {
        SDL_GetWindowSize(win, &w, &h);
    }
    g_ptr_win_w.store(w, std::memory_order_relaxed);
    g_ptr_win_h.store(h, std::memory_order_relaxed);
    g_ptr_wx.store(x, std::memory_order_relaxed);
    g_ptr_wy.store(y, std::memory_order_relaxed);
    const int64_t t = now_us();
    if (t >= g_ptr_quiet_until.load(std::memory_order_relaxed)) {
        if (motion) {
            g_ptr_moved.fetch_add(1, std::memory_order_relaxed);
        }
        // A click is as much a hand on the mouse as a move: it keeps the
        // pointer shown in fullscreen (a player clicking in place saw it
        // vanish under him).
        g_ptr_moved_at.store(t, std::memory_order_relaxed);
    }
}

// The window position in the game's coordinates, through the viewport the
// renderer last drew with -- the mapping of its debugger's own pick. False
// when the pointer is off the picture.
static bool pointer_in_game(int32_t& gx, int32_t& gy) {
    const int32_t wx = g_ptr_wx.load(std::memory_order_relaxed);
    const int32_t wy = g_ptr_wy.load(std::memory_order_relaxed);
    const int32_t ww = g_ptr_win_w.load(std::memory_order_relaxed);
    const int32_t wh = g_ptr_win_h.load(std::memory_order_relaxed);
    const snapdiag::PresentedView v = snapdiag::presentedView();
    if ((wx < 0) || (wy < 0) || (ww <= 0) || (wh <= 0) || (v.width <= 0.0f) || (v.height <= 0.0f) ||
        (v.fbWidth <= 0.0f) || (v.fbHeight <= 0.0f)) {
        return false;
    }
    const float px = float(wx) * float(v.swapWidth) / float(ww);
    const float py = float(wy) * float(v.swapHeight) / float(wh);
    const float sx = ((px - (v.x + v.width * 0.5f)) / (v.width * 0.5f)) * v.aspect;
    const float sy = (py - (v.y + v.height * 0.5f)) / (v.height * 0.5f);
    const float fx = v.fbWidth * 0.5f + sx * v.fbWidth * 0.5f;
    const float fy = v.fbHeight * 0.5f + sy * v.fbHeight * 0.5f;
    if ((fx < 0.0f) || (fy < 0.0f) || (fx >= v.fbWidth) || (fy >= v.fbHeight)) {
        return false;
    }
    gx = int32_t(fx);
    gy = int32_t(fy);
    return true;
}

// The pointer and its counts into the mailbox, once a reading.
static void publish_pointer(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    int32_t gx = -1, gy = -1;
    static const bool scripted = (getenv("SNAP_MOUSE_TEST") != nullptr);
    if (scripted) {
        gx = g_test_gx.load(std::memory_order_relaxed);
        gy = g_test_gy.load(std::memory_order_relaxed);
    } else if (!replaying_run() && !g_captured.load(std::memory_order_relaxed)) {
        if (!pointer_in_game(gx, gy)) {
            gx = gy = -1;
        }
    }
    *word_at(rdram, ADDR_MailboxPointer) = ((gx < 0) || (gy < 0))
        ? 0xFFFFFFFFu : ((uint32_t(gx & 0xFFFF) << 16) | uint32_t(gy & 0xFFFF));
    *word_at(rdram, ADDR_MailboxPointerCounts) =
        ((g_ptr_moved.load(std::memory_order_relaxed) & 0xFFu) << 24) |
        ((g_ptr_click.load(std::memory_order_relaxed) & 0xFFu) << 16) |
        ((g_ptr_back.load(std::memory_order_relaxed) & 0xFFu) << 8) |
        (g_ptr_wheel_up.load(std::memory_order_relaxed) & 0xFFu);
    write_u8(rdram, ADDR_MailboxWheelDown, uint8_t(g_ptr_wheel_down.load(std::memory_order_relaxed)));
    const bool held = scripted ? g_test_held.load(std::memory_order_relaxed)
                    : (!replaying_run() && !g_captured.load(std::memory_order_relaxed) &&
                       ((g_mouse_held.load(std::memory_order_relaxed) >> SDL_BUTTON_LEFT) & 1u));
    write_u8(rdram, ADDR_MailboxMouseHeld, held ? 1 : 0);
}

void input_handle_sdl_event(const SDL_Event& event) {
    note_press_device(event);
    touch_controls_event(event);
    // The Button Setup page is listening: a key, button or wheel tick is the
    // capture's, and reaches neither the latches below nor the game.
    if (g_capture_active.load(std::memory_order_relaxed) && capture_event(event)) {
        return;
    }
    // A mod's text option is being typed: the keyboard is the editor's.
    if (g_text_active.load(std::memory_order_relaxed) && text_event(event)) {
        return;
    }
    // The photographer card is up: what types is the card's.
    if (g_typing.load(std::memory_order_relaxed) && type_event(event)) {
        return;
    }
    const bool captured = g_captured.load(std::memory_order_relaxed);
    const bool buttons_live = g_focused.load(std::memory_order_relaxed) &&
                              (now_us() >= g_buttons_from.load(std::memory_order_relaxed));
    switch (event.type) {
        case SDL_KEYDOWN: {
            // The Button Setup page's clear keys, counted for the game's
            // thread (input_take_clear_key); the keys themselves reach the
            // game through the keyboard's state as every key does.
            const int sc = event.key.keysym.scancode;
            if (!event.key.repeat && ((sc == SDL_SCANCODE_DELETE) || (sc == SDL_SCANCODE_BACKSPACE)) &&
                !key_in_table(sc)) {
                g_clear_key_presses.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                g_focused.store(false, std::memory_order_relaxed);
                clear_mouse();
            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                g_focused.store(true, std::memory_order_relaxed);
                g_buttons_from.store(now_us() + FocusSettleUs, std::memory_order_relaxed);
            } else if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
                g_ptr_wx.store(-1, std::memory_order_relaxed);
                g_ptr_wy.store(-1, std::memory_order_relaxed);
            }
            break;
        // SDL turns a touch into mouse motion and a mouse button unless it is
        // told not to, and a Steam Deck has a touchscreen under the player's
        // thumbs. Only a real pointer aims the camera; the synthetic one would
        // swing the view every time the screen was brushed.
        case SDL_MOUSEMOTION:
            // Taking the mouse warps the cursor to the window's center, and SDL
            // reports the warp as motion: the pointer's offset from the center
            // arrived as one delta and the view started a course aimed where
            // the pointer had been (JackandBeans, Oct 5 2026). The quiet period
            // after a capture change drops it, as it does for the pointer; so
            // does any single delta too large for a hand.
            if (captured && (event.motion.which != SDL_TOUCH_MOUSEID) &&
                (now_us() >= g_ptr_quiet_until.load(std::memory_order_relaxed))) {
                if ((std::abs(event.motion.xrel) > 400) || (std::abs(event.motion.yrel) > 400)) {
                    printf("[SNAP-Input] mouse motion of %d,%d in one event dropped as a warp\n",
                           event.motion.xrel, event.motion.yrel);
                    fflush(stdout);
                    break;
                }
                std::lock_guard<std::mutex> lock(g_motion_mutex);
                g_motion_dx += float(event.motion.xrel);
                g_motion_dy += float(event.motion.yrel);
            }
            if (!captured) {
                pointer_seen(event.motion.windowID, event.motion.x, event.motion.y, event.motion.which, true);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            // On Android a tap outside a course is the pointer's click.
            if ((event.button.which == SDL_TOUCH_MOUSEID) && !touch_as_pointer(g_menu_claimed.load(std::memory_order_relaxed))) {
                break;
            }
            // Steam's click for a Deck trigger reaches nothing, and nor
            // does its release (deck_trigger_click).
            if ((event.button.which != SDL_TOUCH_MOUSEID) && (event.button.button < 8) &&
                deck_trigger_click(event.button.button)) {
                g_trigger_clicks |= 1u << event.button.button;
                if (trace_on()) {
                    const int axis = (event.button.button == SDL_BUTTON_LEFT) ? SDL_CONTROLLER_AXIS_TRIGGERRIGHT
                                                                             : SDL_CONTROLLER_AXIS_TRIGGERLEFT;
                    printf("[SNAP-Input] trace %u ms: mouse button %d dropped, its trigger is held at %d (Steam's click)\n",
                           unsigned(SDL_GetTicks()), int(event.button.button), int(pad_snapshot().axes[axis]));
                    fflush(stdout);
                }
                break;
            }
            if (buttons_live && event.button.button < 8) {
                g_mouse_held.fetch_or(1u << event.button.button, std::memory_order_relaxed);
                g_mouse_press_until[event.button.button].store(now_us() + PressHoldUs, std::memory_order_relaxed);
            }
            // The pointer's shutter, on the pointer's own click (pointer.cpp).
            if (!captured && (event.button.button == SDL_BUTTON_LEFT)) {
                pointer_click();
            }
            if (buttons_live && !captured && !replaying_run()) {
                pointer_seen(event.button.windowID, event.button.x, event.button.y, event.button.which, false);
                if (event.button.button == SDL_BUTTON_LEFT) {
                    g_ptr_click.fetch_add(1, std::memory_order_relaxed);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    g_ptr_back.fetch_add(1, std::memory_order_relaxed);
                }
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if ((event.button.which == SDL_TOUCH_MOUSEID) && !touch_as_pointer(g_menu_claimed.load(std::memory_order_relaxed))) {
                break;
            }
            if ((event.button.button < 8) && ((g_trigger_clicks >> event.button.button) & 1u)) {
                g_trigger_clicks &= ~(1u << event.button.button);
                break;
            }
            if (event.button.button < 8) {
                g_mouse_held.fetch_and(~(1u << event.button.button), std::memory_order_relaxed);
            }
            break;
        case SDL_MOUSEWHEEL:
            if (buttons_live) {
                const int y = (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -event.wheel.y : event.wheel.y;
                if (y > 0) g_wheel_up_until.store(now_us() + PressHoldUs, std::memory_order_relaxed);
                if (y < 0) g_wheel_down_until.store(now_us() + PressHoldUs, std::memory_order_relaxed);
                if (!captured && !replaying_run()) {
                    if (y > 0) g_ptr_wheel_up.fetch_add(1, std::memory_order_relaxed);
                    if (y < 0) g_ptr_wheel_down.fetch_add(1, std::memory_order_relaxed);
                    // A turn of the wheel is a hand on the mouse too: it
                    // keeps the pointer shown in fullscreen, as a move does.
                    g_ptr_moved_at.store(now_us(), std::memory_order_relaxed);
                }
            }
            break;
#if defined(__ANDROID__)
        case SDL_SENSORUPDATE:
            phone_sensor_reading(event.sensor);
            break;
#endif
        case SDL_CONTROLLERSENSORUPDATE:
            if ((event.csensor.sensor == SDL_SENSOR_ACCEL) &&
                g_gyro_enabled.load(std::memory_order_relaxed) &&
                (event.csensor.which == g_gyro_instance.load(std::memory_order_relaxed))) {
                const float a[3] = {event.csensor.data[0], event.csensor.data[1],
                                    event.csensor.data[2]};
                // A reading only says which way is up while the pad is not
                // being accelerated: at rest its length is one gravity. A
                // shaken pad, or one in a moving vehicle, reads something
                // else, and folding that in would tilt the axis yaw is
                // measured against. Anything well away from 9.8 m/s^2 is
                // simply not used; the last good vector stands.
                const float am = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
                if ((am < 6.5f) || (am > 13.0f)) {
                    break;
                }
                const float have = std::sqrt(g_grav[0] * g_grav[0] + g_grav[1] * g_grav[1] +
                                             g_grav[2] * g_grav[2]);
                if (have < 0.1f) {
                    g_grav[0] = a[0]; g_grav[1] = a[1]; g_grav[2] = a[2];
                } else {
                    constexpr float Follow = 0.015f;   // about a quarter second at 250 Hz
                    for (int i = 0; i < 3; i++) {
                        g_grav[i] += Follow * (a[i] - g_grav[i]);
                    }
                }
            }
            // Each reading is a rate; integrated here over the sensor's own
            // clock (its timestamp, when the driver gives one) or its
            // nominal rate, into an angle the game thread adds to the view.
            if ((event.csensor.sensor == SDL_SENSOR_GYRO) &&
                g_gyro_enabled.load(std::memory_order_relaxed) &&
                (event.csensor.which == g_gyro_instance.load(std::memory_order_relaxed))) {
                // The interval the sensor's own rate implies, and the one its
                // clock claims. The claim is believed only when the two agree
                // within a factor of four: SDL's Steam Deck driver ticks
                // timestamp_us in units of its own, and taking it for
                // microseconds made every reading worth a millionth of the
                // turn it represented, so a 14 rad/s swing moved the view by
                // nothing at all (found on a Deck, 2026-09-06). A clock that
                // disagrees is discarded, not scaled: what it is counting is
                // not known, only that it is not microseconds.
                const uint64_t ts = event.csensor.timestamp_us;
                // Where the driver reports a rate, the sensor's own clock is
                // believed only when it agrees with that rate within a factor
                // of four. Where it reports none -- SDL's evdev sensor path
                // does -- there is nothing to check against, so any interval
                // that is physically sensible for a motion sensor is taken,
                // and only the assumed interval stands in when even that
                // fails. Guessing 250 Hz and then rejecting a good clock for
                // disagreeing with the guess would be worse than not checking.
                const bool rateKnown = (g_gyro_rate_hz > 0.0f);
                const float nominal = rateKnown ? (1.0f / g_gyro_rate_hz) : 0.004f;
                const float lo = rateKnown ? (nominal * 0.25f) : 0.0002f;   /* 5 kHz */
                const float hi = rateKnown ? (nominal * 4.0f) : 0.05f;      /* 20 Hz */
                float dt = nominal;
                bool clockUsed = false;
                if ((ts != 0) && (g_gyro_last_us != 0) && (ts > g_gyro_last_us)) {
                    const float measured = float(ts - g_gyro_last_us) * 1e-6f;
                    if ((measured > lo) && (measured < hi)) {
                        dt = measured;
                        clockUsed = true;
                    }
                }
                if (ts != 0) g_gyro_last_us = ts;
                if (dt > 0.05f) dt = 0.05f;   // a gap (focus, a stall) is not a turn
                float rx = event.csensor.data[0];   // about the pad's right axis: pitch
                float ry = event.csensor.data[1];   // about the pad's up axis: yaw
                g_gyro_readings.fetch_add(1, std::memory_order_relaxed);
                if ((rx != 0.0f) || (ry != 0.0f) || (event.csensor.data[2] != 0.0f)) {
                    g_gyro_live_us.store(now_us(), std::memory_order_relaxed);
                }
                // Yaw is the turn about the world's vertical: the reading
                // projected onto the up vector the accelerometer gives (SDL's
                // accelerometer reads +9.8 along the axis pointing away from
                // the earth, so g_grav is UP). Held upright that projection
                // is exactly ry, the pad's own up axis, which is what the
                // fallback for a pad with no accelerometer uses; tilted back
                // -- how a Deck is actually held -- it is the part of the
                // turn that ry alone was missing. The negation to the game's
                // own yaw sense happens once, below, for both paths.
                //
                // A first version of this negated the projection here as
                // well, on the belief that the vector pointed down. It aimed
                // backwards. The first Deck report said it felt right; a
                // deliberate left-turn test on 2026-09-07 said otherwise, and
                // two reviewers had said so from the math before that. A
                // sign is confirmed by turning left and watching, never by
                // feel.
                const float rz = event.csensor.data[2];
                const float gm = std::sqrt(g_grav[0] * g_grav[0] + g_grav[1] * g_grav[1] +
                                           g_grav[2] * g_grav[2]);
                float yawRate = ry;
                if (gm > 1.0f) {
                    yawRate = (rx * g_grav[0] + ry * g_grav[1] + rz * g_grav[2]) / gm;
                }
                // Tightening: under a degree per second the rate is scaled
                // toward zero, so a pad at rest does not creep, without the
                // dead band a cutoff would put on slow, deliberate aiming.
                constexpr float Tight = 0.01745f;
                const float mag = std::sqrt(rx * rx + yawRate * yawRate);
                if (mag < Tight) {
                    const float f = mag / Tight;
                    rx *= f;
                    yawRate *= f;
                }
                std::lock_guard<std::mutex> lock(g_motion_mutex);
                g_gyro_yaw += -yawRate * dt;
                g_gyro_pitch += rx * dt;
                if (g_gyro_debug) {
                    const float raw[3] = {event.csensor.data[0], event.csensor.data[1],
                                          event.csensor.data[2]};
                    for (int i = 0; i < 3; i++) {
                        const float a = std::fabs(raw[i]);
                        if (a > g_dbg_peak[i]) g_dbg_peak[i] = a;
                    }
                    g_dbg_n++;
                    g_dbg_dt_sum += dt;
                    if (clockUsed) g_dbg_clock++;
                    g_dbg_grav[0] = g_grav[0];
                    g_dbg_grav[1] = g_grav[1];
                    g_dbg_grav[2] = g_grav[2];
                }
            }
            break;
        default:
            break;
    }
}

void input_update_mouse_capture() {
#if defined(__ANDROID__)
    phone_gyro_sync();
    touch_controls_tick();   // a photo the game took: the shutter is felt
#endif
    // A replayed run takes no input from the mouse (input_get), so it must
    // not take the cursor either: the release suite plays courses while
    // I am elsewhere on the same desktop, and a captured cursor locked me
    // out of it.
    static const bool replaying = (getenv("SNAP_REPLAY") != nullptr);
    const bool claimed = g_menu_claimed.load(std::memory_order_relaxed);
    // The title's attract demo is a course the mouse cannot steer (its
    // camera follows the recorded script), so it does not take the mouse
    // either: the cursor stays free over it, and a click is A, which ends
    // the demo as on the cartridge.
    const bool demo = (g_rdram != nullptr) && (read_s32(g_rdram, ADDR_gIdleScript) != 0);
    const bool wantedNow = !replaying &&
                           settings().mouse_aim &&
                           g_focused.load(std::memory_order_relaxed) &&
                           g_app_level_resident.load(std::memory_order_relaxed) &&
                           !claimed && !demo;
    // A course takes the mouse after the pointer's flash out: a capture
    // first wanted while the pointer is on screen waits four tenths of a
    // second (the flash's length, pointer.cpp), the flash playing, and then
    // takes it; a pointer already hidden is taken at once. A ride resumed
    // from the pause menu snatched the pointer away with no flash, the one
    // exit without one (JackandBeans, Oct 3 2026).
    constexpr int64_t kPointerFlashUs = 400000;
    static int64_t captureAtUs = 0;
    bool wanted = wantedNow;
    // The pointer has no business on screen in fullscreen: there is nothing
    // beside the game to point at, and on a Steam Deck, which boots
    // fullscreen, it sat over the picture for the whole session. Windowed it
    // stays, because the window still has chrome to drag, resize and close.
    // Kept out of the early return below: fullscreen can change while the
    // capture state does not.
    {
        static int cursorState = -1;
        static int64_t lastClaimUs = 0;
        const int64_t nowUs = now_us();
        if (wantedNow && !g_captured.load(std::memory_order_relaxed)) {
            if (captureAtUs == 0) {
                captureAtUs = (cursorState == SDL_ENABLE) ? (nowUs + kPointerFlashUs) : nowUs;
            }
            if (nowUs < captureAtUs) {
                wanted = false;
            }
        } else {
            captureAtUs = 0;
        }
        const bool capturePending = wantedNow && !wanted;
        // In fullscreen, a menu that takes the mouse shows the pointer while
        // it is being moved, and the pointer hides three seconds after the
        // hand stops, or a second and a half after the last menu that took
        // the mouse: a fade between two menus drops the mouse for less than
        // that, so the pointer rides through it, while a cutscene or the
        // intro keeps it dropped and the pointer goes (it went at every fade
        // before: JackandBeans, Oct 3 2026).
        if (claimed) {
            lastClaimUs = nowUs;
        }
        const int64_t idleUs = nowUs - g_ptr_moved_at.load(std::memory_order_relaxed);
        const int64_t unclaimedUs = nowUs - lastClaimUs;
        const int64_t hideInUs = std::min<int64_t>(3000000 - idleUs, 1500000 - unclaimedUs);
        const bool pointing = hideInUs > 0;
        const int want = (settings().fullscreen && !wanted && !pointing) ? SDL_DISABLE : SDL_ENABLE;
        if (cursorState != want) {
            SDL_ShowCursor(want);
            cursorState = want;
        }
        // The port's own pointer: it flashes in as it comes on screen
        // (shown, and not captured for aiming), flashes out in the last
        // moments before either hide, and plays on from here.
        pointer_visible((want == SDL_ENABLE) && !wanted);
        if (capturePending) {
            pointer_hiding_in(int32_t(std::clamp<int64_t>((captureAtUs - nowUs) / 1000, 0, INT32_MAX)));
        } else if (settings().fullscreen && !wanted) {
            pointer_hiding_in(int32_t(std::clamp<int64_t>(hideInUs / 1000, INT32_MIN, INT32_MAX)));
        } else {
            pointer_hiding_in(INT32_MAX);
        }
        pointer_update();
    }
    const bool current = g_captured.load(std::memory_order_relaxed);
    if (wanted == current) {
        return;
    }
    if (SDL_SetRelativeMouseMode(wanted ? SDL_TRUE : SDL_FALSE) != 0) {
        printf("[SNAP-Input] mouse capture %s failed: %s\n", wanted ? "on" : "off", SDL_GetError());
        fflush(stdout);
        return;
    }
    g_captured.store(wanted, std::memory_order_relaxed);
    g_ptr_quiet_until.store(now_us() + 150000, std::memory_order_relaxed);
    if (!wanted) {
        clear_mouse();
    }
    printf("[SNAP-Input] mouse %s\n", wanted ? "captured: it aims the camera" : "released");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

// The Back button's press, taken from SDL's event stream rather than from
// the state polled below. The game reads button STATE (input_get), and a
// photo is saved on a press. SDL turns the press into an
// SDL_CONTROLLERBUTTONDOWN event on the thread that polls the controllers
// -- the pad thread, in its SDL_GameControllerUpdate -- and an event watch
// is SDL's hook into that delivery: it runs synchronously, on that thread,
// as the event is queued. export_photo takes its own lock, so the P key's
// path on the main thread and this one never overlap. The return value of
// a watch is ignored by SDL.
static int SDLCALL photo_button_watch(void* /*userdata*/, SDL_Event* event) {
    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        if ((event->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) && !g_pad_nso_n64) {
            // Select: the port's Options, from anywhere; Start while they
            // are up, which closes them. On the Switch Online N64
            // controller Back is C-right: the Options are on the pause menu.
            if (menu_pages_open()) {
                input_tap_start();
            } else {
                input_request_menu();
            }
        } else if ((event->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) ||
                   ((event->cbutton.button == SDL_CONTROLLER_BUTTON_MISC1) && g_pad_misc1_photo)) {
            export_photo(g_rdram);
        }
    }
    return 1;
}

// SDL knows a few thousand pads already. For the rest there is
// gamecontrollerdb.txt, the community's own list, which other ports of
// this kind ship and, since 1.0.3, so does this one, beside the executable
// (assets/ in the tree; NOTICE.md). A newer copy dropped over it is read the
// same way. The alternative SDL offers is an environment variable holding
// one raw mapping string, which is a developer's tool, not a player's. The
// file is expected now, so its absence is worth a line.
static void load_controller_mappings() {
    // Read from beside the executable, where it ships. A copy in the data
    // directory -- the same folder on Windows and on a writable Linux
    // install; ~/.config/Snap64Recomp on a read-only one (paths.h) -- is
    // read after it, and its lines win for the pads both name.
    const std::string path = exe_path("gamecontrollerdb.txt").string();
    int added = SDL_GameControllerAddMappingsFromFile(path.c_str());
    if (base_dir() != exe_dir()) {
        const int more = SDL_GameControllerAddMappingsFromFile(base_path("gamecontrollerdb.txt").string().c_str());
        if (more > 0) {
            added = ((added > 0) ? added : 0) + more;
        }
    }
    if (added > 0) {
        printf("[SNAP-Input] gamecontrollerdb.txt: %d controller mappings added\n", added);
        fflush(stdout);
    } else if (added == 0) {
        printf("[SNAP-Input] gamecontrollerdb.txt: no mappings in the file\n");
        fflush(stdout);
    } else {
        printf("[SNAP-Input] gamecontrollerdb.txt: not beside the executable; only the pads SDL knows on its own are used\n");
        fflush(stdout);
    }
}

// Whether SDL's mapping for joystick i gives it a stick. A device with
// buttons alone -- the Android emulator's own keyboard, which SDL maps as A,
// B, Start and a D-pad -- is the pad only while nothing better is attached.
static bool joystick_has_stick(int i) {
    char* mapping = SDL_GameControllerMappingForDeviceIndex(i);
    const bool stick = (mapping != nullptr) && (SDL_strstr(mapping, "leftx:") != nullptr);
    SDL_free(mapping);
    return stick;
}

// Pads shaped like the N64's whose names do not say so. The mapping list
// the port ships (assets/gamecontrollerdb.txt) maps each as it maps the N64
// pads -- L and R the shoulders, Z the left trigger -- under a name without
// "N64" in it, so the name test below gave them the standard layout, which
// reads their L as Z and their R as the fast forward. Read from the list,
// not from a pad. Their ids are the same on every platform's line.
struct PadIds {
    uint16_t vendor;
    uint16_t product;
};
static constexpr PadIds kN64ShapedPads[] = {
    {0x2DC8, 0x3019},   // "8BitDo 64"
    {0x374E, 0x0111},   // "M64 Pro Controller"
};

// The open pad's layout, from pad_layout: 0 Auto, by the pad's ids and then
// its name; 1 Standard and 2 N64 whatever the pad is. The Controls page's
// Pad Preset row sets pad_layout, and the pad thread calls this again when
// it changes, so the preset is in force at once (pad_thread_main).
static void decide_pad_layout() {
    const int wanted = settings().pad_layout;     // read once: the menu's thread writes it
    const int layout = std::clamp(wanted, 0, 2);
    g_pad_layout_applied = wanted;
    const char* name = SDL_GameControllerName(game_controller);
    const uint16_t vendor = SDL_GameControllerGetVendor(game_controller);
    const uint16_t product = SDL_GameControllerGetProduct(game_controller);
    // The Switch Online N64 controller by its ids (Nintendo 057E, product
    // 2019), which no driver or SDL renames; the name stays as a second way in.
    const bool nso_ids = (vendor == 0x057E) && (product == 0x2019);
    bool n64 = (layout == 2);
    const char* why = "from the Pad Preset (pad_layout)";
    if (layout == 0) {
        bool by_ids = nso_ids;
        for (const PadIds& known : kN64ShapedPads) {
            by_ids = by_ids || ((vendor == known.vendor) && (product == known.product));
        }
        n64 = by_ids;
        why = "Auto, from the pad's ids";
        if (!by_ids) {
            why = "Auto, from the pad's name";
            for (const char* p = name; (p != nullptr) && (*p != '\0'); p++) {
                if (SDL_strncasecmp(p, "N64", 3) == 0) {
                    n64 = true;
                    break;
                }
            }
        }
    }
    g_pad_layout_n64 = n64;
    // Its C buttons are a property of the pad, not of the layout: they stay
    // wired under Standard too. Only where they come as buttons, from SDL's
    // own driver, whose mapping has Y (C-up). Through the community list the
    // pad is the "NSO N64 Controller": C buttons as a right stick, ZR as the
    // right trigger, read as any N64 pad is.
    const bool c_as_buttons = SDL_GameControllerHasButton(game_controller, SDL_CONTROLLER_BUTTON_Y) == SDL_TRUE;
    g_pad_nso_n64 = c_as_buttons &&
                    (nso_ids || (n64 && (name != nullptr) && (SDL_strcmp(name, "Nintendo N64 Controller") == 0)));
    g_pad_r_on_trigger = n64 && !SDL_GameControllerHasButton(game_controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) &&
                         SDL_GameControllerHasAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    g_pad_misc1_photo = SDL_GameControllerHasButton(game_controller, SDL_CONTROLLER_BUTTON_MISC1) &&
                        !SDL_GameControllerHasButton(game_controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    printf("[SNAP-Input] pad layout: %s, %s\n",
           n64 ? "N64-shaped (its L and R are L and R, its Z is Z)" : "standard (the left shoulder is Z, the triggers are L and R)",
           why);
    if (g_pad_nso_n64) {
        printf("[SNAP-Input] Switch Online N64 controller: its C buttons are the C buttons (SDL's driver sends them as Y, X, Back and the right trigger)%s\n",
               n64 ? "; its ZR is the right shoulder's" : "");
    }
    if (g_pad_r_on_trigger) {
        printf("[SNAP-Input] the pad maps no right shoulder: its right trigger is its R\n");
    }
    if (g_pad_misc1_photo) {
        printf("[SNAP-Input] the pad has no right stick to press in: its Capture or Share button saves the photo\n");
    }
    fflush(stdout);
}

static void try_open_controller() {
    // Drop a handle whose device is gone, otherwise the stale pointer blocks
    // every future open and a replugged controller never comes back.
    if (game_controller != nullptr && !SDL_GameControllerGetAttached(game_controller)) {
        SDL_GameControllerClose(game_controller);
        game_controller = nullptr;
        g_deck_pad_keys_ignored.store(false, std::memory_order_relaxed);
    }

    int num_joysticks = SDL_NumJoysticks();
    // The open pad has no stick: when the list of joysticks changes and one
    // that has a stick is there, that one is the pad instead. Looked at only
    // when the count changes, since the thread polls every 2 ms.
    static int counted = -1;
    if (game_controller != nullptr) {
        if ((num_joysticks == counted) || SDL_GameControllerHasAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTX)) {
            counted = num_joysticks;
            return;
        }
        counted = num_joysticks;
        int better = -1;
        for (int i = 0; (i < num_joysticks) && (better < 0); i++) {
            if ((!g_vpad_test || SDL_JoystickIsVirtual(i)) && SDL_IsGameController(i) && joystick_has_stick(i)) {
                better = i;
            }
        }
        if (better < 0) {
            return;
        }
        const char* now = SDL_GameControllerName(game_controller);
        const char* next = SDL_GameControllerNameForIndex(better);
        printf("[SNAP-Input] %s has no stick; %s, which has one, is the pad now\n",
               now ? now : "the open pad", next ? next : "another pad");
        fflush(stdout);
        SDL_GameControllerClose(game_controller);
        game_controller = nullptr;
        g_deck_pad_keys_ignored.store(false, std::memory_order_relaxed);
    }
    counted = num_joysticks;

    // Pads with a stick first, then the rest, each in SDL's order.
    std::vector<int> order;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < num_joysticks; i++) {
            const bool stick = SDL_IsGameController(i) && joystick_has_stick(i);
            if (stick == (pass == 0)) {
                order.push_back(i);
            }
        }
    }
    // A joystick SDL cannot map is named once, not at every poll: while no
    // pad was open the line went out at every poll (36,207 times in one
    // phone's log, for its accelerometer).
    static std::vector<SDL_JoystickID> unmappedSaid;
    for (int i : order) {
        if (g_vpad_test && !SDL_JoystickIsVirtual(i)) {
            continue;   // the virtual test pad is the only pad this run opens
        }
        if (SDL_IsGameController(i)) {
            game_controller = SDL_GameControllerOpen(i);
            if (game_controller) {
                const bool gyro = SDL_GameControllerHasSensor(game_controller, SDL_SENSOR_GYRO) == SDL_TRUE;
                const char* path = nullptr;
#if SDL_VERSION_ATLEAST(2, 24, 0)
                path = SDL_GameControllerPath(game_controller);
#endif
                printf("[SNAP-Input] Opened game controller: %s (gyro: %s%s%s)\n",
                       SDL_GameControllerName(game_controller), gyro ? "yes" : "none",
                       path ? ", " : "", path ? path : "");
                // The pad's ids and SDL's mapping line, so a report of a button
                // that does the wrong thing comes with the facts (issue #26).
                {
                    char* mapping = SDL_GameControllerMapping(game_controller);
                    printf("[SNAP-Input] pad ids %04X:%04X, SDL's mapping: %s\n",
                           unsigned(SDL_GameControllerGetVendor(game_controller)), unsigned(SDL_GameControllerGetProduct(game_controller)),
                           (mapping != nullptr) ? mapping : "(none)");
                    SDL_free(mapping);
                }

                decide_pad_layout();

                const bool deck = is_steam_deck_pad(game_controller);
                g_deck_pad_keys_ignored.store(deck, std::memory_order_relaxed);
                if (deck) {
                    printf("[SNAP-Input] the Deck's own controller is attached: keyboard Return, Escape, Tab, Space and Left Shift are ignored "
                           "(Steam's desktop layout sends them for A, B, Select, Y and a paddle)\n");
                }
                // SNAP_PAD_STALL_MS: a diagnostic. Holds this thread for the
                // given time right after a pad opens, standing in for the
                // seconds SDL's HIDAPI Switch driver can spend in its
                // synchronous open and close handshakes with a Bluetooth pad
                // (issue #7). On the game's controller thread, where the
                // open used to run, 20 s of this left the boot black for
                // good; on the pad thread the game carries on and the pad
                // simply arrives late.
                if (const char* stall = getenv("SNAP_PAD_STALL_MS")) {
                    const int ms = atoi(stall);
                    if (ms > 0) {
                        printf("[SNAP-Input] SNAP_PAD_STALL_MS: holding this thread %d ms after the open\n", ms);
                        fflush(stdout);
                        SDL_Delay(Uint32(ms));
                        printf("[SNAP-Input] SNAP_PAD_STALL_MS: released\n");
                        fflush(stdout);
                    }
                }
                break;
            }
        }
        else {
            // A pad SDL has no mapping for is silent otherwise; say it was
            // seen, so a player knows why it does nothing and can hand SDL a
            // mapping through SDL_GAMECONTROLLERCONFIG (README, Controls).
            const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
            if (std::find(unmappedSaid.begin(), unmappedSaid.end(), id) == unmappedSaid.end()) {
                unmappedSaid.push_back(id);
                const char* name = SDL_JoystickNameForIndex(i);
                printf("[SNAP-Input] joystick %d (%s) has no game controller mapping; it is not used. "
                       "A line for it in gamecontrollerdb.txt, beside the game, would teach SDL this pad\n",
                       i, name ? name : "unnamed");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The Steam Deck's IMU switch. Steam's client turns the Deck's built-in gyro
// off whenever the active controller layout has Gyro set to None (the
// desktop layout does), and the controller stays that way after Steam exits.
// SDL's Steam Deck driver assumes "sensors are enabled by default" and never
// sends the setting, so a game reading the pad directly sees a live sensor
// whose every reading is exactly zero. This sends the controller the same
// setting SDL's Steam Controller driver sends (SETTING_IMU_MODE, 0x30, = raw
// gyro | raw accel, 0x0018), the way SteamDeckGyroDSU does, through SDL's own
// HID API on a second handle to the controller's interface: once when the
// gyro is turned on, and again whenever the readings stay at exactly zero,
// which is Steam putting its layout back. On a Deck (2026-09-06) the setting
// alone woke the sensor within a dozen readings; the sensor was switched off
// again about a minute later, and this is why the watch below keeps
// sending. A feature report is how SDL's driver talks to the Deck; an output
// report is the fallback.
// ---------------------------------------------------------------------------
constexpr uint16_t ValveVendorId = 0x28DE;
constexpr uint16_t SteamDeckProductId = 0x1205;

static bool is_steam_deck_pad(SDL_GameController* gc) {
    return (gc != nullptr) &&
           (SDL_GameControllerGetVendor(gc) == ValveVendorId) &&
           (SDL_GameControllerGetProduct(gc) == SteamDeckProductId);
}

// ID_SET_SETTINGS_VALUES (0x87), a length, then three bytes per setting: its
// number and its value, little-endian. (SteamDeckGyroDSU sends four more
// settings with it, the trackpad and mouse ones SDL's driver already sends;
// the one setting proved enough.)
static const unsigned char kDeckImuOn[] = {
    0x87, 0x03,
    0x30, 0x18, 0x00,   // SETTING_IMU_MODE = SEND_RAW_ACCEL | SEND_RAW_GYRO
};

// The handle to the Deck's controller, opened once and kept. A re-send then
// costs one write instead of a walk of every HID device on the system, which
// matters because this runs on the game's thread, once a second for as long
// as the readings stay dead.
static SDL_hid_device* g_deckHid = nullptr;

static SDL_hid_device* deck_hid_open() {
    if (g_deckHid != nullptr) return g_deckHid;
    static bool hidReady = false;
    if (!hidReady) {
        if (SDL_hid_init() != 0) return nullptr;
        hidReady = true;
    }
    SDL_hid_device_info* list = SDL_hid_enumerate(ValveVendorId, SteamDeckProductId);
    if (list == nullptr) return nullptr;
    // The controller's own interface is 2 (0 and 1 are the keyboard and the
    // mouse the firmware emulates); the vendor usage page marks it where the
    // number is not known.
    const char* best = nullptr;
    int bestRank = 0;
    for (SDL_hid_device_info* d = list; d != nullptr; d = d->next) {
        int rank = 1;
        if (d->usage_page == 0xFFFF) rank = 2;
        if (d->interface_number == 2) rank = 3;
        if ((d->path != nullptr) && (rank > bestRank)) {
            bestRank = rank;
            best = d->path;
        }
    }
    if (best != nullptr) g_deckHid = SDL_hid_open_path(best, 0);
    SDL_hid_free_enumeration(list);
    return g_deckHid;
}

static void deck_hid_close() {
    if (g_deckHid != nullptr) {
        SDL_hid_close(g_deckHid);
        g_deckHid = nullptr;
    }
}

// Sends the settings report to the Deck's controller. Returns what was done,
// for the log, or nullptr when nothing could be sent; a handle that no longer
// takes a write is dropped, so the next send finds the pad again.
static const char* deck_send_imu_on() {
    SDL_hid_device* dev = deck_hid_open();
    if (dev == nullptr) return nullptr;
    unsigned char buf[65];
    memset(buf, 0, sizeof(buf));
    memcpy(buf + 1, kDeckImuOn, sizeof(kDeckImuOn));
    const char* how = nullptr;
    if (SDL_hid_send_feature_report(dev, buf, sizeof(buf)) > 0) {
        how = "feature report";
    } else if (SDL_hid_write(dev, buf, sizeof(buf)) > 0) {
        how = "output report";
    }
    if (how != nullptr) {
        // A lingering report may come back after a settings change; SDL's
        // driver discards it the same way.
        unsigned char back[65];
        memset(back, 0, sizeof(back));
        SDL_hid_get_feature_report(dev, back, sizeof(back));
    } else {
        deck_hid_close();
    }
    return how;
}

// While the gyro is on: says once when the readings carry turning; when
// they stay at exactly zero for half a second (a pad lying still reads
// noise, never a run of exact zeros) says so, and on a Deck sends the IMU
// switch again, a second apart, until they carry turning again, which is
// logged too. Runs on the game's thread, once per poll.
static void gyro_watch(bool justEnabled) {
    static int64_t onAt = 0;
    static int64_t lastSend = 0;
    static int64_t deadSince = 0;
    static int deadLogs = 0;
    static bool liveLogged = false;
    static bool deadLogged = false;
    static uint32_t aliveReadings = 0;
    const int64_t t = now_us();
    if (justEnabled) {
        onAt = t;
        lastSend = 0;
        deadSince = 0;
        deadLogs = 0;
        liveLogged = false;
        deadLogged = false;
        aliveReadings = 0;
        g_gyro_readings.store(0, std::memory_order_relaxed);
        g_gyro_live_us.store(0, std::memory_order_relaxed);
        if (is_steam_deck_pad(game_controller)) {
            const char* how = deck_send_imu_on();
            printf("[SNAP-Input] gyro: the Deck's IMU-on setting %s\n",
                   how ? how : "could not be sent (no HID access to the controller)");
            fflush(stdout);
            lastSend = t;
        }
        return;
    }
    if (!g_gyro_enabled.load(std::memory_order_relaxed)) return;
    if (g_gyro_debug) {
        static int64_t said = 0;
        if (t - said >= 1000000) {
            said = t;
            float pk[3], gv[3];
            uint32_t n, clk;
            float ly, lp, dts;
            {
                std::lock_guard<std::mutex> lock(g_motion_mutex);
                pk[0] = g_dbg_peak[0]; pk[1] = g_dbg_peak[1]; pk[2] = g_dbg_peak[2];
                n = g_dbg_n;
                ly = g_dbg_last_yaw; lp = g_dbg_last_pitch;
                dts = g_dbg_dt_sum; clk = g_dbg_clock;
                gv[0] = g_dbg_grav[0]; gv[1] = g_dbg_grav[1]; gv[2] = g_dbg_grav[2];
                g_dbg_peak[0] = g_dbg_peak[1] = g_dbg_peak[2] = 0.0f;
                g_dbg_n = 0;
                g_dbg_dt_sum = 0.0f;
                g_dbg_clock = 0;
            }
            printf("[SNAP-GYRO] %u readings spanning %.3f s (%u by the sensor's own clock), "
                   "biggest rate x=%.4f y=%.4f z=%.4f rad/s; down=(%.1f %.1f %.1f); "
                   "angle handed over yaw=%.4f pitch=%.4f rad; %s\n",
                   n, dts, clk, pk[0], pk[1], pk[2], gv[0], gv[1], gv[2], ly, lp, g_dbg_stop);
            fflush(stdout);
        }
    }
    constexpr int64_t Dead = 500000;
    constexpr int64_t Resend = 1000000;
    const int64_t live = g_gyro_live_us.load(std::memory_order_relaxed);
    const uint32_t readings = g_gyro_readings.load(std::memory_order_relaxed);
    const int64_t since = (live != 0) ? live : onAt;
    if (t - since < Dead) {
        aliveReadings = readings;
        if ((live != 0) && !liveLogged) {
            printf("[SNAP-Input] gyro readings carry turning (%u readings since it was turned on)\n", readings);
            fflush(stdout);
            liveLogged = true;
        }
        if ((live != 0) && (deadSince != 0)) {
            if (deadLogs <= 5) {
                printf("[SNAP-Input] gyro readings carry turning again, after %.1f s at zero\n",
                       double(live - deadSince) * 1e-6);
                fflush(stdout);
            }
            deadSince = 0;
        }
        return;
    }
    // Dead: no reading with any turning for half a second. Only readings that
    // arrived and carried none count. With none arriving at all -- the quit
    // box holding the event loop, the pad put down and asleep -- there is
    // nothing to say and nothing to switch; the quit box used to be logged
    // as "all zero" twice, with the same count both times.
    if (readings == aliveReadings) {
        return;
    }
    if (deadSince == 0) deadSince = since;
    if (is_steam_deck_pad(game_controller)) {
        if (t - lastSend < Resend) return;
        const char* how = deck_send_imu_on();
        lastSend = t;
        deadLogs++;
        if (deadLogs <= 5) {
            printf("[SNAP-Input] gyro: %u readings, all zero for half a second; the Deck's IMU-on setting %s\n",
                   readings, how ? how : "could not be sent");
            fflush(stdout);
        }
    } else if (!deadLogged) {
        printf("[SNAP-Input] gyro: %u readings, all zero for half a second; this pad's sensor reports no turning\n",
               readings);
        fflush(stdout);
        deadLogged = true;
    }
}

// Turns the pad's gyro on when the setting asks and the pad has one, off
// otherwise; decided on every poll so the Controls page's row and a
// replugged pad both take effect at once. A pad without a gyro (every
// Xbox pad, Steam's virtual pad in the Deck's gaming mode) has nothing to
// enable, and the setting then does nothing.
static void sync_gyro() {
    static bool enabledFor = false;
    // The pad is named by its joystick instance id, which SDL never reuses
    // within a run. The address of the handle is not usable for this: it is
    // closed when a pad goes away, and the allocator can hand the same
    // address back for the next one, which would leave the gyro switched off
    // on a pad the port believed it had already seen.
    static SDL_JoystickID padId = -1;
    const bool has = (game_controller != nullptr) &&
                     (SDL_GameControllerHasSensor(game_controller, SDL_SENSOR_GYRO) == SDL_TRUE);
    const bool want = has && (settings().gyro_aim != 0);
    const SDL_JoystickID nowId =
        (game_controller != nullptr)
            ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(game_controller))
            : -1;
    if ((padId == nowId) && (enabledFor == want)) {
        gyro_watch(false);
        return;
    }
    padId = nowId;
    enabledFor = want;
    if (!has) {
        g_gyro_enabled.store(false, std::memory_order_relaxed);
        deck_hid_close();
        return;
    }
    if (SDL_GameControllerSetSensorEnabled(game_controller, SDL_SENSOR_GYRO, want ? SDL_TRUE : SDL_FALSE) != 0) {
        printf("[SNAP-Input] gyro %s failed: %s\n", want ? "on" : "off", SDL_GetError());
        fflush(stdout);
        g_gyro_enabled.store(false, std::memory_order_relaxed);
        return;
    }
    // The accelerometer says which way is down, which is what makes a turn a
    // turn whatever angle the pad is held at. A pad without one still aims;
    // the yaw simply falls back to the pad's own up axis.
    if (SDL_GameControllerHasSensor(game_controller, SDL_SENSOR_ACCEL) == SDL_TRUE) {
        SDL_GameControllerSetSensorEnabled(game_controller, SDL_SENSOR_ACCEL, want ? SDL_TRUE : SDL_FALSE);
    }
    g_grav[0] = g_grav[1] = g_grav[2] = 0.0f;
    g_gyro_rate_hz = SDL_GameControllerGetSensorDataRate(game_controller, SDL_SENSOR_GYRO);
    g_gyro_last_us = 0;
    g_gyro_instance.store(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(game_controller)), std::memory_order_relaxed);
    g_gyro_enabled.store(want, std::memory_order_relaxed);
    printf("[SNAP-Input] gyro %s (%s, %.0f Hz)\n",
           want ? "on: the pad's turning aims the camera" : "off",
           SDL_GameControllerName(game_controller), g_gyro_rate_hz);
    fflush(stdout);
    if (want) {
        gyro_watch(true);
    } else {
        deck_hid_close();
    }
}

// ---------------------------------------------------------------------------
// The pad thread
// ---------------------------------------------------------------------------

// Rumble as the game asks for it, through the runtime's motor callback on a
// game thread: -1 nothing asked yet, 0 stop, 1 start. The pad thread sends
// the change to SDL. The cartridge never asks (its only motor calls are in
// the reset handler, which the port never runs), so this is the runtime's
// callback kept complete, not a feature.
static std::atomic<int> g_rumble_wanted{-1};

static void pad_close() {
    if (game_controller != nullptr) {
        SDL_GameControllerClose(game_controller);
        game_controller = nullptr;
    }
    g_deck_pad_keys_ignored.store(false, std::memory_order_relaxed);
}

static void pad_apply_rumble() {
    static int applied = 0;
    const int want = g_rumble_wanted.load(std::memory_order_relaxed);
    if ((want < 0) || (want == applied) || (game_controller == nullptr)) {
        return;
    }
    applied = want;
    // On or off, as the Rumble Pak is; SDL caps one start at
    // SDL_MAX_RUMBLE_DURATION_MS (0xFFFF), about 65 seconds.
    const Uint16 mag = want ? 0xFFFF : 0;
    SDL_GameControllerRumble(game_controller, mag, mag, want ? 0xFFFF : 0);
}

// The pad's state, as SDL holds it after this poll, for the game's threads.
static void pad_publish() {
    PadSnapshot s{};
    if ((game_controller != nullptr) && SDL_GameControllerGetAttached(game_controller)) {
        s.attached = true;
        s.n64_layout = g_pad_layout_n64;
        s.nso_n64 = g_pad_nso_n64;
        s.r_on_trigger = g_pad_r_on_trigger;
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            if (SDL_GameControllerGetButton(game_controller, SDL_GameControllerButton(b)) != 0) {
                s.buttons |= (1u << b);
            }
        }
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) {
            s.axes[a] = SDL_GameControllerGetAxis(game_controller, SDL_GameControllerAxis(a));
        }
    }
    std::lock_guard<std::mutex> lock(g_pad_mutex);
    g_pad = s;
}

static std::thread* g_pad_thread = nullptr;
static std::atomic<bool> g_pad_thread_quit{false};

// Polls SDL for the controllers (SDL_GameControllerUpdate: device arrivals
// and departures, every driver's reads, the events they raise), opens the
// first pad it finds and closes a departed one, keeps the gyro in step with
// the settings, sends rumble, and publishes a snapshot -- every two
// milliseconds, so the game's next reading is at most that stale, which is
// no more than the window's pump gave it before. A driver that blocks holds
// this thread and nothing else: the game keeps running and the window keeps
// answering, and the pad turns up when the driver is done.
static void pad_thread_main() {
    ultramodern::set_native_thread_name("Pad Thread");
#if defined(__APPLE__)
    // Started here and not in SDL_Init on the main thread (main.cpp,
    // create_gfx): SDL's IOKit driver schedules its device matching on the
    // run loop of the thread that initialized the joystick subsystem and
    // services that loop from SDL_GameControllerUpdate, which runs on this
    // thread; on any other thread no pad on that driver is ever seen.
    // Stopped at the end of this function for the same reason.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        printf("[SNAP-Input] SDL_InitSubSystem(GAMECONTROLLER) failed: %s; no controller this run\n", SDL_GetError());
        fflush(stdout);
        pad_publish();
        return;
    }
#endif
    // Any extra mappings first, so a pad this file teaches SDL about is
    // recognized by the first open rather than on some later poll.
    load_controller_mappings();
    if (g_vpad_test) {
        SDL_VirtualJoystickDesc desc;
        SDL_zero(desc);
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        desc.naxes = SDL_CONTROLLER_AXIS_MAX;
        desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
        // SNAP_VPAD_NAME and SNAP_VPAD_IDS=vvvv:pppp dress the pad as another
        // (the Switch Online N64 controller: "Nintendo N64 Controller",
        // 057E:2019), so the suite can exercise what the port does for it.
        const char* vname = getenv("SNAP_VPAD_NAME");
        desc.name = ((vname != nullptr) && (*vname != '\0')) ? vname : "Snap64 virtual test pad";
        if (const char* ids = getenv("SNAP_VPAD_IDS")) {
            unsigned v = 0, p = 0;
            if (sscanf(ids, "%x:%x", &v, &p) == 2) {
                desc.vendor_id = Uint16(v);
                desc.product_id = Uint16(p);
            }
        }
        const int idx = SDL_JoystickAttachVirtualEx(&desc);
        g_vpad = (idx >= 0) ? SDL_JoystickOpen(idx) : nullptr;
        printf("[SNAP-Input] SNAP_VPAD_TEST: virtual pad %s\n",
               (g_vpad != nullptr) ? "attached; it is the pad this run opens, any real one is left alone" : "could not be attached");
        fflush(stdout);
    }
    // Registered once, for the life of the process; the watch runs on the
    // thread that queues the event, which is this one.
    SDL_AddEventWatch(photo_button_watch, nullptr);
    bool disabledSaid = false;
    while (!g_pad_thread_quit.load(std::memory_order_relaxed)) {
        SDL_GameControllerUpdate();
        if (!settings().pad_enabled) {
            // Ignored, not merely unread: nothing is opened, so a driver
            // that misbehaves at open (issue #7) is never entered.
            pad_close();
            if (!disabledSaid) {
                disabledSaid = true;
                printf("[SNAP-Input] pad_enabled is false: controllers are ignored\n");
                fflush(stdout);
            }
        } else {
            disabledSaid = false;
            // Pick up a newly connected controller, and replace a detached one.
            try_open_controller();
            // The Controls page's Pad Preset row, in force at once.
            if ((game_controller != nullptr) && (settings().pad_layout != g_pad_layout_applied)) {
                decide_pad_layout();
            }
            pad_apply_rumble();
        }
        sync_gyro();
        pad_publish();
        SDL_Delay(2);
    }
    pad_close();
    sync_gyro();
    pad_publish();
#if defined(__APPLE__)
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
#endif
}

void input_start_pad_thread() {
    if (g_pad_thread != nullptr) {
        return;
    }
    g_pad_thread_quit.store(false, std::memory_order_relaxed);
    g_pad_thread = new std::thread(pad_thread_main);
}

void input_stop_pad_thread() {
    if (g_pad_thread == nullptr) {
        return;
    }
    g_pad_thread_quit.store(true, std::memory_order_relaxed);
    g_pad_thread->join();
    delete g_pad_thread;
    g_pad_thread = nullptr;
}

// ---------------------------------------------------------------------------
// Public API (matches ultramodern::input::callbacks_t)
// ---------------------------------------------------------------------------

// The runtime's poll, at every osContStartReadData, on the game's controller
// thread. Nothing to do here any more: the pad thread polls SDL on its own
// clock and the reading below takes its snapshot. Kept so the callback
// table stays complete.
void input_poll() {
}


// Records or replays every controller reading the game is handed.
//
// The point is autonomy. Every test so far has needed a person on the stick,
// because the attract demo never reaches the content under test -- it is the
// game's own recorded-input playback, only of a ride nobody chose. This is the
// same idea pointed at the whole game, the way TAS input movies work: play a
// course once with SNAP_RECORD set and every reading is written down; run with
// SNAP_REPLAY and the file is handed back reading by reading, no hands needed.
// The ride is on rails, so replay keeps to the course even where the game's
// own randomness drifts.
//
// The tap sits at the single point every input reaches the game through, after
// all mapping and dead zones, so a recording is exactly what the game
// experienced and a replay needs no controller at all.
static void snap_input_tap(uint16_t* buttons, float* x, float* y) {
    static FILE* record = nullptr;
    static FILE* replay = nullptr;
    static bool opened = false;
    // Presented-frame capture on a schedule. A visual fault can only be judged
    // from the images that actually reached the screen, and a replay passes any
    // given moment exactly once -- so the camera has to already be armed when
    // the moment arrives. Readings are the schedule's clock: SNAP_PCAP_EVERY=N
    // arms a burst every N readings (scouting an unknown ride), SNAP_PCAP_AT=
    // a,b,c arms at exact readings (returning to a moment scouting found), and
    // SNAP_PCAP_BURST says how many consecutive presents each burst photographs.
    static uint32_t pcapEvery = 0;
    static uint32_t pcapStart = 0;
    static uint32_t pcapBurst = 24;
    static uint32_t pcapAt[64] = {};
    static uint32_t pcapAtCount = 0;
    static uint32_t readingIndex = 0;
    if (!opened) {
        opened = true;
        const char* replayPath = getenv("SNAP_REPLAY");
        const char* recordPath = getenv("SNAP_RECORD");
        if (replayPath != nullptr) {
            replay = fopen(replayPath, "rb");
            printf("[SNAP-INPUT] replaying inputs from %s: %s\n",
                   replayPath, replay ? "open" : "FAILED");
        }
        else if (recordPath != nullptr) {
            record = fopen(recordPath, "wb");
            printf("[SNAP-INPUT] recording inputs to %s: %s\n",
                   recordPath, record ? "open" : "FAILED");
        }
        const char* everyEnv = getenv("SNAP_PCAP_EVERY");
        if (everyEnv != nullptr) {
            pcapEvery = uint32_t(strtoul(everyEnv, nullptr, 10));
        }
        const char* startEnv = getenv("SNAP_PCAP_START");
        if (startEnv != nullptr) {
            pcapStart = uint32_t(strtoul(startEnv, nullptr, 10));
        }
        const char* burstEnv = getenv("SNAP_PCAP_BURST");
        if (burstEnv != nullptr) {
            pcapBurst = uint32_t(strtoul(burstEnv, nullptr, 10));
        }
        const char* atEnv = getenv("SNAP_PCAP_AT");
        if (atEnv != nullptr) {
            const char* cursor = atEnv;
            while ((*cursor != '\0') && (pcapAtCount < 64)) {
                char* after = nullptr;
                const unsigned long value = strtoul(cursor, &after, 10);
                if (after == cursor) {
                    break;
                }
                pcapAt[pcapAtCount++] = uint32_t(value);
                cursor = (*after == ',') ? (after + 1) : after;
            }
        }
        if ((pcapEvery > 0) || (pcapAtCount > 0)) {
            printf("[SNAP-PCAP] schedule: every %u readings from %u, at %u fixed readings, %u presents per burst\n",
                   pcapEvery, pcapStart, pcapAtCount, pcapBurst);
        }
        fflush(stdout);
    }

    readingIndex++;
    // The photo export's clock, and under SNAP_STATS with
    // SNAP_PHOTO_AUTOEXPORT the hands-free save that lets a replay prove the
    // export. Readings are the right clock for it for the same reason they
    // are the capture schedule's: a rendered photo is complete before the
    // reading after it begins.
    photo_export_on_reading(g_rdram);
    bool armCapture = false;
    if ((pcapEvery > 0) && (readingIndex >= pcapStart) &&
        (((readingIndex - pcapStart) % pcapEvery) == 0)) {
        armCapture = true;
    }
    for (uint32_t i = 0; i < pcapAtCount; i++) {
        if (pcapAt[i] == readingIndex) {
            armCapture = true;
        }
    }
    {
        static std::vector<uint32_t> atFrames = [] {
            std::vector<uint32_t> v;
            if (const char* e = getenv("SNAP_PCAP_ATFRAME")) {
                const char* c = e;
                while (*c != 0) {
                    char* after = nullptr;
                    const unsigned long value = strtoul(c, &after, 10);
                    if (after == c) break;
                    v.push_back(uint32_t(value));
                    c = (*after == ',') ? (after + 1) : after;
                }
            }
            return v;
        }();
        static size_t atFrameNext = 0;
        const uint32_t frameNow = snapdiag::gameFrameCounter().load(std::memory_order_relaxed);
        if ((atFrameNext < atFrames.size()) && (frameNow >= atFrames[atFrameNext])) {
            atFrameNext++;
            armCapture = true;
        }
    }
    if (armCapture) {
        snap_frame_dump_pending.store(int32_t(pcapBurst));
        printf("[SNAP-PCAP] armed %u presents at reading %u\n", pcapBurst, readingIndex);
        fflush(stdout);
    }
    // SNAP_MENU_AT=a,b,c presses the menu key at those readings, so a replay
    // can open the pages from anywhere: a replay carries N64 buttons only.
    {
        static std::vector<uint32_t> menuAt = [] {
            std::vector<uint32_t> v;
            if (const char* e = getenv("SNAP_MENU_AT")) {
                const char* c = e;
                while (*c != 0) {
                    char* after = nullptr;
                    const unsigned long value = strtoul(c, &after, 10);
                    if (after == c) break;
                    v.push_back(uint32_t(value));
                    c = (*after == ',') ? (after + 1) : after;
                }
            }
            return v;
        }();
        for (uint32_t at : menuAt) {
            if (at == readingIndex) {
                if (menu_pages_open()) {
                    input_tap_start();
                } else {
                    input_request_menu();
                }
                printf("[SNAP-MENU] the menu key at reading %u\n", readingIndex);
                fflush(stdout);
            }
        }
    }

    // SNAP_MOUSE_TEST=reading:x,y[:click|back|up|down|press|release];... puts the pointer
    // at x,y of the game's 320 by 240 at that reading -- a move -- and, when
    // named, clicks, right-clicks or turns the wheel there, so a replay can
    // prove the menus' mouse without a hand on it.
    {
        struct MouseStep { uint32_t at; int32_t x; int32_t y; char what; };
        static std::vector<MouseStep> steps = [] {
            std::vector<MouseStep> v;
            if (const char* e = getenv("SNAP_MOUSE_TEST")) {
                std::string all(e);
                size_t start = 0;
                while (start < all.size()) {
                    size_t end = all.find(';', start);
                    if (end == std::string::npos) end = all.size();
                    const std::string one = all.substr(start, end - start);
                    MouseStep m{0, -1, -1, 0};
                    char what[16] = {};
                    const int n = sscanf(one.c_str(), "%u:%d,%d:%15s", &m.at, &m.x, &m.y, what);
                    if (n >= 3) {
                        m.what = what[0];
                        v.push_back(m);
                    }
                    start = end + 1;
                }
            }
            return v;
        }();
        for (const MouseStep& m : steps) {
            if (m.at == readingIndex) {
                g_test_gx.store(m.x, std::memory_order_relaxed);
                g_test_gy.store(m.y, std::memory_order_relaxed);
                g_ptr_moved.fetch_add(1, std::memory_order_relaxed);
                if ((m.what == 'c') || (m.what == 'p')) g_ptr_click.fetch_add(1, std::memory_order_relaxed);
                if (m.what == 'p') g_test_held.store(true, std::memory_order_relaxed);
                if (m.what == 'r') g_test_held.store(false, std::memory_order_relaxed);
                if (m.what == 'b') g_ptr_back.fetch_add(1, std::memory_order_relaxed);
                if (m.what == 'u') g_ptr_wheel_up.fetch_add(1, std::memory_order_relaxed);
                if (m.what == 'd') g_ptr_wheel_down.fetch_add(1, std::memory_order_relaxed);
                if (m.what != 0) g_last_device.store(kBindMouse, std::memory_order_relaxed);
                printf("[SNAP-MOUSE] test at reading %u: the pointer at %d,%d%s%s\n", readingIndex, m.x, m.y,
                       m.what ? ", " : "", (m.what == 'c') ? "click" : (m.what == 'b') ? "right-click"
                                           : (m.what == 'u') ? "wheel up" : (m.what == 'd') ? "wheel down"
                                           : (m.what == 'p') ? "press" : (m.what == 'r') ? "release" : "");
                fflush(stdout);
            }
        }
    }

    // SNAP_DEVICE_TEST=reading:name;... pushes SDL's own press and release of
    // a key or a pad button, named as the binding table names them ("X",
    // "Pad A"), at that reading: the window's thread takes it as a press of
    // that device (input_last_device) the way it takes a hand's. The game does
    // not see it -- SDL's key and pad state are not changed -- so a replay
    // keeps its own buttons. A mouse button ("Mouse Left") is pushed the same
    // way and let go twenty readings on; a trigger ("Pad RightTrigger") is
    // held on the virtual pad (SNAP_VPAD_TEST) for thirty readings, so a
    // click inside that time is a trigger's click on a Deck.
    {
        struct DeviceStep { uint32_t at; std::string name; };
        static std::vector<DeviceStep> steps = [] {
            std::vector<DeviceStep> v;
            if (const char* e = getenv("SNAP_DEVICE_TEST")) {
                std::string all(e);
                size_t start = 0;
                while (start < all.size()) {
                    size_t end = all.find(';', start);
                    if (end == std::string::npos) end = all.size();
                    const std::string one = all.substr(start, end - start);
                    const size_t colon = one.find(':');
                    if (colon != std::string::npos) {
                        v.push_back({uint32_t(strtoul(one.c_str(), nullptr, 10)), one.substr(colon + 1)});
                    }
                    start = end + 1;
                }
            }
            return v;
        }();
        for (const DeviceStep& d : steps) {
            // A sixth of a second on: the device the help lines now name, and
            // the mouse buttons the game's inputs see held.
            if (d.at + 10 == readingIndex) {
                const int now = g_last_device.load(std::memory_order_relaxed);
                printf("[SNAP-Input] device test at reading %u: the help lines name the %s; mouse buttons held 0x%02x\n",
                       readingIndex, device_word(now), unsigned(g_mouse_held.load(std::memory_order_relaxed)));
                fflush(stdout);
            }
            Source src{};
            const bool known = resolve_source(d.name, src);
            if (known && (src.kind == SourceKind::MouseButton) && (d.at + 20 == readingIndex)) {
                SDL_Event up{};
                up.type = SDL_MOUSEBUTTONUP;
                up.button.button = uint8_t(src.code);
                up.button.state = SDL_RELEASED;
                SDL_PushEvent(&up);
            }
            // A held trigger is set again at every reading of its hold: one
            // setting alone was sometimes not seen by the pad's next reading.
            // Its rest is the axis's low end: SDL reads the virtual pad's
            // full-range axis at 0 as a trigger halfway down.
            if (known && (src.kind == SourceKind::PadAxis) && (g_vpad != nullptr) && (readingIndex > d.at) &&
                (readingIndex <= d.at + 30)) {
                SDL_JoystickSetVirtualAxis(g_vpad, src.code,
                                           (readingIndex < d.at + 30) ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
            }
            if (d.at != readingIndex) {
                continue;
            }
            if (known && (src.kind == SourceKind::MouseButton)) {
                SDL_Event down{};
                down.type = SDL_MOUSEBUTTONDOWN;
                down.button.button = uint8_t(src.code);
                down.button.state = SDL_PRESSED;
                down.button.clicks = 1;
                SDL_PushEvent(&down);
                const PadSnapshot pad = pad_snapshot();
                printf("[SNAP-Input] device test at reading %u: a press of \"%s\" (the pad's triggers: left %d, right %d)\n",
                       readingIndex, d.name.c_str(), int(pad.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT]),
                       int(pad.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]));
                fflush(stdout);
                continue;
            }
            if (known && (src.kind == SourceKind::PadAxis) && (g_vpad != nullptr)) {
                SDL_JoystickSetVirtualAxis(g_vpad, src.code, 32767);
                printf("[SNAP-Input] device test at reading %u: \"%s\" held on the virtual pad\n", readingIndex,
                       d.name.c_str());
                fflush(stdout);
                continue;
            }
            if (!known || ((src.kind != SourceKind::Key) && (src.kind != SourceKind::PadButton))) {
                printf("[SNAP-Input] device test at reading %u: \"%s\" is no key, pad button, mouse button or trigger\n",
                       readingIndex, d.name.c_str());
                fflush(stdout);
                continue;
            }
            SDL_Event down{};
            SDL_Event up{};
            if (src.kind == SourceKind::Key) {
                down.type = SDL_KEYDOWN;
                up.type = SDL_KEYUP;
                down.key.keysym.scancode = up.key.keysym.scancode = SDL_Scancode(src.code);
            } else {
                down.type = SDL_CONTROLLERBUTTONDOWN;
                up.type = SDL_CONTROLLERBUTTONUP;
                down.cbutton.button = up.cbutton.button = uint8_t(src.code);
            }
            SDL_PushEvent(&down);
            SDL_PushEvent(&up);
            printf("[SNAP-Input] device test at reading %u: a press of \"%s\"\n", readingIndex, d.name.c_str());
            fflush(stdout);
        }
    }

    if (replay != nullptr) {
        struct { uint16_t btn; float rx; float ry; } r;
        if (fread(&r, sizeof(r), 1, replay) == 1) {
            *buttons = r.btn;
            *x = r.rx;
            *y = r.ry;
        }
        else {
            // The recording ran out: hold neutral rather than repeat the tail.
            *buttons = 0;
            *x = 0.0f;
            *y = 0.0f;
        }
        // The menu key's Start reaches a replay too (SNAP_MENU_AT, or Esc
        // during one): it is the host's, not the tape's.
        if (now_us() < g_esc_start_until.load(std::memory_order_relaxed)) {
            *buttons |= N64_BTN_START;
        }
    }
    else if (record != nullptr) {
        struct { uint16_t btn; float rx; float ry; } r{ *buttons, *x, *y };
        fwrite(&r, sizeof(r), 1, record);
        // Flushed per reading: a recording exists to capture the moments
        // before a crash, and a crash loses everything still in the stdio
        // buffer -- measured: a session died with its whole menu navigation
        // in the unwritten tail, which was the very part under study.
        fflush(record);
    }

    // SNAP_PCAP_ONCOURSE: a burst of presents every sixty readings for the
    // first forty seconds after the course code becomes resident -- the
    // course's opening cinematic and the tutorial, photographed with no
    // reading count to know. For the Deck's top-left block in Widescreen
    // (2026-09-12).
    {
        static const bool pcapOnCourse = (getenv("SNAP_PCAP_ONCOURSE") != nullptr);
        if (pcapOnCourse) {
            static bool courseWas = false;
            static uint32_t courseStart = 0;
            const bool courseNow = g_app_level_resident.load(std::memory_order_relaxed);
            if (courseNow && !courseWas) {
                courseStart = readingIndex;
            }
            courseWas = courseNow;
            if (courseNow && ((readingIndex - courseStart) < 2400) && (((readingIndex - courseStart) % 60) == 0)) {
                snap_frame_dump_pending.store(int32_t(pcapBurst));
                printf("[SNAP-PCAP] armed %u presents at reading %u: %u readings into the course\n",
                       pcapBurst, readingIndex, readingIndex - courseStart);
                fflush(stdout);
            }
        }
    }

    // SNAP_PCAP_ONZOOM: a burst of presents every time Z changes state in a
    // course, the viewfinder going up or coming down. For the moment a
    // player can name but no reading count can: the Deck's top-left flash
    // at the Beach tutorial's Z press (2026-09-06). Read from the final
    // buttons, replay included.
    {
        static const bool pcapOnZoom = (getenv("SNAP_PCAP_ONZOOM") != nullptr);
        if (pcapOnZoom) {
            static bool zoomWas = false;
            const bool zoomNow = (*buttons & 0x2000) != 0;
            if ((zoomNow != zoomWas) && g_app_level_resident.load(std::memory_order_relaxed)) {
                snap_frame_dump_pending.store(int32_t(pcapBurst));
                printf("[SNAP-PCAP] armed %u presents at reading %u: Z %s\n",
                       pcapBurst, readingIndex, zoomNow ? "pressed" : "released");
                fflush(stdout);
            }
            zoomWas = zoomNow;
        }
    }
}

bool input_get(int controller_num, uint16_t* buttons, float* x, float* y) {
    vpad_tick();
    // Port 4 is the Snap Station when it is present: a controller nobody
    // holds, so its pad reads succeed with nothing pressed (snap_station.h).
    if (controller_num == 3 && snap::station_port4_present()) {
        *buttons = 0;
        *x = 0.0f;
        *y = 0.0f;
        return true;
    }
    // Only support controller port 0.
    if (controller_num != 0) {
        return false;
    }

    uint16_t btn = 0;
    float ax = 0.0f;
    float ay = 0.0f;

    // -----------------------------------------------------------------------
    // Keyboard and mouse buttons, through the binding table
    // -----------------------------------------------------------------------
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    // No key reaches the game while a text is typed, nor after it until every
    // key is up (the Enter that kept it would otherwise be a fresh Start).
    static const uint8_t kNoKeys[SDL_NUM_SCANCODES] = {};
    if (g_text_active.load(std::memory_order_relaxed)) {
        keys = kNoKeys;
    }
    // The photographer card takes the keys that type (type_update follows
    // its claim); the others keep their bindings.
    type_update(g_rdram);
    static uint8_t typingKeys[SDL_NUM_SCANCODES];
    if (g_typing.load(std::memory_order_relaxed) && (keys != kNoKeys)) {
        std::memcpy(typingKeys, keys, sizeof typingKeys);
        for (int k = 0; k < SDL_NUM_SCANCODES; k++) {
            if (typing_key(k)) {
                typingKeys[k] = 0;
            }
        }
        keys = typingKeys;
    }
    if ((keys != kNoKeys) && g_text_hold_keys.load(std::memory_order_relaxed)) {
        bool anyDown = false;
        for (int k = 0; (k < SDL_NUM_SCANCODES) && !anyDown; k++) {
            anyDown = keys[k] != 0;
        }
        if (anyDown) {
            keys = kNoKeys;
        } else {
            g_text_hold_keys.store(false, std::memory_order_relaxed);
        }
    }
    // The pad as the pad thread last saw it; the only view of it this thread
    // ever takes.
    const PadSnapshot pad = pad_snapshot();
    {
        std::shared_ptr<const Resolved> table = resolved();
        if (table == nullptr) {
            input_set_bindings(defaults());
            table = resolved();
        }
        const int64_t t = now_us();
        const uint32_t held = g_mouse_held.load(std::memory_order_relaxed);
        if (t < g_esc_start_until.load(std::memory_order_relaxed)) {
            btn |= N64_BTN_START;
        }
        bool claimed = false;
        if (g_rdram != nullptr) {
            const uint8_t claim = read_u8(g_rdram, ADDR_MailboxMouseClaim);
            if (claim != 0) {
                claimed = true;
                write_u8(g_rdram, ADDR_MailboxMouseClaim, uint8_t(claim - 1));
            }
        }
        g_menu_claimed.store(claimed, std::memory_order_relaxed);
        // Outside a course the right button, while it is Z (the zoom it is
        // in a ride), is B: back, as it is on the menus that take the mouse,
        // on the screens that do not -- a Yes or No of Oak's photo check, a
        // text's question. A right button bound to anything else is that.
        const bool course = g_app_level_resident.load(std::memory_order_relaxed);
        bool rightAsB = false;
        for (int i = 0; i < IN_COUNT; i++) {
            bool down = false;
            for (const Source& src : table->sources[i]) {
                if (!course && (i == IN_Z) && (src.kind == SourceKind::MouseButton) &&
                    (src.code == SDL_BUTTON_RIGHT)) {
                    rightAsB = rightAsB || source_down(src, keys, held, t, pad, !claimed);
                    continue;
                }
                if (source_down(src, keys, held, t, pad, !claimed)) { down = true; break; }
            }
            if (rightAsB && (i == IN_Z)) {
                btn |= N64_BTN_B;
            }
            if (!down) continue;
            if (i <= IN_CR) {
                btn |= kInputBits[i];
            } else if (i == IN_STICK_UP) {
                ay += 1.0f;
            } else if (i == IN_STICK_DOWN) {
                ay -= 1.0f;
            } else if (i == IN_STICK_LEFT) {
                ax -= 1.0f;
            } else if (i == IN_STICK_RIGHT) {
                ax += 1.0f;
            }
        }
        // The fast-forward and slow-motion keys, read here beside the
        // inputs so they follow the same table, focus and pad snapshot;
        // src/fast_forward.cpp applies them on the main thread.
        bool fast = false;
        for (const Source& src : table->fast_forward) {
            if (source_down(src, keys, held, t, pad, !claimed)) { fast = true; break; }
        }
        g_fast_forward_held.store(fast, std::memory_order_relaxed);
        bool slow = false;
        for (const Source& src : table->slow_motion) {
            if (source_down(src, keys, held, t, pad, !claimed)) { slow = true; break; }
        }
        g_slow_motion_held.store(slow, std::memory_order_relaxed);
    }
    // Android's touch controls: the buttons the overlay holds.
    btn |= touch_controls_buttons();

    // -----------------------------------------------------------------------
    // Mouse look, while captured (a course, focused): the view itself moves,
    // in the game's memory; the stick is untouched. Under SNAP_REPLAY the
    // mouse is left out, so a replay is the recording and nothing else.
    // -----------------------------------------------------------------------
    if (g_captured.load(std::memory_order_relaxed) || g_gyro_enabled.load(std::memory_order_relaxed) ||
        touch_controls_aiming()) {
        static const bool replaying = (getenv("SNAP_REPLAY") != nullptr);
        if (!replaying) {
            apply_mouse_look(g_rdram);
        }
    }

    // -----------------------------------------------------------------------
    // Game controller input (overrides keyboard if connected)
    // -----------------------------------------------------------------------
    if (pad.attached) {
        // The pad's buttons and triggers went through the binding table above,
        // beside the keyboard's: their defaults are the mapping that used to
        // be written out here, and the settings file can now say otherwise.
        // What stays is the analog: a stick is a direction and a magnitude,
        // not a button, and neither of the two below is expressible as one.

        // Which stick is which: the left aims and the right works the C
        // buttons unless the Controls page's Pad Sticks row swapped them.
        const bool swapped = settings().pad_sticks_swapped;

        // The C stick → C buttons (threshold-based)
        int16_t rx = snapshot_axis(pad, swapped ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_RIGHTX);
        int16_t ry = snapshot_axis(pad, swapped ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_RIGHTY);
        constexpr int16_t C_THRESHOLD = 16000;
        if (ry < -C_THRESHOLD) btn |= N64_BTN_CU;
        if (ry >  C_THRESHOLD) btn |= N64_BTN_CD;
        if (rx < -C_THRESHOLD) btn |= N64_BTN_CL;
        if (rx >  C_THRESHOLD) btn |= N64_BTN_CR;
        // The Switch Online N64 controller through SDL's own driver: no right
        // stick; its C buttons are Y, X, Back and the right trigger, and are
        // the C buttons here (g_pad_nso_n64; the tables' sources on them yield).
        if (pad.nso_n64) {
            if (snapshot_button(pad, SDL_CONTROLLER_BUTTON_Y)) btn |= N64_BTN_CU;
            if (snapshot_axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000) btn |= N64_BTN_CD;
            if (snapshot_button(pad, SDL_CONTROLLER_BUTTON_X)) btn |= N64_BTN_CL;
            if (snapshot_button(pad, SDL_CONTROLLER_BUTTON_BACK)) btn |= N64_BTN_CR;
        }

        // The aiming stick → analog
        int16_t lx = snapshot_axis(pad, swapped ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ly = snapshot_axis(pad, swapped ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY);

        // Normalize to -1.0..1.0 range.
        float gc_x = static_cast<float>(lx) / 32767.0f;
        float gc_y = static_cast<float>(-ly) / 32767.0f; // Invert Y (SDL Y+ is down)

        // The dead zone exists for the pad, not for the game: a real N64
        // stick has none, and this only covers the rest an analog stick on a
        // modern controller does not quite return to.
        //
        // Taken as a distance from center and then rescaled, so the first
        // usable position is the smallest movement rather than a jump.
        // Applying it to each axis separately, and passing the raw value
        // through once it was crossed, did two things wrong. The output
        // stepped straight from nothing to fifteen percent of full deflection,
        // and since this game divides the stick byte by eighty and uses the
        // quotient unclamped as a rate, the first perceptible nudge commanded
        // fifteen percent of full turn, pitch and reticle speed. And a
        // diagonal whose smaller axis sat under the threshold lost that axis
        // completely, so fine aim on the diagonal did not exist -- a square
        // gate on a round stick, in a game that is entirely aiming.
        //
        // Its size is the Controls page's Dead Zone row (pad_deadzone),
        // fifteen percent as shipped, up to forty for a pad that drifts.
        const float DEADZONE = float(std::clamp(settings().pad_deadzone, 0, 40)) / 100.0f;
        const float rawMagnitude = std::sqrt((gc_x * gc_x) + (gc_y * gc_y));
        if (rawMagnitude > DEADZONE) {
            const float scaled = (rawMagnitude - DEADZONE) / (1.0f - DEADZONE);
            const float rescale = std::fmin(scaled, 1.0f) / rawMagnitude;
            ax = gc_x * rescale;
            ay = gc_y * rescale;
        }
    }

    // The D-pad moves the stick as well, while the stick itself is at rest.
    // The cartridge never reads the D-pad -- only its crash screen does --
    // so every menu answers to the stick alone, and a player with a pad in
    // hand expects the D-pad to walk the lab, the title and the Options
    // pages. Whatever is bound to the D-pad counts, the arrow keys included;
    // the D-pad's own bits still go to the game, which ignores them. In a
    // course this turns the view at full deflection, as W A S D do.
    if ((std::fabs(ax) < 0.15f) && (std::fabs(ay) < 0.15f)) {
        float dx = 0.0f;
        float dy = 0.0f;
        if (btn & N64_BTN_DU) dy += 1.0f;
        if (btn & N64_BTN_DD) dy -= 1.0f;
        if (btn & N64_BTN_DL) dx -= 1.0f;
        if (btn & N64_BTN_DR) dx += 1.0f;
        if ((dx != 0.0f) || (dy != 0.0f)) {
            ax = dx;
            ay = dy;
        }
    }

    // Clamp analog values.
    ax = std::fmax(-1.0f, std::fmin(1.0f, ax));
    ay = std::fmax(-1.0f, std::fmin(1.0f, ay));

    // Hold the stick to what a real one reports. The runtime hands the game
    // (int8_t)(127 * x) at full deflection, and this game divides by exactly
    // eighty -- StickXValue = gContInputStickX / 80.0 -- then uses the
    // quotient unclamped as a rate. So full deflection arrived as 1.5875
    // instead of 1.0 and every analog rate in the game ran fifty-nine
    // percent fast: how quickly the view turns, how quickly it pitches, how
    // quickly the reticle moves. The controls simply were not the ones the
    // game was tuned for.
    //
    // Limited as a vector rather than per axis. The stick moves in a round
    // gate and cannot reach full deflection on both axes at once, so
    // clamping them independently reports a diagonal no controller can
    // produce -- and a diagonal is where the error was largest.
    //
    // The half unit absorbs the runtime's truncation to int8_t: 127 times
    // 80/127 lands a hair under eighty in float and would arrive as 79.
    constexpr float StickFullDeflection = 80.5f / 127.0f;
    const float stickMagnitude = std::sqrt((ax * ax) + (ay * ay));
    if (stickMagnitude > 1.0f) {
        ax /= stickMagnitude;
        ay /= stickMagnitude;
    }

    ax *= StickFullDeflection;
    ay *= StickFullDeflection;

    // The Button Setup page's capture: the press it is waiting for must not
    // reach the game as the button it used to be, and once it has landed
    // the key may still be down as the button it now is. Nothing goes
    // through until the capture ends and everything is released.
    if (g_capture_active.load(std::memory_order_relaxed)) {
        btn = 0;
        ax = 0.0f;
        ay = 0.0f;
    } else if (g_capture_settle.load(std::memory_order_relaxed)) {
        if ((btn != 0) || (ax != 0.0f) || (ay != 0.0f)) {
            btn = 0;
            ax = 0.0f;
            ay = 0.0f;
        } else {
            g_capture_settle.store(false, std::memory_order_relaxed);
        }
    }

    *buttons = btn;
    *x = ax;
    *y = ay;
    // The session tap: everything the game is about to be handed, recorded or
    // replaced. See snap_input_tap below.
    snap_input_tap(buttons, x, y);
    *buttons |= uint16_t(g_inject_buttons.exchange(0, std::memory_order_relaxed));
    publish_pointer(g_rdram);

    return true;
}

// The runtime's motor callback (osMotorStart / osMotorStop), on a game
// thread. The Rumble Pak is on or off; the pad thread passes the change to
// SDL. Reachable only through the game's reset handler, which the port never
// runs: the cartridge does not rumble in play (see input_get_connected_device_info).
void input_set_rumble(int controller_num, bool rumble) {
    if (controller_num != 0) return;
    g_rumble_wanted.store(rumble ? 1 : 0, std::memory_order_relaxed);
}

ultramodern::input::connected_device_info_t input_get_connected_device_info(int controller_num) {
    // Port 4: the Snap Station, a controller with a pak-class device in it.
    // The game probes any port that reports a pak (contInitialize,
    // contDetectDevices) and recognizes the station by what the probe echoes
    // (snap_station.cpp); "ControllerPak" here only says a pak is present --
    // the runtime's own rumble path checks for RumblePak and leaves it alone.
    if (controller_num == 3 && snap::station_port4_present()) {
        return {
            .connected_device = ultramodern::input::Device::Controller,
            .connected_pak    = ultramodern::input::Pak::ControllerPak,
        };
    }
    if (controller_num != 0) {
        return {
            .connected_device = ultramodern::input::Device::None,
            .connected_pak    = ultramodern::input::Pak::None,
        };
    }

    // Port one is NEVER empty on PC: the keyboard is always attached, and
    // the game samples this exactly once at boot to pick its whole session's
    // shape -- controller present means title-first boot with the letter
    // bounce, absent means the dimmed no-controller flow. Reporting the SDL
    // pad's true state here made every boot a race against SDL's device
    // enumeration: some sessions got the real intro and some quietly lost
    // it, which also made the same input recording take different routes on
    // different boots.
    //
    // And nothing in the slot. Until 1.0.2 the port claimed a Rumble Pak
    // here whenever a pad was attached, and opened the pad on this call to
    // know -- the game asks at osContInit, before its first read -- so that
    // the game would set its motor up. The claim was worth nothing: the
    // cartridge never runs the motor in play (its only motor calls are in
    // the reset handler, and contRumbleStart is called nowhere in the ROM),
    // and the game's pak probe against the claim (contInitialize,
    // contDetectDevices) hit stubs and gave up. The open, on the game's own
    // controller thread, was where a slow controller driver held the game
    // (issue #7). A console with a controller and an empty pak slot reports
    // no pak; so does the port, from a function that touches no SDL.
    return {
        .connected_device = ultramodern::input::Device::Controller,
        .connected_pak    = ultramodern::input::Pak::None,
    };
}

} // namespace snap
