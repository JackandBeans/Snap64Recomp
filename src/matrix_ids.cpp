/**
 * @file matrix_ids.cpp
 * @brief Ground-truth object identity for RT64's frame interpolation.
 *
 * RT64 interpolates by pairing each frame's transforms with the previous
 * frame's, which requires knowing which object every matrix belongs to.
 * Games written for it say so directly, tagging matrices with object ids
 * through the extended GBI (gEXMatrixGroup); Pokemon Snap predates that, and
 * identity guessed from geometry or heap addresses is wrong often enough to
 * lerp one object into another.
 *
 * The game already knows, so ask it. renPrepareModelMatrix is called with the
 * object whose matrices it is about to build, and hal_mtx_f2l writes each
 * finished matrix to a known address, so intercepting the pair records
 * exactly which object owns the matrix at each address. RT64 looks the
 * address up while walking the display list (see State::snapMatrixIdLookup)
 * and tags the transform, making interpolation exact rather than inferred.
 *
 * Both hooks and the lookup run on the game thread: the display list is
 * processed inside send_dl, on the same thread that built these matrices.
 */

#include <cstdint>
#include <unordered_map>

#include "recomp.h"

extern "C" {
#include "funcs.h"
}

namespace snap {

namespace {

// Physical RDRAM address of a matrix -> id of the object that owns it.
// Keyed by address, so a rebuilt matrix simply overwrites its own entry and
// the table stays bounded by the number of distinct matrix addresses.
std::unordered_map<uint32_t, uint32_t> g_matrix_object_ids;

// The object renPrepareModelMatrix is currently building matrices for, as an
// id, or 0 when no such call is in progress.
uint32_t g_current_object_id = 0;

// N64 virtual addresses to the physical form RT64 indexes RDRAM with.
constexpr uint32_t PhysicalMask = 0x03FFFFFFu;

// Object ids must avoid RT64's two reserved values, G_EX_ID_IGNORE (0) and
// G_EX_ID_AUTO (0xFFFFFFFF). Object addresses fit in 24 bits of RDRAM, so
// tagging a high bit keeps every id distinct and inside the safe range.
constexpr uint32_t IdTag = 0x01000000u;

uint32_t object_id_from_address(uint32_t objectAddress) {
    return (objectAddress & 0x00FFFFFFu) | IdTag;
}

} // namespace

uint32_t matrix_id_for_address(uint32_t physicalAddress) {
    auto it = g_matrix_object_ids.find(physicalAddress & PhysicalMask);
    return (it != g_matrix_object_ids.end()) ? it->second : 0;
}

} // namespace snap

// a0 is the object whose matrices are about to be built. Anything
// hal_mtx_f2l writes while this runs belongs to it.
extern "C" void renPrepareModelMatrix(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t previous_object_id = snap::g_current_object_id;
    snap::g_current_object_id = snap::object_id_from_address(static_cast<uint32_t>(ctx->r4));

    __real_renPrepareModelMatrix(rdram, ctx);

    // Restored rather than cleared: the renderer walks object hierarchies, so
    // this can be reentered while an outer object is still being built.
    snap::g_current_object_id = previous_object_id;
}

// a1 is the destination matrix. Record who it belongs to before the call, so
// the mapping is in place even if the callee clobbers a1.
extern "C" void hal_mtx_f2l(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t destination = static_cast<uint32_t>(ctx->r5);
    if (snap::g_current_object_id != 0) {
        snap::g_matrix_object_ids[destination & snap::PhysicalMask] = snap::g_current_object_id;
    }
    else {
        // Built outside an object (camera, projection, one-off effects).
        // Dropping the entry leaves the transform untagged, which RT64 treats
        // as "match by geometry" instead of pairing it with a stale owner.
        snap::g_matrix_object_ids.erase(destination & snap::PhysicalMask);
    }

    __real_hal_mtx_f2l(rdram, ctx);
}
