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

#include <cstdint>
#include <cstring>

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

constexpr uint32_t ComponentAuto = 0x2;   // G_EX_COMPONENT_AUTO
constexpr uint32_t ComponentSkip = 0x0;   // G_EX_COMPONENT_SKIP
constexpr uint32_t OrderLinear = 0x0;     // G_EX_ORDER_LINEAR
constexpr uint32_t EditAllow = 0x1;       // G_EX_EDIT_ALLOW
constexpr uint32_t InterpolateDecompose = 0x1;
constexpr uint32_t NoPush = 0x0;

constexpr uint32_t EnableWord0 = Param(HookOpcode, 8, 24) | Param(HookMagicNumber, 24, 0);
constexpr uint32_t EnableWord1 = Param(HookOpEnable, 4, 28) | Param(ExtendedOpcode, 8, 0);

constexpr uint32_t MatrixGroupWord0 = Param(ExtendedOpcode, 8, 24) | Param(MatrixGroupV1, 24, 0);

// Matches gEXMatrixGroupDecomposed: no push, modelview rather than
// projection, every component automatic, and linear ordering, which pairs an
// object's nth matrix with its own nth matrix from the previous frame.
//
// Decomposed rather than simple interpolation, which matters more here than in
// most games. Simple mode lerps the matrix element by element, and the result
// of blending two rotations that way is not a rotation: it skews and shrinks,
// the more so the larger the angle between them. Snap keeps the camera in the
// modelview matrices, so pitching the camera down rotates *every* object's
// matrix at once, and a fast look turns that error into visible warping.
// Decomposing into position, rotation and scale interpolates the rotation as a
// rotation, which is what RT64 recommends and what other ports tag actors with.
constexpr uint32_t MatrixGroupWord2 =
    Param(NoPush, 1, 0) |
    Param(0, 1, 1) |                        // proj: modelview
    Param(InterpolateDecompose, 1, 2) |
    Param(ComponentAuto, 2, 3) |            // position
    Param(ComponentAuto, 2, 5) |            // rotation
    Param(ComponentAuto, 2, 7) |            // scale
    Param(ComponentAuto, 2, 9) |            // skew
    Param(ComponentAuto, 2, 11) |           // perspective
    //
    // Everything below is interpolated from per-vertex and per-tile data
    // rather than from the matrix, and all of it is skipped here.
    //
    // Vertex interpolation derives a velocity per vertex by comparing this
    // frame's vertex data with the previous frame's over the matched
    // transform's vertex range. That assumes a vertex keeps its position in
    // the range between frames, which does not hold in this game: the vertex
    // heap is rebuilt every frame, so equally sized ranges routinely describe
    // different geometry and the velocities become meaningless. Applied to a
    // close, animated model it tears the model apart. Texture coordinate,
    // tile and lookAt interpolation are derived the same way, from data this
    // game also rebuilds, so they are skipped for the same reason.
    //
    // The transform itself still interpolates, which is where the smoothness
    // comes from.
    Param(ComponentSkip, 2, 13) |           // vertices
    Param(ComponentSkip, 2, 15) |           // tiles
    // Linear ordering. Automatic ordering was tried and stopped transforms
    // pairing at all (matched fell to zero while tagging stayed at 3891 of
    // 3951), so identity by id plus position in the object's list is what
    // actually matches here.
    Param(OrderLinear, 2, 17) |
    Param(EditAllow, 1, 19) |
    Param(0, 2, 20) |                       // aspect: automatic
    Param(ComponentSkip, 2, 22) |           // texture coordinates
    Param(ComponentSkip, 2, 24);            // lookAt

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

// Object identity itself now comes from patches/src/render_patch.c, which
// tags each matrix with the OMMtx it belongs to from inside the game. All
// that is left here is switching the extended commands on.
// Camera setup runs ahead of object rendering each frame, so this is where the
// extension gets turned on. RT64 keeps the state, and re-enabling is harmless.
extern "C" void renPrepareCameraMatrix(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t gfxPtrAddress = static_cast<uint32_t>(ctx->r4);

    // Unconditional: the game patches emit extended commands whether or not
    // interpolation is on -- the matrix tags and the widescreen border fills
    // both ride in the display list -- and an extension that is never enabled
    // leaves them as unknown opcodes. Enabling costs nothing when unused.
    if (snap::valid_ram_address(gfxPtrAddress)) {
        const uint32_t gfx = MEM_W(0, (gpr)(int32_t)gfxPtrAddress);
        if (snap::valid_ram_address(gfx)) {
            MEM_W(0x0, (gpr)(int32_t)gfx) = snap::EnableWord0;
            MEM_W(0x4, (gpr)(int32_t)gfx) = snap::EnableWord1;
            MEM_W(0, (gpr)(int32_t)gfxPtrAddress) = gfx + snap::GfxCommandSize;
        }
    }

    __real_renPrepareCameraMatrix(rdram, ctx);
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

    if (static_cast<uint32_t>(ctx->r2) != 0) {
        snap::g_world_rebased = true;
    }
}
