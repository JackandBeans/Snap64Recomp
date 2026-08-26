/**
 * @file fx_tags.cpp
 * @brief Names each particle the effect system draws, so 2D effects interpolate.
 *
 * Koffing's smoke, the gust that sweeps Meowth, Jigglypuff's notes, Doduo's
 * sand, the leaves out of the tall grass -- all of it is one system, fx_draw,
 * and none of it carried a name. Measured on a real ride it produces up to nine
 * hundred and fifty rectangles in a reporting interval, and during those moments
 * over three thousand unnamed rectangles MOVED per interval, against a menu
 * screen where 98.5% of unnamed content stands perfectly still. That difference
 * is the whole complaint: unnamed content that moves is content stepping at the
 * rate the game draws at while the world around it runs at the display's.
 *
 * Why this cannot be done from outside the function. fx_draw builds its
 * rectangles inline, inside three nested loops, and calls nothing between the
 * loop head and the end -- there is no per-effect function for a wrapper to sit
 * on. And tagging the whole pass with one name is worse than useless: the
 * effect system is a single camera object, so every particle in the game would
 * share one id whose rectangle count changes every frame as particles are born
 * and die, and the renderer refuses a pair whenever an element's count moves.
 * It would report every rectangle named and pair none of them.
 *
 * So the tag is emitted from INSIDE the loop, at the address where the display
 * list cursor for one particle's commands is fetched. tools/hook_funcs.py grew
 * the ability to insert a call at a named guest address for this.
 *
 * Two facts make the insertion point correct, both read from the translated
 * MIPS rather than assumed:
 *
 *   - At 0x800A5970 the code does lw $v0, 0x0($t1) with $t1 holding
 *     gMainGfxPos, then advances the pointer by eight and writes its first
 *     command through $v0. A tag written and advanced BEFORE that load is
 *     picked up by it; a tag written after would be overwritten, because from
 *     that instruction on the cursor lives in a register and the pointer in
 *     memory is no longer consulted for this group.
 *
 *   - $s7 holds the particle for the whole body. The branch above it advances
 *     $s7 only on the path that skips drawing -- the recompiler jumps over the
 *     delay slot on the path that draws -- so a particle that reaches here is
 *     the one $s7 names, and the code below reads its fields at 0x4B and 0x6.
 *
 * Each particle emits exactly one rectangle, so the group is always a group of
 * one and the count can never change between frames. That is the one thing the
 * pairing rule cannot forgive, and this shape sidesteps it entirely.
 */
#include <atomic>
#include <cstdint>
#include <unordered_map>

#include "recomp.h"
#include "hle/rt64_snap_diag.h"

extern "C" {
#include "funcs.h"
}

namespace snap {
namespace {

constexpr uint32_t GMainGfxPos = 0x8004A890;

// Same packet the sprite tagger writes; see src/rect_tags.cpp for the layout.
constexpr uint32_t Param(uint32_t value, uint32_t width, uint32_t shift) {
    return (value & ((1u << width) - 1u)) << shift;
}

constexpr uint32_t HookOpcode = 0xE0;
constexpr uint32_t HookMagicNumber = 0x525464;
constexpr uint32_t HookOpEnable = 0x1;
constexpr uint32_t ExtendedOpcode = 0x64;
constexpr uint32_t RectGroupV1 = 0x000035;

constexpr uint32_t EnableWord0 = Param(HookOpcode, 8, 24) | Param(HookMagicNumber, 24, 0);
constexpr uint32_t EnableWord1 = Param(HookOpEnable, 4, 28) | Param(ExtendedOpcode, 8, 0);
constexpr uint32_t RectGroupWord0 = Param(ExtendedOpcode, 8, 24) | Param(RectGroupV1, 24, 0);

constexpr uint32_t TagBytes = 16;

// KSEG0 only: the MEM_ macros subtract 0x80000000 without masking, so a KSEG1
// pointer that passes a masked range test is read far outside what is mapped.
bool valid_ram_address(uint32_t address) {
    return ((address >> 29) == 4u) && ((address & 0x1FFFFFFFu) < 0x00800000u);
}

// A particle's address is not enough to name it by itself.
//
// fx_ejectStruct pushes a freed block onto the head of a free list and
// fx_createParticle pops it straight back, so the address of a particle that
// died can name a different particle on the very next frame. Paired on address
// alone, a new puff of sand would be drawn travelling from wherever the dead
// one was. The port already solved this exact problem for object matrices by
// stamping a serial in omGetMtx; the same idea applies here, except that
// nothing is known about which words of this struct are spare, so the serial is
// kept on this side rather than written into the game's memory.
//
// Reached only from the game's own thread, from creation and from drawing.
std::unordered_map<uint32_t, uint32_t> g_particle_serials;
uint32_t g_next_serial = 0;

uint32_t serial_for(uint32_t particle) {
    const auto it = g_particle_serials.find(particle);
    return (it != g_particle_serials.end()) ? it->second : 0u;
}

// How many times this particle has been drawn so far in this pass over the
// effects, cleared at the start of each one.
//
// A particle is drawn once per render pass and the number of passes it appears
// in is not the same every frame. Named by particle alone it is therefore an
// element whose rectangle count moves -- one frame two rectangles, the next
// frame one -- and a pair is refused whenever a count moves, because the
// ordinal is only emission order and a changed count shifts everything after
// it. Measured during effect bursts that refused twelve to twenty-four percent
// of elements outright, and only about half of the named effect rectangles were
// pairing at all against ninety-nine percent in the menus.
//
// Folding the occurrence into the name makes each drawing of a particle its own
// element with exactly one rectangle, so its count is one and can never change.
// The failure mode the gate exists to catch cannot arise here at all.
std::unordered_map<uint32_t, uint32_t> g_particle_occurrence;

// Mixes the address with the serial so a reissued address is a different name.
// Avoids both values the extended commands reserve, so an id can never be read
// as "ignore this" or "work it out yourself".
uint32_t particle_id(uint32_t particle, uint32_t serial, uint32_t pass) {
    uint32_t id = particle ^ (serial * 2654435761u) ^ (pass * 0x9E3779B9u);
    id ^= (id >> 13);
    if ((id == 0u) || (id == 0xFFFFFFFFu)) {
        id = 1u;
    }

    return id;
}

} // namespace
} // namespace snap

// Whether the main display list has room for one tag, plus the margin the game
// still needs for its own commands. The game sized these buffers for what it
// draws and does not check during the frame; past the end it writes into the
// neighbouring buffer and then the matrix heap.
static bool snap_fx_tag_fits(uint8_t* rdram, uint32_t cursor) {
    constexpr uint32_t GtlDLBuffers = 0x8004A850;
    constexpr uint32_t GtlContextId = 0x8004A910;
    constexpr uint32_t DLBufferSize = 8;
    constexpr uint32_t BufferKinds = 4;

    const uint32_t context = MEM_W(0, (gpr)(int32_t)GtlContextId);
    if (context >= 2u) {
        return false;
    }

    const uint32_t entry = GtlDLBuffers + ((context * BufferKinds) * DLBufferSize);
    const uint32_t start = MEM_W(0, (gpr)(int32_t)entry);
    const uint32_t capacity = MEM_W(0x4, (gpr)(int32_t)entry);
    if ((start == 0u) || (capacity == 0u) || (cursor < start)) {
        return false;
    }

    const uint32_t used = cursor - start;
    if (used > capacity) {
        return false;
    }

    return (capacity - used) > (snap::TagBytes + 64u);
}

// Called from inside fx_draw, at the address where one particle's display-list
// cursor is fetched. Names the particle so the rectangle that follows belongs
// to it and to nothing else.
extern "C" void snap_fx_particle(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t particle = static_cast<uint32_t>(ctx->r23);
    if (!snap::valid_ram_address(particle)) {
        return;
    }

    // Counted whether or not the name is written.
    //
    // The previous build gated this whole function on the feature switch, so
    // turning the naming off to stop the smearing also silenced the one number
    // that says WHY it smears. A measurement that stops when the thing it
    // measures is disabled cannot be used to decide whether to enable it.
    snap::g_particle_occurrence[particle]++;
    if (snapdiag::statsEnabled()) {
        snapdiag::fxTagsWrittenCounter().fetch_add(1, std::memory_order_relaxed);
    }

    // Which render pass this drawing belongs to, read from fx_draw's own loop
    // counter -- the prologue stores it at sp+0x1F8 and the drawing loops run
    // inside it.
    //
    // The pass matters because this game renders twice: the scene, and the
    // Pokemon detector's offscreen pass. fx_draw walks up to four cameras, so
    // one particle can be drawn once per pass in a single frame. Distinguished
    // only by drawing order, the on-screen drawing of one frame can carry the
    // same name as the OFFSCREEN drawing of the next, and pairing them blends a
    // rectangle towards a position in a buffer nobody sees -- which garbles.
    // The 3D path salts its ids by pass for precisely this reason
    // (renEXPassSalt, patches/src/render_patch.c); this is the same idea. The
    // pass slot is stable -- slot zero is the scene camera -- where drawing
    // order between passes is not.
    const uint32_t pass = static_cast<uint32_t>(MEM_W(0x1F8, ctx->r29)) & 3u;

    if (!snapdiag::fxTaggingEnabled().load(std::memory_order_relaxed)) {
        return;
    }

    const uint32_t cursor = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos));
    if (!snap::valid_ram_address(cursor) || !snap_fx_tag_fits(rdram, cursor)) {
        return;
    }

    const uint32_t id = snap::particle_id(particle, snap::serial_for(particle), pass);
    MEM_W(0x0, (gpr)(int32_t)cursor) = snap::EnableWord0;
    MEM_W(0x4, (gpr)(int32_t)cursor) = snap::EnableWord1;
    MEM_W(0x8, (gpr)(int32_t)cursor) = snap::RectGroupWord0;
    MEM_W(0xC, (gpr)(int32_t)cursor) = static_cast<int32_t>(id);
    MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos) = static_cast<int32_t>(cursor + snap::TagBytes);

    if (snapdiag::statsEnabled()) {
        snapdiag::rectsTaggedEffectsCounter().fetch_add(1, std::memory_order_relaxed);
    }
}

// The whole effect pass. Counted, and -- the part that matters -- CLOSED.
//
// Each particle opens a group naming itself, and a group stays current until
// something replaces it. Without a close the last particle of the pass keeps
// naming everything drawn afterwards, which then pairs against whatever
// happened to follow the last particle on the previous frame. Those are
// unrelated rectangles being blended towards each other, which is what ghosting
// is. src/rect_tags.cpp closes its groups for exactly this reason; this did not,
// and that was an omission rather than a decision.
extern "C" void fx_draw(uint8_t* rdram, recomp_context* ctx) {
    // How many different things the previous pass actually named. Compared
    // against the number of tags it wrote, this says whether the register the
    // tag reads is one particle or something shared between them.
    if (snapdiag::statsEnabled()) {
        snapdiag::fxDistinctParticlesCounter().fetch_add(
            uint32_t(snap::g_particle_occurrence.size()), std::memory_order_relaxed);
    }

    // Each pass counts its own drawings, so an id names one rectangle.
    snap::g_particle_occurrence.clear();

    const bool counting = snapdiag::statsEnabled();
    const uint32_t before = counting
        ? static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos))
        : 0u;

    __real_fx_draw(rdram, ctx);

    const uint32_t cursor = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos));
    if (counting && snap::valid_ram_address(before) && snap::valid_ram_address(cursor) &&
        (cursor > before) && ((cursor - before) < 0x8000u)) {
        uint32_t count = 0;
        for (uint32_t at = before; (at + 8u) <= cursor; at += 8u) {
            const uint32_t opcode = static_cast<uint32_t>(MEM_W(0, (gpr)(int32_t)at)) >> 24;
            if ((opcode == 0xE4u) || (opcode == 0xE5u) || (opcode == 0xF6u)) {
                count++;
            }
        }

        snapdiag::rectsFromEffectsCounter().fetch_add(count, std::memory_order_relaxed);
    }

    if (snap::valid_ram_address(cursor) && snap_fx_tag_fits(rdram, cursor)) {
        MEM_W(0x0, (gpr)(int32_t)cursor) = snap::EnableWord0;
        MEM_W(0x4, (gpr)(int32_t)cursor) = snap::EnableWord1;
        MEM_W(0x8, (gpr)(int32_t)cursor) = snap::RectGroupWord0;
        MEM_W(0xC, (gpr)(int32_t)cursor) = 0;
        MEM_W(0, (gpr)(int32_t)snap::GMainGfxPos) =
            static_cast<int32_t>(cursor + snap::TagBytes);
    }
}

// Every particle handed out gets a number, so a recycled address is a different
// name from the particle that used to live there.
extern "C" void fx_createParticle(uint8_t* rdram, recomp_context* ctx) {
    __real_fx_createParticle(rdram, ctx);

    const uint32_t particle = static_cast<uint32_t>(ctx->r2);
    if (!snap::valid_ram_address(particle)) {
        return;
    }

    // Bounded: the free list is finite, so the number of distinct addresses
    // this can ever hold is the size of the particle pool.
    snap::g_particle_serials[particle] = ++snap::g_next_serial;
}
