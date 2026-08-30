/**
 * @file spawn_fade.cpp
 * @brief Fades constructor-time Pokemon spawns into view, using the game's
 *        own cut fade machinery.
 *
 * The game ships a whole-model fader it never calls: every Pokemon carries
 * an alpha byte (Pokemon+0xE4, initialised 255 and then written by nothing)
 * and pokemon.c contains a render wrapper (func_80360074) that draws the
 * model through a fog-register alpha blend -- uniform opacity for the whole
 * model, regardless of its materials. This module wires that machinery up:
 * when a Pokemon is constructed in view of the settings toggle, its render
 * callback is swapped to the game's own fade renderer, the alpha starts at
 * zero, and the per-tick stepper ramps it to opaque before restoring the
 * original callback. Positions, animations, timing, AI: untouched. The only
 * thing that changes is that materialisation takes a third of a second.
 *
 * Deliberately NOT faded: scripted HIDE/SHOW reveals (Diglett popping from
 * the ground is an authored beat, not an artifact), model-less controller
 * objects, props and eggs (species id >= 500), and any species whose render
 * callback is not one the game's fade renderer understands (it wraps
 * renderPokemonModelTypeI, so only TypeI and TypeIFogged species qualify --
 * which covers the block-table spawns that actually pop).
 *
 * The one hard gate: the photo-score screen re-creates photographed Pokemon
 * as fresh objects and derives the entire score from their depth-buffer
 * coverage. The fade renderer draws without depth writes, so a fade there
 * would zero every photo. initObjectsOnPhoto is wrapped to raise a flag,
 * and no fade starts while it is up.
 *
 * Everything runs on the game thread (the spawn wrapper and the gtlUpdate
 * tick), so the table needs no locks. GObj addresses recycle instantly, so
 * every write is preceded by an identity check and a mismatch drops the
 * entry without touching memory.
 */
#include <cstdint>
#include <cstdio>

#include "recomp.h"
#include "settings.h"
#include "hle/rt64_snap_diag.h"

namespace snap {

namespace {

// Game addresses (build/pokemonsnap.map). The app_level overlay is loaded
// whenever any course -- or the score screen that re-renders its Pokemon --
// is running, and these addresses are fixed within it.
constexpr uint32_t RenderTypeI       = 0x8035958Cu;
constexpr uint32_t RenderTypeIFogged = 0x8035942Cu;
constexpr uint32_t RenderFade        = 0x80360074u;   // the game's cut fade renderer
constexpr uint32_t PokemonUpdate     = 0x80362C50u;

// GObj field offsets (include/sys/om.h) and the Pokemon alpha byte.
constexpr uint32_t GOBJ_LINK      = 0x0C;  // u8, LINK_POKEMON == 3
constexpr uint32_t GOBJ_FNUPDATE  = 0x14;  // u32
constexpr uint32_t GOBJ_FNRENDER  = 0x2C;  // u32
constexpr uint32_t GOBJ_USERDATA  = 0x58;  // u32 -> Pokemon
constexpr uint32_t POKE_ID        = 0x00;  // s32 species
constexpr uint32_t POKE_ALPHA     = 0xE4;  // u8, the fade byte

// A third of a second of logic ticks: quick enough that a snapped photo a
// beat later sees a fully-drawn subject, slow enough to read as arrival.
constexpr int FadeTicks = 20;

struct Fade {
    uint32_t gobj = 0;
    uint32_t userData = 0;
    uint32_t origRender = 0;
    int tick = 0;
};

Fade g_fades[64];
int g_fadeCount = 0;
bool g_scoring = false;
uint8_t* g_rdram_local = nullptr;

uint32_t rd_u32(uint32_t addr) {
    return *reinterpret_cast<uint32_t*>(g_rdram_local + (addr - 0x80000000u));
}
void wr_u32(uint32_t addr, uint32_t v) {
    *reinterpret_cast<uint32_t*>(g_rdram_local + (addr - 0x80000000u)) = v;
}
uint8_t rd_u8(uint32_t addr) {
    return g_rdram_local[(addr - 0x80000000u) ^ 3u];
}
void wr_u8(uint32_t addr, uint8_t v) {
    g_rdram_local[(addr - 0x80000000u) ^ 3u] = v;
}
void drop_fade(int i) {
    g_fades[i] = g_fades[g_fadeCount - 1];
    g_fadeCount--;
}

// The entry is still the object it was created for exactly when the update
// callback, the userData pointer and our own render swap all still match.
// A recycled slot re-ran the constructor, which rewrote all three.
bool fade_still_valid(const Fade& f) {
    return (rd_u32(f.gobj + GOBJ_FNUPDATE) == PokemonUpdate) &&
           (rd_u32(f.gobj + GOBJ_USERDATA) == f.userData) &&
           (rd_u32(f.gobj + GOBJ_FNRENDER) == RenderFade);
}

} // namespace

void spawn_fade_set_scoring(bool scoring) {
    g_scoring = scoring;
    if (scoring) {
        // Anything mid-fade belongs to a course that is over; the score
        // screen must meet every object fully opaque and unhooked.
        g_fadeCount = 0;
    }
}

void spawn_fade_on_spawn(uint8_t* rdram, recomp_context* ctx) {
    if (!settings().spawn_fade || g_scoring || rdram == nullptr) {
        return;
    }
    g_rdram_local = rdram;

    const uint32_t gobj = uint32_t(ctx->r2);   // v0: the constructed GObj
    if (gobj == 0) {
        return;
    }
    if (rd_u8(gobj + GOBJ_LINK) != 3) {        // LINK_POKEMON only
        return;
    }
    const uint32_t userData = rd_u32(gobj + GOBJ_USERDATA);
    if (userData == 0) {
        return;
    }
    // Real creatures and visible props fade; the 1000+ range is gates, the
    // evolution flash controller and invisible helpers -- authored effects
    // and non-renders that must arrive exactly as built.
    if (rd_u32(userData + POKE_ID) >= 1000) {
        return;
    }
    const uint32_t fnRender = rd_u32(gobj + GOBJ_FNRENDER);
    if ((fnRender != RenderTypeI) && (fnRender != RenderTypeIFogged)) {
        return;                                 // the fade renderer only
    }                                           // understands TypeI models

    // Replace any stale entry for a recycled slot, newest spawn wins.
    for (int i = 0; i < g_fadeCount; i++) {
        if (g_fades[i].gobj == gobj) {
            drop_fade(i);
            break;
        }
    }
    if (g_fadeCount >= 64) {
        return;
    }

    wr_u32(gobj + GOBJ_FNRENDER, RenderFade);
    wr_u8(userData + POKE_ALPHA, 0);
    g_fades[g_fadeCount++] = { gobj, userData, fnRender, 0 };

    if (snapdiag::statsEnabled()) {
        printf("[SNAP-FADE] gobj %08X species %u fading in\n",
               gobj, rd_u32(userData + POKE_ID));
    }
}

void spawn_fade_tick(uint8_t* rdram) {
    if ((g_fadeCount == 0) || (rdram == nullptr)) {
        return;
    }
    g_rdram_local = rdram;

    for (int i = 0; i < g_fadeCount;) {
        Fade& f = g_fades[i];
        if (!fade_still_valid(f)) {
            // The object was deleted or rebuilt; whoever owns the slot now
            // configured it themselves. Touch nothing.
            drop_fade(i);
            continue;
        }
        f.tick++;
        if (f.tick >= FadeTicks) {
            wr_u8(f.userData + POKE_ALPHA, 255);
            wr_u32(f.gobj + GOBJ_FNRENDER, f.origRender);
            drop_fade(i);
            continue;
        }
        wr_u8(f.userData + POKE_ALPHA, uint8_t(255 * f.tick / FadeTicks));
        i++;
    }
}

} // namespace snap

// The score screen's rebuild: no fade may start while it runs, and none may
// still be running when it looks.
extern "C" void __real_initObjectsOnPhoto(uint8_t* rdram, recomp_context* ctx);
extern "C" void initObjectsOnPhoto(uint8_t* rdram, recomp_context* ctx) {
    snap::spawn_fade_set_scoring(true);
    __real_initObjectsOnPhoto(rdram, ctx);
    snap::spawn_fade_set_scoring(false);
}
