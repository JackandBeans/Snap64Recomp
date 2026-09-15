/**
 * @file vr_game.cpp
 * @brief The game's camera follows the head.
 *
 * The ride camera is two processes of the player object, updateCameraZoomedOut
 * and updateCameraZoomedIn (decomp src/app_level/player.c), each of which
 * turns the Control Stick's accumulated yaw and pitch into the camera's eye
 * and look-at point every tick. With the headset on, the port wraps both
 * (tools/hook_funcs.py renames the recompiled functions to __real_*) and,
 * just before each runs, writes the head's turn into the game's own yaw and
 * pitch variables: the cartridge's camera then does exactly what it does for
 * a stick held at that angle, and every photo, score and reaction is the
 * game's.
 *
 * Yaw, zoomed out, is a quarter-turn direction index plus an offset that the
 * game re-snaps past 54 degrees (updateCameraZoomedOut); the port picks the
 * nearest quarter turn and leaves the offset within it, so the snap never
 * fires. Zoomed in, the yaw is one angle around the whole circle. Pitch is
 * clamped to the limits the course set (MinPitch, MaxPitch): the photo
 * cannot look further down than the cartridge allowed, though the eyes
 * can. The stick's own contribution is zeroed for the tick so it cannot
 * fight the head; the game's sign convention is yaw positive to the right,
 * the headset's is positive to the left.
 *
 * After each runs, the camera the game computed (its eye and look-at, the
 * cart's position and rotation) goes to the renderer, which finds the
 * projection drawn with that camera and replaces it with each eye's.
 * Nothing is written while the game has taken the camera itself
 * (IsInputDisabled), so a scripted turn plays out as authored.
 */
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "recomp.h"
#include "funcs.h"

#include "vr_openxr.h"

namespace {

constexpr float Pi = 3.14159265358979f;
constexpr float Tau = 2.0f * Pi;

// The player camera's globals (decomp src/app_level/player.c; the addresses
// are the ones patches/game_syms.ld carries).
constexpr uint32_t AddrPlayerViewYaw = 0x80382CC8u;         // f32
constexpr uint32_t AddrViewPitch = 0x80382C0Cu;             // f32
constexpr uint32_t AddrDirectionIndex = 0x80382BFCu;        // s32 gDirectionIndex
constexpr uint32_t AddrCurrentDirectionYaw = 0x80382CD0u;   // f32
constexpr uint32_t AddrTurnToDirSpeed = 0x80382CCCu;        // f32
constexpr uint32_t AddrTargetDirectionZoomedIn = 0x80382C4Cu; // s32
constexpr uint32_t AddrStickXValue = 0x80382CC0u;           // f32
constexpr uint32_t AddrStickYValue = 0x80382CC4u;           // f32
constexpr uint32_t AddrMinPitch = 0x80382CECu;              // f32
constexpr uint32_t AddrMaxPitch = 0x80382CF0u;              // f32
constexpr uint32_t AddrIsInputDisabled = 0x80382D0Cu;       // s32
constexpr uint32_t AddrCameraEyePos = 0x803AE410u;          // Vec3f
constexpr uint32_t AddrCameraAtPos = 0x803AE420u;           // Vec3f
constexpr uint32_t AddrMovementState = 0x80366BA4u;         // MovementState (world.h)
constexpr uint32_t MovementPosOffset = 0x0Cu;               // Vec3f pos
constexpr uint32_t MovementRotOffset = 0x18u;               // Vec3f rotation

// The recompiled memory keeps each 32-bit word in host order (recomp.h's
// MEM_W), so a word is read in place.
uint32_t *word_at(uint8_t *rdram, uint32_t addr) {
    return reinterpret_cast<uint32_t *>(rdram + (addr - 0x80000000u));
}

float read_f32(uint8_t *rdram, uint32_t addr) {
    float f;
    std::memcpy(&f, word_at(rdram, addr), sizeof f);
    return f;
}

void write_f32(uint8_t *rdram, uint32_t addr, float f) {
    std::memcpy(word_at(rdram, addr), &f, sizeof f);
}

int32_t read_s32(uint8_t *rdram, uint32_t addr) {
    return static_cast<int32_t>(*word_at(rdram, addr));
}

void write_s32(uint8_t *rdram, uint32_t addr, int32_t v) {
    *word_at(rdram, addr) = static_cast<uint32_t>(v);
}

float wrap_pi(float a) {
    while (a > Pi) a -= Tau;
    while (a < -Pi) a += Tau;
    return a;
}

void camera_before(uint8_t *rdram, bool zoomed) {
    if (!snap::vr_active()) {
        return;
    }
    float yaw = 0.0f, pitch = 0.0f;
    if (!snap::vr_head_yaw_pitch(yaw, pitch)) {
        return;
    }
    if (read_s32(rdram, AddrIsInputDisabled) != 0) {
        return;
    }
    // The game's yaw is positive to the right; the headset's to the left.
    const float psi = -yaw;
    const float minPitch = read_f32(rdram, AddrMinPitch);
    const float maxPitch = read_f32(rdram, AddrMaxPitch);
    float p = pitch;
    if (p < minPitch) p = minPitch;
    if (p > maxPitch) p = maxPitch;
    write_f32(rdram, AddrViewPitch, p);
    write_f32(rdram, AddrStickXValue, 0.0f);
    write_f32(rdram, AddrStickYValue, 0.0f);
    if (!zoomed) {
        // DirectionsList: -pi, -pi/2, 0, pi/2 (and pi, the same facing as
        // -pi); the nearest, the rest as the offset, always within a
        // quarter turn.
        static const float dirs[4] = { -Pi, -Pi / 2.0f, 0.0f, Pi / 2.0f };
        int best = 2;
        float bestOffset = wrap_pi(psi);
        for (int k = 0; k < 4; k++) {
            const float off = wrap_pi(psi - dirs[k]);
            if (std::fabs(off) < std::fabs(bestOffset)) {
                best = k;
                bestOffset = off;
            }
        }
        write_s32(rdram, AddrDirectionIndex, best);
        write_f32(rdram, AddrCurrentDirectionYaw, dirs[best]);
        write_f32(rdram, AddrPlayerViewYaw, bestOffset);
        write_f32(rdram, AddrTurnToDirSpeed, 0.0f);
    }
    else {
        float y = psi;
        while (y < 0.0f) y += Tau;
        while (y >= Tau) y -= Tau;
        write_f32(rdram, AddrPlayerViewYaw, y);
        write_s32(rdram, AddrTargetDirectionZoomedIn, 0);
    }
}

void camera_after(uint8_t *rdram, bool zoomed) {
    if (!snap::vr_active()) {
        return;
    }
    float eye[3], at[3], pos[3], rot[3];
    for (uint32_t i = 0; i < 3; i++) {
        eye[i] = read_f32(rdram, AddrCameraEyePos + i * 4);
        at[i] = read_f32(rdram, AddrCameraAtPos + i * 4);
        pos[i] = read_f32(rdram, AddrMovementState + MovementPosOffset + i * 4);
        rot[i] = read_f32(rdram, AddrMovementState + MovementRotOffset + i * 4);
    }
    snap::vr_publish_camera(zoomed, eye, at, pos, rot);
}

} // namespace

extern "C" void updateCameraZoomedOut(uint8_t *rdram, recomp_context *ctx) {
    camera_before(rdram, false);
    __real_updateCameraZoomedOut(rdram, ctx);
    camera_after(rdram, false);
}

extern "C" void updateCameraZoomedIn(uint8_t *rdram, recomp_context *ctx) {
    camera_before(rdram, true);
    __real_updateCameraZoomedIn(rdram, ctx);
    camera_after(rdram, true);
}
