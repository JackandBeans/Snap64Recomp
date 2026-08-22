/**
 * @file matrix_tags.cpp
 * @brief Enables RT64's extended display list commands.
 *
 * Interpolation blends this frame's matrices with the previous frame's, which
 * requires knowing which object each matrix belongs to. A display list does
 * not say, and identity guessed from geometry cannot separate rows of
 * identical vegetation quads, tiled wall and sky segments, or two of the same
 * Pokemon. Ports built for RT64 answer this by patching the game to emit
 * gEXMatrixGroup commands; this does the same thing without touching the ROM.
 *
 * renPrepareModelMatrix(Gfx** gfxPtr, DObj* dobj) is called with the object
 * whose matrices it is about to emit, so intercepting it is enough: write a
 * matrix group naming that DObj into the display list, advance the game's own
 * write pointer past it, and let the original run. A DObj keeps its address
 * for as long as the object exists, which is the stability the matcher needs,
 * and a group applies to every matrix emitted after it, so one command covers
 * the whole object however many matrices it turns out to emit.
 *
 * The command travels inside the display list, which is what makes this
 * correct: RT64 reads it while walking the list on the graphics thread, with
 * no side table shared across threads and no dependence on when the list is
 * processed relative to when it was built.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hle/rt64_snap_diag.h"
#include "recomp.h"

#include "settings.h"

extern "C" {
#include "funcs.h"
}

namespace snap {
namespace {

// Encoding from lib/rt64/include/rt64_extended_gbi.h. The words are built
// here rather than by including that header, which needs the game's Gfx types.
constexpr uint32_t Param(uint32_t value, uint32_t bits, uint32_t shift) {
    return (value & ((1u << bits) - 1u)) << shift;
}

// G_SPNOOP under F3DEX2, which Snap uses. RT64 recognises the magic number
// that follows and treats the command as a hook rather than a no-op.
constexpr uint32_t HookOpcode = 0xE0;
constexpr uint32_t HookMagicNumber = 0x525464;
constexpr uint32_t HookOpEnable = 0x1;
constexpr uint32_t ExtendedOpcode = 0x64;
constexpr uint32_t MatrixGroupV1 = 0x00000C;

constexpr uint32_t ComponentInterpolate = 0x1;  // G_EX_COMPONENT_INTERPOLATE
constexpr uint32_t ComponentSkip = 0x0;         // G_EX_COMPONENT_SKIP
constexpr uint32_t OrderLinear = 0x0;           // G_EX_ORDER_LINEAR
constexpr uint32_t NoPush = 0x0;

constexpr uint32_t EnableWord0 = Param(HookOpcode, 8, 24) | Param(HookMagicNumber, 24, 0);
constexpr uint32_t EnableWord1 = Param(HookOpEnable, 4, 28) | Param(ExtendedOpcode, 8, 0);

// A matrix group is a two-command (four-word) extended packet: word0 the
// opcode, word1 the group id, word2 the component modes, word3 zero.
constexpr uint32_t MatrixGroupWord0 = Param(ExtendedOpcode, 8, 24) | Param(MatrixGroupV1, 24, 0);

// The camera's group, on the projection side: Snap's cameras multiply the
// look-at into the projection stack (render.c: G_MTX_MUL | G_MTX_PROJECTION),
// so a projection-side group emitted before the camera's matrices names the
// view transform the renderer interpolates. Simple rather than decomposed
// interpolation, matching what the renderer already defaulted the camera to.
// Everything derived from per-vertex or per-tile data is skipped: this game
// rebuilds those every frame, so ranges routinely describe different data and
// the derived velocities are meaningless.
constexpr uint32_t CameraGroupCommon =
    Param(NoPush, 1, 0) |
    Param(1, 1, 1) |                        // projection side
    Param(0, 1, 2) |                        // simple interpolation
    Param(ComponentInterpolate, 2, 11) |    // perspective
    Param(ComponentSkip, 2, 13) |           // vertices
    Param(ComponentSkip, 2, 15) |           // tiles
    Param(OrderLinear, 2, 17) |
    Param(0, 1, 19) |                       // edit: none
    Param(0, 2, 20) |                       // aspect: automatic
    Param(ComponentSkip, 2, 22) |           // texture coordinates
    Param(ComponentSkip, 2, 24);            // lookAt

// An ordinary frame: the view blends between its previous and current pose
// like any moving object.
constexpr uint32_t CameraGroupInterpolate = CameraGroupCommon |
    Param(ComponentInterpolate, 2, 3) |     // position
    Param(ComponentInterpolate, 2, 5);      // rotation

// A cut frame: the view snaps to its new pose while everything else keeps
// interpolating. This is the Zelda64Recomp camera pattern -- their camera
// patch emits exactly this group with skip components when the game skipped
// its camera -- and it is what makes a cut cost one native-rate step of the
// camera instead of a whole frame of frozen motion.
constexpr uint32_t CameraGroupSkip = CameraGroupCommon |
    Param(ComponentSkip, 2, 3) |            // position
    Param(ComponentSkip, 2, 5);             // rotation

// A Gfx command is two 32-bit words.
constexpr uint32_t GfxCommandSize = 8;

// Reserved ids RT64 gives its own meaning: a DObj address is neither.
constexpr uint32_t IdIgnore = 0x0;
constexpr uint32_t IdAuto = 0xFFFFFFFF;

bool valid_ram_address(uint32_t address) {
    // KSEG0/KSEG1 pointers into the 8MB the game sees.
    const uint32_t offset = address & 0x1FFFFFFFu;
    return (address >= 0x80000000u) && (offset < 0x00800000u);
}

} // namespace
} // namespace snap

// Camera cut detection, the way the shipped recomps do it: the game's own
// camera data says when a cut happened, in the game's own numbers, before
// any float reconstruction. Each camera's eye and look-at are compared with
// their values from the previous call for that camera; a jump beyond anything
// continuous motion produces -- the cart glides at about three units a frame,
// cutscene dollies a handful -- is a cut, whoever authored it: the intro's
// shot changes, a scene transition, a block-transition origin move.
//
// The verdict travels inside the display list as the camera's matrix group
// (skip components on a cut frame), which is what makes it correct: it lands
// on exactly the frame it was computed for, on the same thread that computed
// it, with no shared flag whose consumption depends on renderer timing. A
// camera the table has never seen needs no verdict at all -- its group id has
// no previous frame to pair with, so the renderer draws it snapped anyway.
namespace snap {
namespace {
struct CameraTrack {
    uint32_t address = 0;
    uint32_t lastSeen = 0;
    float eye[3] = {};
    float at[3] = {};
    // Last call's per-call motion, for the velocity-continuity test.
    float eyeDelta = 0.0f;
    float atDelta = 0.0f;
    bool valid = false;
};
// Eight slots outlast any set of cameras alive at once (main view, the item
// camera, photo passes); when the game has cycled through more addresses than
// that, the least recently seen is evicted rather than the table going blind.
// An evicted camera that comes back simply starts a fresh baseline.
CameraTrack g_camera_tracks[8];
uint32_t g_camera_seen_counter = 0;

float read_cam_f32(uint8_t* rdram, uint32_t addr) {
    const uint32_t bits = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)addr));
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
} // namespace
} // namespace snap

// Object identity comes from patches/src/render_patch.c, which tags each
// matrix with the OMMtx it belongs to from inside the game. This hook does
// the same for the camera: it names the view transform with the OMCamera it
// came from, and on the frame that camera's own data jumped it emits the
// group with skip components so the view snaps while everything else keeps
// interpolating. Camera setup runs ahead of object rendering each frame, so
// this is also where the extension gets turned on; RT64 keeps the state, and
// re-enabling is harmless.
extern "C" void renPrepareCameraMatrix(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t gfxPtrAddress = static_cast<uint32_t>(ctx->r4);

    // a1 is the OMCamera. Both view kinds keep eye at +0x3C and the look-at
    // target at +0x48 (sys/om.h: MtxCameraLookAt/LookAtRoll share the layout).
    const uint32_t cam = static_cast<uint32_t>(ctx->r5);
    bool cameraCut = false;
    if (snap::valid_ram_address(cam)) {
        snap::CameraTrack* track = nullptr;
        for (auto& t : snap::g_camera_tracks) {
            if (t.address == cam) { track = &t; break; }
        }
        if (track == nullptr) {
            for (auto& t : snap::g_camera_tracks) {
                if (t.address == 0) { track = &t; break; }
            }
        }
        if (track == nullptr) {
            for (auto& t : snap::g_camera_tracks) {
                if ((track == nullptr) || (t.lastSeen < track->lastSeen)) { track = &t; }
            }
            track->valid = false;
        }

        float eye[3], at[3];
        for (int i = 0; i < 3; i++) {
            eye[i] = snap::read_cam_f32(rdram, cam + 0x3C + i * 4);
            at[i] = snap::read_cam_f32(rdram, cam + 0x48 + i * 4);
        }
        float eyeD = 0.0f, atD = 0.0f;
        if (track->valid && (track->address == cam)) {
            float eyeD2 = 0.0f, atD2 = 0.0f;
            for (int i = 0; i < 3; i++) {
                const float de = eye[i] - track->eye[i];
                const float da = at[i] - track->at[i];
                eyeD2 += de * de;
                atD2 += da * da;
            }
            eyeD = sqrtf(eyeD2);
            atD = sqrtf(atD2);

            // A cut is a jump that BREAKS from the camera's own motion, not
            // merely a large one. The floor comes from measurement: the
            // fastest legitimate per-frame ride motion was 10.4 units, the
            // smallest genuine cut 29.5. But a scripted flyby moves 85+
            // units every frame with the delta gliding smoothly -- sustained
            // speed is motion, and blending motion is correct however fast
            // it is. So a delta only cuts when it is both past the floor and
            // discontinuous with the previous delta; the first frame of a
            // discontinuity always is, and the frames of a smooth sweep
            // never are. A false fire costs one native-rate camera step.
            constexpr float CutDistance = 25.0f;
            const auto discontinuous = [](float d, float prevD) {
                return fabsf(d - prevD) > std::max(6.0f, 0.3f * prevD);
            };
            const bool eyeCut = (eyeD > CutDistance) && discontinuous(eyeD, track->eyeDelta);
            const bool atCut = (atD > CutDistance) && discontinuous(atD, track->atDelta);
            if (eyeCut || atCut) {
                cameraCut = true;
                if (snapdiag::diagEnabled()) {
                    printf("[SNAP-CAMCUT] cam %08X eye moved %.1f at moved %.1f\n", cam, eyeD, atD);
                    fflush(stdout);
                }
            }
            else if (snapdiag::diagEnabled() && ((eyeD > 10.0f) || (atD > 10.0f))) {
                // Motion distribution for threshold tuning: anything over
                // ten units that did not cut.
                printf("[SNAP-CAMMOVE] cam %08X eye %.1f at %.1f\n", cam, eyeD, atD);
                fflush(stdout);
            }
        }
        track->address = cam;
        track->lastSeen = ++snap::g_camera_seen_counter;
        track->eyeDelta = eyeD;
        track->atDelta = atD;
        std::memcpy(track->eye, eye, sizeof(eye));
        std::memcpy(track->at, at, sizeof(at));
        track->valid = true;
    }

    // Enable the extension, then name this camera's view transform before the
    // original emits its matrices. The group id is the OMCamera itself, which
    // is stable for as long as the camera exists; a different camera is a
    // different id, so switching cameras never pairs two unrelated views.
    // Enabling is unconditional: the game patches emit extended commands
    // whether or not interpolation is on -- the matrix tags and the
    // widescreen border fills both ride in the display list -- and an
    // extension that is never enabled leaves them as unknown opcodes.
    if (snap::valid_ram_address(gfxPtrAddress)) {
        const uint32_t gfx = MEM_W(0, (gpr)(int32_t)gfxPtrAddress);
        if (snap::valid_ram_address(gfx)) {
            MEM_W(0x0, (gpr)(int32_t)gfx) = snap::EnableWord0;
            MEM_W(0x4, (gpr)(int32_t)gfx) = snap::EnableWord1;
            uint32_t cursor = gfx + snap::GfxCommandSize;
            if (snap::valid_ram_address(cam)) {
                MEM_W(0x0, (gpr)(int32_t)cursor) = snap::MatrixGroupWord0;
                MEM_W(0x4, (gpr)(int32_t)cursor) = cam;
                MEM_W(0x8, (gpr)(int32_t)cursor) = cameraCut ? snap::CameraGroupSkip : snap::CameraGroupInterpolate;
                MEM_W(0xC, (gpr)(int32_t)cursor) = 0;
                cursor += 2 * snap::GfxCommandSize;
            }
            MEM_W(0, (gpr)(int32_t)gfxPtrAddress) = cursor;
        }
    }

    __real_renPrepareCameraMatrix(rdram, ctx);

    // The group deliberately stays current after this returns. The renderer
    // binds a group to a projection at the projection's first draw, not at
    // the matrix load, so a reset emitted here would replace the camera's
    // group before anything drew under it. The next camera's group replaces
    // this one; a projection loaded with no camera of its own (the sprite
    // pass) inherits the last camera's group, which for its static view
    // changes nothing.
}

// Object identity for interpolation is the OMMtx a matrix belongs to, and its
// address alone is not enough, because the addresses come back.
//
// omGetMtx and omFreeMtx are a plain LIFO free list (om.c:464-486): freeing
// pushes onto the head and allocating pops it, with no clearing and nothing
// held back, so the most recently released matrix is the very next one handed
// out. Between scenes that would not matter. enterNextBlock does it mid-frame
// during ordinary play -- it deletes the Pokemon of the block being left and
// creates the next block's on the following line -- so addresses freed by the
// first call are reissued by the second before that frame's display list is
// built. Tagged by address, a new object's limb and a dead one's limb are the
// same id, they pair, and the limb is interpolated from an object a world
// block away. Stretched between two unrelated transforms it covers the screen,
// which is the frame of flat colour at a block boundary.
//
// The counter it carries has to be wide. A first attempt folded three bits into
// the spare low bits of the address, which leaves a one in eight chance that a
// reissued address collides with what it replaced; with dozens of matrices
// recycled at once that is several collisions at every boundary, and the
// artifact survived. This gives each allocation its own number instead, so a
// collision needs the counter to go all the way round.
//
// It goes in the next field, which is free for the whole time a matrix is
// allocated: omGetMtx reads it to advance the free list before it returns, and
// nothing touches it again until omFreeMtx overwrites it on release. Every
// other reference to it in the game is the free list itself (om.c:483).
//
// This lives here rather than in the game patch because the patch's own data is
// placed outside the memory the game can see, so state that has to persist
// belongs on this side.
extern "C" void omGetMtx(uint8_t* rdram, recomp_context* ctx) {
    __real_omGetMtx(rdram, ctx);

    const uint32_t mtx = static_cast<uint32_t>(ctx->r2);   // v0 holds the OMMtx*
    if (!snap::valid_ram_address(mtx)) {
        return;
    }

    // Skips both values the extended commands reserve, so a serial can never be
    // read as "ignore this matrix" or "work the identity out yourself".
    static uint32_t serial = 0;
    do {
        serial++;
    } while ((serial == snap::IdIgnore) || (serial == snap::IdAuto));

    MEM_W(0x0, (gpr)(int32_t)mtx) = serial;
}

// Set when the game crosses into the next world block. enterNextBlock rebases
// the world origin, so the frame it runs on is not continuous with the one
// before it. Consumed by send_dl, which passes it to the renderer.
namespace snap {
bool g_world_rebased = false;
float g_world_rebase_delta[3] = {};
}

// enterNextBlock hands this the distance the origin is about to move, which is
// what lets the previous frame be re-expressed in the new one instead of being
// thrown away.
//
// Only the first two floats of an o32 call arrive in floating point registers.
// The generated code settles it: bindCameraNextBlock opens with mtc1 $a2, $f20,
// the compiler pulling the third out of an integer register. Reading it as a
// float register gives whatever happened to be there.
extern "C" void bindCameraNextBlock(uint8_t* rdram, recomp_context* ctx) {
    float dz;
    const uint32_t bits = static_cast<uint32_t>(ctx->r6);
    std::memcpy(&dz, &bits, sizeof(dz));

    snap::g_world_rebase_delta[0] = ctx->f12.fl;
    snap::g_world_rebase_delta[1] = ctx->f14.fl;
    snap::g_world_rebase_delta[2] = dz;

    __real_bindCameraNextBlock(rdram, ctx);
}

// enterNextBlock returns the block moved into, or NULL when there was nowhere to
// go, which is the case where nothing was rebased.
extern "C" void enterNextBlock(uint8_t* rdram, recomp_context* ctx) {
    __real_enterNextBlock(rdram, ctx);

    const uint32_t block = static_cast<uint32_t>(ctx->r2);
    if (block != 0) {
        snap::g_world_rebased = true;
        if (snapdiag::diagEnabled() && snap::valid_ram_address(block)) {
            printf("[SNAP-BLOCK] entered block %d\n",
                   static_cast<int32_t>(MEM_W(0x0, (gpr)(int32_t)block)));
            fflush(stdout);
        }
    }
}

// Spawn-timing probes. The world creates a block's Pokemon one block before
// the cart arrives (enterNextBlock adds next->next), so a Pokemon that
// visibly pops into existence mid-ride either was created at that moment in
// view -- the game's own authored timing, reproducible from this log -- or
// existed for a block already and something on the draw side revealed it.
// One ride with SNAP_DIAG answers which.
namespace snap {
namespace {
void logSpawn(uint8_t* rdram, uint32_t spawn, int32_t blockIndex) {
    const uint32_t id = static_cast<uint32_t>(MEM_W(0x0, (gpr)(int32_t)spawn));
    float t[3];
    for (int i = 0; i < 3; i++) {
        t[i] = read_cam_f32(rdram, spawn + 0x08 + i * 4);
    }
    printf("[SNAP-SPAWN] block %d pokemon %u at (%.0f, %.0f, %.0f)\n",
           blockIndex, id, t[0], t[1], t[2]);
}
} // namespace
} // namespace snap

extern "C" void pokemonAdd(uint8_t* rdram, recomp_context* ctx) {
    if (snapdiag::diagEnabled()) {
        const uint32_t block = static_cast<uint32_t>(ctx->r4);
        if (snap::valid_ram_address(block)) {
            const int32_t index = static_cast<int32_t>(MEM_W(0x0, (gpr)(int32_t)block));
            const uint32_t desc = static_cast<uint32_t>(MEM_W(0x4, (gpr)(int32_t)block));
            if (snap::valid_ram_address(desc)) {
                uint32_t spawn = static_cast<uint32_t>(MEM_W(0x1C, (gpr)(int32_t)desc));
                for (int n = 0; snap::valid_ram_address(spawn) && (n < 40); n++, spawn += 0x30) {
                    if (static_cast<uint32_t>(MEM_W(0x0, (gpr)(int32_t)spawn)) == 0xFFFFFFFFu) {
                        break;
                    }
                    snap::logSpawn(rdram, spawn, index);
                }
                fflush(stdout);
            }
        }
    }

    __real_pokemonAdd(rdram, ctx);
}

extern "C" void pokemonAddOne(uint8_t* rdram, recomp_context* ctx) {
    if (snapdiag::diagEnabled()) {
        const uint32_t block = static_cast<uint32_t>(ctx->r4);
        const uint32_t spawn = static_cast<uint32_t>(ctx->r6);
        if (snap::valid_ram_address(block) && snap::valid_ram_address(spawn)) {
            snap::logSpawn(rdram, spawn, static_cast<int32_t>(MEM_W(0x0, (gpr)(int32_t)block)));
            fflush(stdout);
        }
    }

    __real_pokemonAddOne(rdram, ctx);
}
