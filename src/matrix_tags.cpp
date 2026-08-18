/**
 * @file matrix_tags.cpp
 * @brief Object identity for RT64's frame interpolation.
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
 * the whole object.
 *
 * The command travels inside the display list, which is what makes this
 * correct: RT64 reads it while walking the list on the graphics thread, with
 * no side table shared across threads and no dependence on when the list is
 * processed relative to when it was built.
 */

#include <cstdint>

#include "recomp.h"

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

// a0 is Gfx**, the game's display list write pointer. Emitting through it and
// storing it back keeps the game unaware that anything was inserted.
extern "C" void renPrepareModelMatrix(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t gfxPtrAddress = static_cast<uint32_t>(ctx->r4);
    const uint32_t dobj = static_cast<uint32_t>(ctx->r5);

    if (snap::valid_ram_address(gfxPtrAddress) && (dobj != snap::IdIgnore) && (dobj != snap::IdAuto)) {
        const uint32_t gfx = MEM_W(0, (gpr)(int32_t)gfxPtrAddress);
        if (snap::valid_ram_address(gfx)) {
            // gEXMatrixGroup occupies two commands: opcode and id, then the
            // component flags and a reserved word.
            MEM_W(0x0, (gpr)(int32_t)gfx) = snap::MatrixGroupWord0;
            MEM_W(0x4, (gpr)(int32_t)gfx) = dobj;
            MEM_W(0x8, (gpr)(int32_t)gfx) = snap::MatrixGroupWord2;
            MEM_W(0xC, (gpr)(int32_t)gfx) = 0;
            MEM_W(0, (gpr)(int32_t)gfxPtrAddress) = gfx + (2 * snap::GfxCommandSize);
        }
    }

    __real_renPrepareModelMatrix(rdram, ctx);
}

// Camera setup runs ahead of object rendering each frame, so this is where the
// extension gets turned on. RT64 keeps the state, and re-enabling is harmless.
extern "C" void renPrepareCameraMatrix(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t gfxPtrAddress = static_cast<uint32_t>(ctx->r4);

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
