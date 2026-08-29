/**
 * @file input.cpp
 * @brief SDL2-based input implementation for WaveRace64-Recomp.
 *
 * Maps SDL2 game controller and keyboard input to N64 controller state.
 * Supports a single controller (port 0) with keyboard fallback.
 *
 * N64 button mapping:
 *   A       = SDL_CONTROLLER_BUTTON_A / Keyboard X
 *   B       = SDL_CONTROLLER_BUTTON_B / Keyboard Z
 *   Z       = SDL_CONTROLLER_BUTTON_LEFTSHOULDER / Keyboard L-Shift
 *   START   = SDL_CONTROLLER_BUTTON_START / Keyboard Return
 *   D-Up    = SDL_CONTROLLER_BUTTON_DPAD_UP / Keyboard Up
 *   D-Down  = SDL_CONTROLLER_BUTTON_DPAD_DOWN / Keyboard Down
 *   D-Left  = SDL_CONTROLLER_BUTTON_DPAD_LEFT / Keyboard Left
 *   D-Right = SDL_CONTROLLER_BUTTON_DPAD_RIGHT / Keyboard Right
 *   L       = SDL_CONTROLLER_AXIS_TRIGGERLEFT / Keyboard Q
 *   R       = SDL_CONTROLLER_AXIS_TRIGGERRIGHT / Keyboard E
 *   C-Up    = Right stick up / Keyboard I
 *   C-Down  = Right stick down / Keyboard K
 *   C-Left  = Right stick left / Keyboard J
 *   C-Right = Right stick right / Keyboard L
 *   Analog  = Left stick / Keyboard WASD
 */

#include "input.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <SDL2/SDL.h>

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

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static SDL_GameController* game_controller = nullptr;
static bool controller_initialized = false;

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
                printf("[SNAP-Input] Opened game controller: %s\n",
                       SDL_GameControllerName(game_controller));
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API (matches ultramodern::input::callbacks_t)
// ---------------------------------------------------------------------------

void input_poll() {
    if (!controller_initialized) {
        // SDL_Init should have been called by the gfx create callback.
        // Try to open a game controller if we haven't yet.
        try_open_controller();
        controller_initialized = true;
    }

    // Pick up newly connected controllers, and replace detached ones.
    try_open_controller();
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
    // Only support controller port 0.
    if (controller_num != 0) {
        return false;
    }

    uint16_t btn = 0;
    float ax = 0.0f;
    float ay = 0.0f;

    // -----------------------------------------------------------------------
    // Keyboard input
    // -----------------------------------------------------------------------
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        // Buttons
        if (keys[SDL_SCANCODE_X])       btn |= N64_BTN_A;
        if (keys[SDL_SCANCODE_Z])       btn |= N64_BTN_B;
        if (keys[SDL_SCANCODE_LSHIFT])  btn |= N64_BTN_Z;
        if (keys[SDL_SCANCODE_RETURN])  btn |= N64_BTN_START;
        if (keys[SDL_SCANCODE_UP])      btn |= N64_BTN_DU;
        if (keys[SDL_SCANCODE_DOWN])    btn |= N64_BTN_DD;
        if (keys[SDL_SCANCODE_LEFT])    btn |= N64_BTN_DL;
        if (keys[SDL_SCANCODE_RIGHT])   btn |= N64_BTN_DR;
        if (keys[SDL_SCANCODE_Q])       btn |= N64_BTN_L;
        if (keys[SDL_SCANCODE_E])       btn |= N64_BTN_R;
        if (keys[SDL_SCANCODE_I])       btn |= N64_BTN_CU;
        if (keys[SDL_SCANCODE_K])       btn |= N64_BTN_CD;
        if (keys[SDL_SCANCODE_J])       btn |= N64_BTN_CL;
        if (keys[SDL_SCANCODE_L])       btn |= N64_BTN_CR;

        // Analog stick from WASD
        if (keys[SDL_SCANCODE_W]) ay += 1.0f;
        if (keys[SDL_SCANCODE_S]) ay -= 1.0f;
        if (keys[SDL_SCANCODE_A]) ax -= 1.0f;
        if (keys[SDL_SCANCODE_D]) ax += 1.0f;
    }

    // -----------------------------------------------------------------------
    // Game controller input (overrides keyboard if connected)
    // -----------------------------------------------------------------------
    if (game_controller && SDL_GameControllerGetAttached(game_controller)) {
        // Buttons
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_A))
            btn |= N64_BTN_A;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_B))
            btn |= N64_BTN_B;
        // X = B on N64 (alternative)
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_X))
            btn |= N64_BTN_B;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
            btn |= N64_BTN_Z;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_START))
            btn |= N64_BTN_START;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_DPAD_UP))
            btn |= N64_BTN_DU;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            btn |= N64_BTN_DD;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            btn |= N64_BTN_DL;
        if (SDL_GameControllerGetButton(game_controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            btn |= N64_BTN_DR;

        // Triggers → L/R
        int16_t lt = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int16_t rt = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (lt > 8000)  btn |= N64_BTN_L;
        if (rt > 8000)  btn |= N64_BTN_R;

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
    if (rumble) {
        SDL_GameControllerRumble(game_controller, 0xFFFF, 0xFFFF, 100);
    } else {
        SDL_GameControllerRumble(game_controller, 0, 0, 0);
    }
#else
    (void)rumble;
#endif
}

ultramodern::input::connected_device_info_t input_get_connected_device_info(int controller_num) {
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
