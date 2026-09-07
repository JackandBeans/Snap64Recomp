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
 * click advances Oak's text and confirms a menu the way A does. Its motion
 * feeds the game only while it is captured: mouse aim on in the settings,
 * the window focused, and a course running (.app_level resident,
 * src/overlay_hook.cpp). Then the cursor is hidden and its motion turns
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
 * Not an N64 button: SDL_CONTROLLER_BUTTON_BACK (Select on most pads) saves
 * the photo on screen, as the same button did on the Wii Virtual Console
 * release (src/photo_export.cpp). The keyboard's P does the same, through
 * the hotkey table in src/settings.cpp.
 */

#include "input.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include "hle/rt64_snap_diag.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <SDL2/SDL.h>

#include "paths.h"
#include "photo_export.h"
#include "settings.h"
#include "snap_station.h"

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

// The pad the port is reading. Defined here rather than with the rest of the
// internal state because a binding can now name one of its buttons, and
// source_down below has to be able to ask it.
static SDL_GameController* game_controller = nullptr;

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
// pad's opening from its name, or by pad_layout in the settings file.
static bool g_pad_layout_n64 = false;

static bool pad_button_held(int code) {
    if (game_controller == nullptr) {
        return false;
    }
    if (g_pad_layout_n64) {
        if (code == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            return SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
        }
        if (code == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            return SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        }
    }
    return SDL_GameControllerGetButton(game_controller, SDL_GameControllerButton(code)) != 0;
}

static bool pad_axis_held(int code) {
    if (game_controller == nullptr) {
        return false;
    }
    if (g_pad_layout_n64) {
        if (code == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            return SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
        }
        if (code == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            return SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
        }
    }
    // A trigger counts as held past the same eighth of its travel the
    // hardcoded mapping used. A stick axis bound to a button behaves the
    // same way, in its positive direction.
    return SDL_GameControllerGetAxis(game_controller, SDL_GameControllerAxis(code)) > 8000;
}

struct Source {
    SourceKind kind;
    int code;   // SDL_Scancode, the SDL mouse button index, or the pad's
                // SDL_GameControllerButton / SDL_GameControllerAxis
};

struct Resolved {
    std::vector<Source> sources[IN_COUNT];
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
        {"l",           {"Q", "Pad LeftTrigger"}},
        {"r",           {"E", "Pad RightTrigger"}},
        {"c_up",        {"I", "Wheel Up"}},
        {"c_down",      {"K", "Wheel Down"}},
        {"c_left",      {"J", "Mouse X1"}},
        {"c_right",     {"L", "Mouse X2"}},
        {"stick_up",    {"W"}},
        {"stick_down",  {"S"}},
        {"stick_left",  {"A"}},
        {"stick_right", {"D"}},
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
// The click that focuses the window arrives with the focus; until this
// moment, buttons are not presses.
std::atomic<int64_t> g_buttons_from{0};
constexpr int64_t FocusSettleUs = 250000;
std::atomic<uint32_t> g_mouse_held{0};                 // bit (1 << button index)
std::atomic<int64_t> g_mouse_press_until[8] = {};       // latched presses, microseconds
std::atomic<int64_t> g_wheel_up_until{0};
std::atomic<int64_t> g_wheel_down_until{0};
std::atomic<int64_t> g_esc_start_until{0};

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
constexpr uint32_t ADDR_gDirectionIndex         = 0x80382BFC;  // s32: 0..3 facing, -1 zoomed in, -2 changing
constexpr uint32_t ADDR_TargetDirectionZoomedIn = 0x80382C4C;  // s32: nonzero while a C button turns the view
constexpr uint32_t ADDR_ZoomedInCameraHeld      = 0x80382D08;  // s32 (D_80382D08_523118): nonzero holds the zoomed camera
constexpr uint32_t ADDR_IsInputDisabled         = 0x80382D0C;  // s32
constexpr uint32_t ADDR_IsPaused                = 0x80382D20;  // u8

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
int32_t read_s32(uint8_t* rdram, uint32_t addr) {
    return static_cast<int32_t>(*word_at(rdram, addr));
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
// game's states are honoured: nothing moves while paused, while input is
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
    if (dx == 0.0f && dy == 0.0f && gyaw == 0.0f && gpitch == 0.0f) SNAP_GYRO_DROP("no angle arrived");
    if (rdram == nullptr) SNAP_GYRO_DROP("no rdram");
    if (!g_app_level_resident.load(std::memory_order_relaxed)) SNAP_GYRO_DROP("not in a course");
    if (read_u8(rdram, ADDR_IsPaused) != 0) SNAP_GYRO_DROP("paused");
    if (read_s32(rdram, ADDR_IsInputDisabled) != 0) SNAP_GYRO_DROP("input disabled");
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

    // SDL's y grows downwards; the game's pitch grows upwards. The gyro's
    // pitch already grows upwards; the same tilt setting flips both.
    const float dpitch = (s.mouse_invert_y ? dy : -dy) * k + (s.mouse_invert_y ? -gpitch : gpitch) * gs;
    float pitch = read_f32(rdram, ADDR_ViewPitch) + dpitch;
    const float lo = read_f32(rdram, ADDR_MinPitch);
    const float hi = read_f32(rdram, ADDR_MaxPitch);
    if (pitch < lo) pitch = lo;
    if (pitch > hi) pitch = hi;
    write_f32(rdram, ADDR_ViewPitch, pitch);
    if (g_gyro_debug) g_dbg_stop = "applied to the view";
}
#undef SNAP_GYRO_DROP

bool source_down(const Source& src, const uint8_t* keys, uint32_t held, int64_t t) {
    switch (src.kind) {
        case SourceKind::Key:
            return keys != nullptr && src.code >= 0 && src.code < SDL_NUM_SCANCODES && keys[src.code];
        case SourceKind::MouseButton:
            return (src.code >= 0) && (src.code < 8) &&
                   (((held >> src.code) & 1u) || (t < g_mouse_press_until[src.code].load(std::memory_order_relaxed)));
        case SourceKind::WheelUp:
            return t < g_wheel_up_until.load(std::memory_order_relaxed);
        case SourceKind::WheelDown:
            return t < g_wheel_down_until.load(std::memory_order_relaxed);
        case SourceKind::PadButton:
            return pad_button_held(src.code);
        case SourceKind::PadAxis:
            return pad_axis_held(src.code);
    }
    return false;
}

} // namespace

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

void input_set_bindings(const Bindings& given) {
    const Bindings bindings = migrate_pad_bindings(given);
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
    for (const auto& entry : bindings) {
        bool known = false;
        for (const char* k : kInputNames) known = known || (entry.first == k);
        if (!known) {
            printf("[SNAP-Input] keys.%s: not an input this port has; ignored (input.h lists them)\n", entry.first.c_str());
        }
    }
    fflush(stdout);
    std::lock_guard<std::mutex> lock(g_bindings_mutex);
    g_resolved = std::move(r);
    g_bindings_in_force = std::move(in_force);
}

Bindings input_bindings() {
    std::lock_guard<std::mutex> lock(g_bindings_mutex);
    if (g_bindings_in_force.empty()) {
        return defaults();
    }
    return g_bindings_in_force;
}

void input_tap_start() {
    g_esc_start_until.store(now_us() + PressHoldUs, std::memory_order_relaxed);
}

void input_release_mouse() {
    if (g_captured.load(std::memory_order_relaxed)) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        g_captured.store(false, std::memory_order_relaxed);
        clear_mouse();
    }
}

void input_handle_sdl_event(const SDL_Event& event) {
    const bool captured = g_captured.load(std::memory_order_relaxed);
    const bool buttons_live = g_focused.load(std::memory_order_relaxed) &&
                              (now_us() >= g_buttons_from.load(std::memory_order_relaxed));
    switch (event.type) {
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                g_focused.store(false, std::memory_order_relaxed);
                clear_mouse();
            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                g_focused.store(true, std::memory_order_relaxed);
                g_buttons_from.store(now_us() + FocusSettleUs, std::memory_order_relaxed);
            }
            break;
        // SDL turns a touch into mouse motion and a mouse button unless it is
        // told not to, and a Steam Deck has a touchscreen under the player's
        // thumbs. Only a real pointer aims the camera; the synthetic one would
        // swing the view every time the screen was brushed.
        case SDL_MOUSEMOTION:
            if (captured && (event.motion.which != SDL_TOUCH_MOUSEID)) {
                std::lock_guard<std::mutex> lock(g_motion_mutex);
                g_motion_dx += float(event.motion.xrel);
                g_motion_dy += float(event.motion.yrel);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.which == SDL_TOUCH_MOUSEID) {
                break;
            }
            if (buttons_live && event.button.button < 8) {
                g_mouse_held.fetch_or(1u << event.button.button, std::memory_order_relaxed);
                g_mouse_press_until[event.button.button].store(now_us() + PressHoldUs, std::memory_order_relaxed);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.which == SDL_TOUCH_MOUSEID) {
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
            }
            break;
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
                // two reviewers had said so from the maths before that. A
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
    // A replayed run takes no input from the mouse (input_get), so it must
    // not take the cursor either: the release suite plays courses while
    // the author is elsewhere on the same desktop, and a captured cursor
    // locked them out of it.
    static const bool replaying = (getenv("SNAP_REPLAY") != nullptr);
    const bool wanted = !replaying &&
                        settings().mouse_aim &&
                        g_focused.load(std::memory_order_relaxed) &&
                        g_app_level_resident.load(std::memory_order_relaxed);
    // The pointer has no business on screen in fullscreen: there is nothing
    // beside the game to point at, and on a Steam Deck, which boots
    // fullscreen, it sat over the picture for the whole session. Windowed it
    // stays, because the window still has chrome to drag, resize and close.
    // Kept out of the early return below: fullscreen can change while the
    // capture state does not.
    {
        static int cursorState = -1;
        const int want = (settings().fullscreen && !wanted) ? SDL_DISABLE : SDL_ENABLE;
        if (cursorState != want) {
            SDL_ShowCursor(want);
            cursorState = want;
        }
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
    if (!wanted) {
        clear_mouse();
    }
    printf("[SNAP-Input] mouse %s\n", wanted ? "captured: it aims the camera" : "released");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static bool controller_initialized = false;

// The Back button's press, taken from SDL's event stream rather than from
// the state polled below. The game reads button STATE (input_get, on the
// thread the game reads its controller on), and a photo is saved on a
// press, on the main thread, where the hotkeys run and where the settings
// file is written. SDL already turns the press into an
// SDL_CONTROLLERBUTTONDOWN event on the thread that pumps events -- the
// main thread, in main.cpp's update_gfx, which then ignores controller
// events -- and an event watch is SDL's hook into that same delivery: it
// runs synchronously, on the pumping thread, as the event is queued. So
// this callback IS main-thread code, without a poll or a queue of its own.
// The return value of a watch is ignored by SDL.
static int SDLCALL photo_button_watch(void* /*userdata*/, SDL_Event* event) {
    if ((event->type == SDL_CONTROLLERBUTTONDOWN) &&
        (event->cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) {
        export_photo(g_rdram);
    }
    return 1;
}

// SDL knows a few thousand pads already. For the rest there is
// gamecontrollerdb.txt, the community's own list, which every other port of
// this kind accepts: drop the file beside the game and the pad works. The
// alternative SDL offers is an environment variable holding one raw mapping
// string, which is a developer's tool, not a player's. Absent is the normal
// case and says nothing.
static void load_controller_mappings() {
    const std::string path = base_path("gamecontrollerdb.txt").string();
    const int added = SDL_GameControllerAddMappingsFromFile(path.c_str());
    if (added > 0) {
        printf("[SNAP-Input] gamecontrollerdb.txt: %d controller mappings added\n", added);
        fflush(stdout);
    } else if (added == 0) {
        printf("[SNAP-Input] gamecontrollerdb.txt: no mappings in the file\n");
        fflush(stdout);
    }
}

static void try_open_controller() {
    // Drop a handle whose device is gone, otherwise the stale pointer blocks
    // every future open and a replugged controller never comes back.
    if (game_controller != nullptr && !SDL_GameControllerGetAttached(game_controller)) {
        SDL_GameControllerClose(game_controller);
        game_controller = nullptr;
    }

    if (game_controller != nullptr) return;

    int num_joysticks = SDL_NumJoysticks();
    for (int i = 0; i < num_joysticks; i++) {
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

                // The layout: by name unless the settings file decides.
                {
                    const int layout = settings().pad_layout;
                    const char* name = SDL_GameControllerName(game_controller);
                    bool n64 = false;
                    if (layout == 2) {
                        n64 = true;
                    }
                    else if (layout == 0) {
                        for (const char* p = name; (p != nullptr) && (*p != '\0'); p++) {
                            if (SDL_strncasecmp(p, "N64", 3) == 0) {
                                n64 = true;
                                break;
                            }
                        }
                    }
                    g_pad_layout_n64 = n64;
                    printf("[SNAP-Input] pad layout: %s%s\n",
                           n64 ? "N64-shaped (its L and R are L and R, its Z is Z)" : "standard (the left shoulder is Z, the triggers are L and R)",
                           (layout == 0) ? ", from the pad's name" : ", from pad_layout");
                }
                break;
            }
        }
        else {
            // A pad SDL has no mapping for is silent otherwise; say it was
            // seen, so a player knows why it does nothing and can hand SDL a
            // mapping through SDL_GAMECONTROLLERCONFIG (README, Controls).
            const char* name = SDL_JoystickNameForIndex(i);
            printf("[SNAP-Input] joystick %d (%s) has no game controller mapping; it is not used. "
                   "A line for it in gamecontrollerdb.txt, beside the game, would teach SDL this pad\n",
                   i, name ? name : "unnamed");
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
    const int64_t t = now_us();
    if (justEnabled) {
        onAt = t;
        lastSend = 0;
        deadSince = 0;
        deadLogs = 0;
        liveLogged = false;
        deadLogged = false;
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
    // Dead: no reading with any turning for half a second.
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
// Public API (matches ultramodern::input::callbacks_t)
// ---------------------------------------------------------------------------

void input_poll() {
    if (!controller_initialized) {
        // SDL_Init should have been called by the gfx create callback.
        // Any extra mappings first, so a pad this file teaches SDL about is
        // recognised by the open below rather than on some later poll.
        load_controller_mappings();
        // Try to open a game controller if we haven't yet.
        try_open_controller();
        // Registered once, after SDL_Init, for the life of the process.
        // SDL_AddEventWatch is thread-safe, so the thread this runs on does
        // not matter; the thread the watch runs on is the one that pumps.
        SDL_AddEventWatch(photo_button_watch, nullptr);
        controller_initialized = true;
    }

    // Pick up newly connected controllers, and replace detached ones.
    try_open_controller();
    sync_gyro();
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
}

bool input_get(int controller_num, uint16_t* buttons, float* x, float* y) {
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
        for (int i = 0; i < IN_COUNT; i++) {
            bool down = false;
            for (const Source& src : table->sources[i]) {
                if (source_down(src, keys, held, t)) { down = true; break; }
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
    }

    // -----------------------------------------------------------------------
    // Mouse look, while captured (a course, focused): the view itself moves,
    // in the game's memory; the stick is untouched. Under SNAP_REPLAY the
    // mouse is left out, so a replay is the recording and nothing else.
    // -----------------------------------------------------------------------
    if (g_captured.load(std::memory_order_relaxed) || g_gyro_enabled.load(std::memory_order_relaxed)) {
        static const bool replaying = (getenv("SNAP_REPLAY") != nullptr);
        if (!replaying) {
            apply_mouse_look(g_rdram);
        }
    }

    // -----------------------------------------------------------------------
    // Game controller input (overrides keyboard if connected)
    // -----------------------------------------------------------------------
    if (game_controller && SDL_GameControllerGetAttached(game_controller)) {
        // The pad's buttons and triggers went through the binding table above,
        // beside the keyboard's: their defaults are the mapping that used to
        // be written out here, and the settings file can now say otherwise.
        // What stays is the analogue: a stick is a direction and a magnitude,
        // not a button, and neither of the two below is expressible as one.

        // Right stick → C buttons (threshold-based)
        int16_t rx = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_RIGHTX);
        int16_t ry = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_RIGHTY);
        constexpr int16_t C_THRESHOLD = 16000;
        if (ry < -C_THRESHOLD) btn |= N64_BTN_CU;
        if (ry >  C_THRESHOLD) btn |= N64_BTN_CD;
        if (rx < -C_THRESHOLD) btn |= N64_BTN_CL;
        if (rx >  C_THRESHOLD) btn |= N64_BTN_CR;

        // Left stick → analog
        int16_t lx = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ly = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTY);

        // Normalize to -1.0..1.0 range.
        float gc_x = static_cast<float>(lx) / 32767.0f;
        float gc_y = static_cast<float>(-ly) / 32767.0f; // Invert Y (SDL Y+ is down)

        // The dead zone exists for the pad, not for the game: a real N64
        // stick has none, and this only covers the rest an analog stick on a
        // modern controller does not quite return to.
        //
        // Taken as a distance from centre and then rescaled, so the first
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
        constexpr float DEADZONE = 0.15f;
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

    *buttons = btn;
    *x = ax;
    *y = ay;
    // The session tap: everything the game is about to be handed, recorded or
    // replaced. See snap_input_tap below.
    snap_input_tap(buttons, x, y);

    return true;
}

void input_set_rumble(int controller_num, bool rumble) {
    if (controller_num != 0 || !game_controller) return;

#if SDL_VERSION_ATLEAST(2, 0, 9)
    // The Rumble Pak is on or off: the game runs the motor with osMotorStart
    // and stops it with osMotorStop, and nothing re-triggers in between. The
    // hundred milliseconds asked for here was therefore a cutoff, not a
    // duration -- every rumble the game meant to hold ended after a tenth of
    // a second. It runs until it is told to stop now.
    const int pct = std::clamp(settings().rumble_strength, 0, 100);
    if (rumble && (pct > 0)) {
        const Uint16 mag = Uint16((0xFFFF * pct) / 100);
        // SDL caps a rumble at SDL_MAX_RUMBLE_DURATION_MS (0xFFFF, about
        // 65 seconds) whatever is asked for, so that is the longest a single
        // start can run. The game always stops its own buzzes long before
        // then; asking for more would only be clamped to this anyway.
        SDL_GameControllerRumble(game_controller, mag, mag, 0xFFFF);
    } else {
        SDL_GameControllerRumble(game_controller, 0, 0, 0);
    }
#else
    (void)rumble;
#endif
}

ultramodern::input::connected_device_info_t input_get_connected_device_info(int controller_num) {
    // Port 4: the Snap Station, a controller with a pak-class device in it.
    // The game probes any port that reports a pak (contInitialize,
    // contDetectDevices) and recognises the station by what the probe echoes
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

    // Asked here as well as from the poll, because the game asks this first.
    // contInitialize calls osContInit before it ever reads the port, and this
    // used to be reachable only through osContStartReadData -- so at the one
    // moment the answer mattered no controller had been opened yet, the port
    // reported nothing attached, and the game skipped its pak and motor setup
    // for good. It runs once and is never repeated, so rumble was dead for
    // every player in every session, controller plugged in or not.
    try_open_controller();

    // Port one is NEVER empty on PC: the keyboard is always attached, and
    // the game samples this exactly once at boot to pick its whole session's
    // shape -- controller present means title-first boot with the letter
    // bounce, absent means the dimmed no-controller flow. Reporting the SDL
    // pad's true state here made every boot a race against SDL's device
    // enumeration: some sessions got the real intro and some quietly lost
    // it, which also made the same input recording take different routes on
    // different boots. Only the Rumble Pak claim follows the physical pad,
    // because pak probing paths should not run against hardware that is not
    // there.
    const bool attached = (game_controller != nullptr) && SDL_GameControllerGetAttached(game_controller);
    return {
        .connected_device = ultramodern::input::Device::Controller,
        .connected_pak    = attached ? ultramodern::input::Pak::RumblePak
                                     : ultramodern::input::Pak::None,
    };
}

} // namespace snap
