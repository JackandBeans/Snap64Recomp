/**
 * @file touch_controls.cpp
 * @brief Android's touch controls: the native side.
 *
 * A phone has no pad unless the player brings one, so the app draws the
 * game's buttons over the picture (android/.../Snap64TouchOverlay.java): in
 * a course A (the photo when zoomed, an apple when not), B (the pester
 * ball), Z (zoom), R (the dash), the four C buttons (turn left, right and
 * behind; C-Down the Poke Flute) and Start; outside one A, B and Start. On a
 * wide phone they sit in the black bars beside the 4:3 picture. The overlay
 * owns a touch that lands on a button and hands the held buttons here
 * whole; every other touch reaches SDL and this file:
 *
 * - in a course a drag anywhere aims, through the mouse's own path
 *   (input.cpp, apply_mouse_look: the game's pauses, cutscenes, zoom and
 *   pitch limits are honored the same way, and the mouse's sensitivity and
 *   zoom speed apply). A drag across the whole width of the screen turns the
 *   view as 640 pixels of the mouse do, a little over ninety degrees. One
 *   finger aims: the first to move on the picture, until it lifts. A finger
 *   that rests there (a thumb gripping the edge) never takes the aim, and
 *   another finger's motion while one aims does not turn the view;
 * - outside a course, and in one while a menu takes the mouse (the pause
 *   menu), a tap is the pointer's click (input.cpp, touch_as_pointer), so
 *   every menu that takes the mouse takes a finger.
 *
 * The overlay's Z, in the game's own Hold mode, and R (the dash, which runs
 * only while R is down) are held by a tap until the next tap, so one thumb
 * can zoom or dash and then press A; a longer press is a hold. In its
 * Switch mode the game toggles the zoom at each press and Z is a plain
 * button. The overlay learns the mode and the zoom from here (nativeState),
 * read from the game's memory at each pad read, and each photo the game
 * takes (its own count of the ride's photos) is handed to it from the
 * window's thread, so the shutter is felt (touch_controls_tick).
 *
 * touch_controls in the settings file: 0 automatic (shown until a pad or a
 * keyboard is used, and again at the next touch), 1 always, 2 never;
 * touch_size and touch_opacity in percent. No Options rows yet.
 */

#include "touch_controls.h"

#include <atomic>
#include <chrono>
#include <cstdio>

#include <SDL2/SDL.h>

#include "settings.h"

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace snap {

extern std::atomic<bool> g_app_level_resident;     // overlay_hook.cpp: a course's code is loaded
void input_add_touch_look(float dx, float dy);     // input.cpp: motion the mouse's way

#if defined(__ANDROID__)

extern uint8_t* g_rdram;                           // settings.cpp: the game's memory, once it is mapped
extern std::atomic<bool> g_phone_gyro_on;          // input.cpp: the phone's own gyroscope aims

namespace {

// A drag across the screen's whole width, in the mouse's pixels.
constexpr float kPixelsPerWidth = 640.0f;

// The game's zoom (app_level/player.c), in the course's own memory: the
// view's direction, -1 when zoomed in, and the Z Button setting the ride
// started with (1 Switch: each press toggles the zoom; 0 Hold).
constexpr uint32_t ADDR_gDirectionIndex = 0x80382BFC;  // s32: 0..3 facing, -1 zoomed in, -2 changing
constexpr uint32_t ADDR_ZoomSwitchMode  = 0x803AE520;  // s8
// Set while the title's attract demo plays a course from a script.
constexpr uint32_t ADDR_gIdleScript     = 0x803AE518;  // IdleScript*
std::atomic<bool> g_zoomed{false};
std::atomic<bool> g_zoom_switch{false};
std::atomic<bool> g_demo{false};

// The photos the game has taken in a ride: its own count of the ride's
// photos (app_render, makePhoto; reset as a ride starts), read at the pad
// reads. And the overlay's class and its photoTaken, kept from its first
// call: the window's thread hands each photo to it, since a thread the game
// made cannot find the app's classes by name.
constexpr uint32_t ADDR_gPhotoCount     = 0x800AC0E0;  // s32, 0 to 60
std::atomic<uint32_t> g_photos{0};
std::atomic<jclass> g_overlay_class{nullptr};
std::atomic<jmethodID> g_photo_taken{nullptr};

// The recompiled memory keeps each 32-bit word in host order, and bytes at
// their address XOR 3 (input.cpp's own readers, which are its file's).
int32_t read_s32(uint8_t* rdram, uint32_t addr) {
    return *reinterpret_cast<int32_t*>(rdram + (addr - 0x80000000u));
}
int8_t read_s8(uint8_t* rdram, uint32_t addr) {
    return static_cast<int8_t>(rdram[(addr - 0x80000000u) ^ 3u]);
}

std::atomic<uint32_t> g_touch_buttons{0};
// Buttons pressed since the game last read its pad: a tap can be down and
// up within a few milliseconds, between two of the game's reads, and the
// game reads state, so each press is held until one read has seen it.
std::atomic<uint32_t> g_touch_pressed{0};
// When a finger last touched the screen and when a pad or a keyboard was
// last pressed (steady-clock microseconds): the automatic controls show
// while the touch is the later. A touch at the start shows them.
std::atomic<int64_t> g_touch_at{1};
std::atomic<int64_t> g_other_at{0};
// The fingers SDL holds down (a finger on a button never reaches SDL), each
// with how far it has moved in all, in window pixels, since it touched or
// since the aim was last let go. The first to move further than a small
// share of the width takes the aim, until it lifts: a thumb gripping the
// edge or resting on the picture never does. Window thread only.
struct Finger {
    SDL_TouchID touch;
    SDL_FingerID id;
    float x, y;
};
constexpr int kMaxFingers = 10;
constexpr float kClaimShare = 0.003f;
Finger g_fingers[kMaxFingers];
int g_finger_count = 0;
bool g_aim_held = false;
SDL_TouchID g_aim_touch = 0;
SDL_FingerID g_aim_finger = 0;
// Its drag, for its log line when it lifts; and the motion of the fingers
// left out, for theirs.
float g_drag_x = 0.0f;
float g_drag_y = 0.0f;
int g_drag_events = 0;
float g_other_x = 0.0f;
float g_other_y = 0.0f;
int g_other_events = 0;

int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool course() {
    return g_app_level_resident.load(std::memory_order_relaxed);
}

int mode() {
    return settings().touch_controls;
}

bool shown() {
    const int m = mode();
    if (m == 1) {
        return true;
    }
    if (m == 2) {
        return false;
    }
    return g_touch_at.load(std::memory_order_relaxed) >= g_other_at.load(std::memory_order_relaxed);
}

void note_shown_change() {
    static int last = -1;
    const int now = shown() ? 1 : 0;
    if (now != last) {
        last = now;
        printf("[SNAP-TOUCH] the touch controls are %s\n", now ? "shown" : "hidden (a pad or a keyboard is in use)");
        fflush(stdout);
    }
}

const char* button_names(uint32_t bits, char* out, size_t cap) {
    static const struct { uint32_t bit; const char* name; } kNames[] = {
        {0x8000, "A"}, {0x4000, "B"}, {0x2000, "Z"}, {0x1000, "Start"}, {0x0010, "R"}, {0x0020, "L"},
        {0x0008, "C-Up"}, {0x0004, "C-Down"}, {0x0002, "C-Left"}, {0x0001, "C-Right"},
    };
    out[0] = '\0';
    size_t used = 0;
    for (const auto& n : kNames) {
        if ((bits & n.bit) != 0) {
            used += size_t(snprintf(out + used, (used < cap) ? cap - used : 0, "%s%s", used ? " " : "", n.name));
        }
    }
    if (used == 0) {
        snprintf(out, cap, "none");
    }
    return out;
}

bool aiming_finger(const SDL_TouchFingerEvent& finger) {
    return g_aim_held && (finger.touchId == g_aim_touch) && (finger.fingerId == g_aim_finger);
}

// The finger's entry, made when it is not there yet (its down was before the
// app came back, or more fingers were down than the table holds).
Finger* finger_entry(const SDL_TouchFingerEvent& finger) {
    for (int i = 0; i < g_finger_count; i++) {
        if ((g_fingers[i].touch == finger.touchId) && (g_fingers[i].id == finger.fingerId)) {
            return &g_fingers[i];
        }
    }
    if (g_finger_count == kMaxFingers) {
        return nullptr;
    }
    g_fingers[g_finger_count] = Finger{finger.touchId, finger.fingerId, 0.0f, 0.0f};
    return &g_fingers[g_finger_count++];
}

void drop_finger(const SDL_TouchFingerEvent& finger) {
    for (int i = 0; i < g_finger_count; i++) {
        if ((g_fingers[i].touch == finger.touchId) && (g_fingers[i].id == finger.fingerId)) {
            g_fingers[i] = g_fingers[--g_finger_count];
            return;
        }
    }
}

} // namespace

void touch_controls_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_KEYDOWN:
            // The phone's Back reaches SDL as a key; it is not a keyboard.
            if (!event.key.repeat && (event.key.keysym.scancode != SDL_SCANCODE_AC_BACK)) {
                g_other_at.store(now_us(), std::memory_order_relaxed);
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            g_other_at.store(now_us(), std::memory_order_relaxed);
            break;
        case SDL_CONTROLLERAXISMOTION:
            if ((event.caxis.value > 16000) || (event.caxis.value < -16000)) {
                g_other_at.store(now_us(), std::memory_order_relaxed);
            }
            break;
        case SDL_FINGERDOWN:
            g_touch_at.store(now_us(), std::memory_order_relaxed);
            finger_entry(event.tfinger);
            break;
        case SDL_FINGERMOTION:
            if (course() && (mode() != 2)) {
                int w = 0, h = 0;
                if (SDL_Window* win = SDL_GetWindowFromID(event.tfinger.windowID)) {
                    SDL_GetWindowSize(win, &w, &h);
                }
                if ((w > 0) && (h > 0)) {
                    // The finger's motion is in fractions of the window; in
                    // its own pixels it is the same on both axes.
                    const float scale = kPixelsPerWidth / float(w);
                    const float px = event.tfinger.dx * float(w);
                    const float py = event.tfinger.dy * float(h);
                    if (aiming_finger(event.tfinger)) {
                        input_add_touch_look(px * scale, py * scale);
                        g_drag_x += px * scale;
                        g_drag_y += py * scale;
                        g_drag_events++;
                    } else if (!g_aim_held) {
                        // No finger aims: this one takes the aim once it has
                        // moved far enough, and its whole motion turns the view.
                        Finger* f = finger_entry(event.tfinger);
                        if (f != nullptr) {
                            f->x += px;
                            f->y += py;
                            const float claim = kClaimShare * float(w);
                            if ((f->x * f->x + f->y * f->y) > claim * claim) {
                                g_aim_held = true;
                                g_aim_touch = event.tfinger.touchId;
                                g_aim_finger = event.tfinger.fingerId;
                                g_drag_x = f->x * scale;
                                g_drag_y = f->y * scale;
                                g_drag_events = 1;
                                input_add_touch_look(g_drag_x, g_drag_y);
                            }
                        }
                    } else {
                        g_other_x += px * scale;
                        g_other_y += py * scale;
                        g_other_events++;
                    }
                }
            }
            break;
        case SDL_FINGERUP:
            if (aiming_finger(event.tfinger)) {
                if (g_drag_events > 0) {
                    printf("[SNAP-TOUCH] a drag aimed: %.0f by %.0f of the mouse's pixels in %d moves\n",
                           g_drag_x, g_drag_y, g_drag_events);
                    fflush(stdout);
                }
                g_aim_held = false;
                g_drag_events = 0;
                // The fingers still down start afresh: what they moved while
                // another aimed does not count toward taking the aim.
                for (int i = 0; i < g_finger_count; i++) {
                    g_fingers[i].x = g_fingers[i].y = 0.0f;
                }
            } else if (g_other_events > 0) {
                printf("[SNAP-TOUCH] another finger moved %.0f by %.0f of the mouse's pixels in %d moves; "
                       "it does not aim\n", g_other_x, g_other_y, g_other_events);
                fflush(stdout);
                g_other_x = g_other_y = 0.0f;
                g_other_events = 0;
            }
            drop_finger(event.tfinger);
            break;
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_DIDENTERBACKGROUND:
            // A finger lost to the Home gesture may never lift in SDL's
            // eyes: no finger aims, and the next to move takes the aim.
            g_aim_held = false;
            g_finger_count = 0;
            g_drag_events = 0;
            g_other_events = 0;
            g_other_x = g_other_y = 0.0f;
            break;
        default:
            break;
    }
    note_shown_change();
}

uint16_t touch_controls_buttons() {
    // The game's zoom and its Z Button setting, for the overlay's Z: read
    // here, on the game's thread at its pad read, and only while a course's
    // code (which holds them) is loaded.
    static int namedMode = -1;   // the setting last named in the log; -1 outside a course
    static int32_t photosSeen = -1;   // the ride's photo count at the last read; -1 outside a ride
    bool zoomed = false, zoomSwitch = false, demo = false;
    if (course() && (g_rdram != nullptr)) {
        zoomed = (read_s32(g_rdram, ADDR_gDirectionIndex) == -1);
        zoomSwitch = (read_s8(g_rdram, ADDR_ZoomSwitchMode) == 1);
        demo = (read_s32(g_rdram, ADDR_gIdleScript) != 0);
        const int32_t taken = demo ? -1 : read_s32(g_rdram, ADDR_gPhotoCount);
        if ((photosSeen >= 0) && (taken > photosSeen)) {
            g_photos.fetch_add(uint32_t(taken - photosSeen), std::memory_order_relaxed);
        }
        photosSeen = taken;
        if (g_zoomed.load(std::memory_order_relaxed) != zoomed) {
            printf("[SNAP-TOUCH] the game zoomed %s\n", zoomed ? "in" : "out");
            fflush(stdout);
        }
        if (int(zoomSwitch) != namedMode) {
            namedMode = int(zoomSwitch);
            printf("[SNAP-TOUCH] the game's Z Button is %s\n",
                   zoomSwitch ? "Switch: the overlay's Z is a plain button" : "Hold: a tap on the overlay's Z holds it");
            fflush(stdout);
        }
    } else {
        namedMode = -1;
        photosSeen = -1;
    }
    g_zoomed.store(zoomed, std::memory_order_relaxed);
    g_zoom_switch.store(zoomSwitch, std::memory_order_relaxed);
    g_demo.store(demo, std::memory_order_relaxed);
    const uint32_t bits = g_touch_buttons.load(std::memory_order_relaxed) |
                          g_touch_pressed.exchange(0, std::memory_order_relaxed);
    // Each press the game reads, counted and named.
    static uint32_t last = 0;
    static int read = 0;
    const uint32_t rising = bits & ~last;
    last = bits;
    if (rising != 0) {
        char names[96];
        read++;
        printf("[SNAP-TOUCH] the game read a press: %s (%d in all)\n", button_names(rising, names, sizeof names), read);
        fflush(stdout);
    }
    return uint16_t(bits);
}

bool touch_controls_aiming() {
    // The phone's own gyroscope aims through the same path, touch controls
    // or not.
    return (mode() != 2) || g_phone_gyro_on.load(std::memory_order_relaxed);
}

bool touch_as_pointer(bool menuTakesMouse) {
    return !course() || menuTakesMouse;
}

bool touch_controls_shown() {
    return shown();
}

void touch_controls_tick() {
    static uint32_t handed = 0;
    const uint32_t photos = g_photos.load(std::memory_order_relaxed);
    if (photos == handed) {
        return;
    }
    const uint32_t count = photos - handed;
    handed = photos;
    const jclass cls = g_overlay_class.load(std::memory_order_acquire);
    const jmethodID taken = g_photo_taken.load(std::memory_order_acquire);
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if ((cls == nullptr) || (taken == nullptr) || (env == nullptr) || !shown()) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        env->CallStaticVoidMethod(cls, taken);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }
        printf("[SNAP-TOUCH] the game took a photo: the shutter is felt\n");
        fflush(stdout);
    }
}

} // namespace snap

// The overlay's two calls (Snap64TouchOverlay.java), on Android's UI thread.

// What the overlay draws: bit 0 shown, bit 1 a course runs, bit 2 the
// game's view zoomed in, bit 3 the game's Z Button is Switch, bit 4 the
// course is the title's attract demo, bits 8-15 the size and bits 16-23 the
// opacity in percent.
extern "C" JNIEXPORT jint JNICALL
Java_io_github_jackandbeans_snap64recomp_Snap64TouchOverlay_nativeState(JNIEnv* env, jclass cls) {
    if (snap::g_overlay_class.load(std::memory_order_acquire) == nullptr) {
        const jmethodID taken = env->GetStaticMethodID(cls, "photoTaken", "()V");
        if (taken == nullptr) {
            env->ExceptionClear();
        } else {
            snap::g_photo_taken.store(taken, std::memory_order_release);
            snap::g_overlay_class.store(static_cast<jclass>(env->NewGlobalRef(cls)), std::memory_order_release);
        }
    }
    const snap::Settings& s = snap::settings();
    const int size = s.touch_size;
    const int opacity = s.touch_opacity;
    const bool course = snap::course();
    static int lastCourse = -1;
    if (int(course) != lastCourse) {
        lastCourse = int(course);
        printf("[SNAP-TOUCH] %s\n", course ? "a course runs: all the buttons" : "no course: A, B and Start");
        fflush(stdout);
    }
    const bool zoomed = course && snap::g_zoomed.load(std::memory_order_relaxed);
    const bool zoomSwitch = course && snap::g_zoom_switch.load(std::memory_order_relaxed);
    const bool demo = course && snap::g_demo.load(std::memory_order_relaxed);
    return jint((snap::shown() ? 1 : 0) | (course ? 2 : 0) | (zoomed ? 4 : 0) | (zoomSwitch ? 8 : 0) |
                (demo ? 16 : 0) | ((size & 0xFF) << 8) | ((opacity & 0xFF) << 16));
}

// The buttons the overlay holds, whole, at every change.
extern "C" JNIEXPORT void JNICALL
Java_io_github_jackandbeans_snap64recomp_Snap64TouchOverlay_nativeButtons(JNIEnv* /*env*/, jclass /*cls*/, jint bits) {
    const uint32_t was = snap::g_touch_buttons.exchange(uint32_t(bits) & 0xFFFFu, std::memory_order_relaxed);
    const uint32_t pressed = uint32_t(bits) & ~was & 0xFFFFu;
    snap::g_touch_pressed.fetch_or(pressed, std::memory_order_relaxed);
    // A press is a touch. A release alone is not: the overlay lets go of its
    // buttons itself when a pad or a keyboard hides it, and that must not
    // show it again.
    if (pressed != 0) {
        snap::g_touch_at.store(snap::now_us(), std::memory_order_relaxed);
    }
    if (was != uint32_t(bits)) {
        char names[96];
        printf("[SNAP-TOUCH] buttons held: %s\n", snap::button_names(uint32_t(bits), names, sizeof names));
        fflush(stdout);
    }
    snap::note_shown_change();
}

#else

void touch_controls_event(const SDL_Event& /*event*/) {}

uint16_t touch_controls_buttons() {
    return 0;
}

bool touch_controls_aiming() {
    return false;
}

bool touch_as_pointer(bool /*menuTakesMouse*/) {
    return false;
}

} // namespace snap

#endif
