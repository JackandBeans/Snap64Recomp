/**
 * @file spawn_fade.cpp
 * @brief Fades constructor-time Pokemon spawns into view -- the host half
 *        of the renderer-level forced-translucency fade.
 *
 * Six attempts to fade Pokemon by rewriting RDP state around their models
 * failed in presented-frame captures -- fog silhouettes, black bodies,
 * untextured wing shards -- and even the game's own dormant fader
 * (func_80360074) draws models half-untextured under HLE. A state-level
 * mode dump proved injected state reaches the draw intact while the output
 * is still wrong: the pixels are decided below the display list. So the
 * fade lives below it too, in three layers:
 *
 *   1. This module owns the Pokemon alpha byte (Pokemon+0xE4, initialised
 *      255 by the constructor and written by nothing else): alpha starts
 *      at zero on an eligible spawn and ramps to opaque over 24 drawn
 *      frames, advanced only on frames the player actually sees.
 *   2. The patch wrappers (patches/src/spawn_fade_patch.c) replace the
 *      seven pokemon_detect.c render callbacks with byte-for-byte stock
 *      bodies that additionally write a marker fog color -- the dormant
 *      fader's palette (230,250,180), used by no course -- carrying the
 *      alpha, around the model. Semantically inert to the RDP.
 *   3. The renderer detects the marker (rt64_state.cpp), forces the
 *      shader's view of that draw onto a standard translucent blend --
 *      colors, combiners, cycle types untouched -- and RasterPS.hlsl
 *      multiplies the marker's alpha into the blend, where nothing a
 *      display list says can override it.
 *
 * Deliberately NOT faded: scripted HIDE/SHOW reveals (Diglett popping from
 * the ground and the sand-dwellers' timed emergence are authored beats,
 * not artifacts), model-less controller objects, and props, gates and the
 * evolution flash (species id >= 1000). The Beach Butterfree additionally
 * fly in from above the view frustum
 * (patches/src/butterfree_approach_patch.c).
 *
 * The one hard gate: the photo-score screen re-creates photographed
 * Pokemon as fresh objects and derives the entire score from their
 * depth-buffer coverage. The forced translucent path writes no depth, so
 * a fade there would zero every photo. initObjectsOnPhoto is wrapped to
 * raise a flag, no fade starts while it is up, and entering it drives
 * every live fade to opaque first.
 *
 * Everything runs on the game thread (the spawn wrapper and the gtlUpdate
 * tick), so the table needs no locks. GObj addresses recycle instantly, so
 * every write is preceded by an identity check and a mismatch drops the
 * entry without touching memory it no longer owns.
 */
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "settings.h"
#include "hle/rt64_snap_diag.h"

// The presented-frame capture window (consumed by the present queue).
extern "C" std::atomic<int32_t> snap_frame_dump_pending;
// Monotonic drawn-frame counter (src/frame_cost.cpp's gtlDraw wrapper).
extern "C" std::atomic<uint32_t> snap_draw_serial;

namespace snap {

namespace {

// Game addresses (build/pokemonsnap.map). The app_level overlay is loaded
// whenever any course -- or the score screen that re-renders its Pokemon --
// is running, and these addresses are fixed within it.
constexpr uint32_t PokemonUpdate = 0x80362C50u;

// The seven Pokemon render callbacks (pokemon_detect.c), one per geometry
// type and fog mode -- all replaced by the marker-writing wrappers in
// patches/src/spawn_fade_patch.c, so any species drawing through them is
// eligible. Anything else on fnRender is a controller or special-effect
// object that must not fade.
constexpr uint32_t RenderTypes[] = {
    0x8035942Cu,   // renderPokemonModelTypeIFogged
    0x80359484u,   // renderPokemonModelTypeJFogged
    0x803594DCu,   // renderPokemonModelTypeBFogged
    0x80359534u,   // renderPokemonModelTypeDFogged
    0x8035958Cu,   // renderPokemonModelTypeI
    0x803595E4u,   // renderPokemonModelTypeB
    0x8035963Cu,   // renderPokemonModelTypeD
};

bool is_pokemon_render(uint32_t fnRender) {
    for (uint32_t candidate : RenderTypes) {
        if (fnRender == candidate) {
            return true;
        }
    }
    return false;
}

// GObj field offsets (include/sys/om.h) and the Pokemon alpha byte.
constexpr uint32_t GOBJ_LINK      = 0x0C;  // u8, LINK_POKEMON == 3
constexpr uint32_t GOBJ_FNUPDATE  = 0x14;  // u32
constexpr uint32_t GOBJ_FNRENDER  = 0x2C;  // u32
constexpr uint32_t GOBJ_USERDATA  = 0x58;  // u32 -> Pokemon
constexpr uint32_t POKE_ID        = 0x00;  // s32 species
constexpr uint32_t POKE_ALPHA     = 0xE4;  // u8, the fade byte

// Fade length in DRAWN frames (30 per second), advanced only when the game
// actually presented one -- a crossing's triple-update burst cannot consume
// steps the player never sees. 0.8s: quick enough that a snapped photo a
// beat later sees a fully-drawn subject, slow enough to read as arrival.
constexpr int FadeTicks = 24;

struct Fade {
    uint32_t gobj = 0;
    uint32_t userData = 0;
    int tick = 0;
};

Fade g_fades[64];
int g_fadeCount = 0;
bool g_scoring = false;
uint8_t* g_rdram_local = nullptr;

uint32_t rd_u32(uint32_t addr) {
    return *reinterpret_cast<uint32_t*>(g_rdram_local + (addr - 0x80000000u));
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
// callback and the userData pointer both still match. A recycled slot
// re-ran the constructor, which rewrote both (and reset the alpha).
bool fade_still_valid(const Fade& f) {
    return (rd_u32(f.gobj + GOBJ_FNUPDATE) == PokemonUpdate) &&
           (rd_u32(f.gobj + GOBJ_USERDATA) == f.userData);
}

// A dropped entry must never strand a live Pokemon translucent: the ticker
// is the only thing that would ever bring it back to 255. Only written when
// the Pokemon struct is still the one the fade was started for.
void finish_fade(const Fade& f) {
    if (rd_u32(f.gobj + GOBJ_USERDATA) == f.userData) {
        wr_u8(f.userData + POKE_ALPHA, 255);
    }
}

} // namespace

void spawn_fade_set_scoring(bool scoring) {
    g_scoring = scoring;
    if (scoring && g_rdram_local != nullptr) {
        // Anything mid-fade belongs to a course that is over; the score
        // screen must meet every object fully opaque and unhooked.
        for (int i = 0; i < g_fadeCount; i++) {
            finish_fade(g_fades[i]);
        }
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
    if (!is_pokemon_render(fnRender)) {
        if (snapdiag::statsEnabled()) {
            printf("[SNAP-FADE] gobj %08X species %u rejected: render %08X\n",
                   gobj, rd_u32(userData + POKE_ID), fnRender);
        }
        return;
    }

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

    wr_u8(userData + POKE_ALPHA, 0);
    g_fades[g_fadeCount++] = { gobj, userData, 0 };

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

    // Advance only when a frame was actually drawn since the last update;
    // undrawn updates (the block-crossing burst) hold the ramp still.
    static uint32_t lastDrawSerial = 0;
    const uint32_t drawSerial = snap_draw_serial.load(std::memory_order_relaxed);
    const bool advance = (drawSerial != lastDrawSerial);
    lastDrawSerial = drawSerial;

    for (int i = 0; i < g_fadeCount;) {
        Fade& f = g_fades[i];
        if (!fade_still_valid(f)) {
            // The object was deleted or rebuilt, or something re-aimed its
            // renderer. Leave a still-live Pokemon opaque and unhooked;
            // touch nothing on a recycled slot.
            finish_fade(f);
            drop_fade(i);
            continue;
        }
        if (!advance) {
            i++;
            continue;
        }
        f.tick++;
        if (f.tick >= FadeTicks) {
            wr_u8(f.userData + POKE_ALPHA, 255);
            drop_fade(i);
            continue;
        }
        wr_u8(f.userData + POKE_ALPHA, uint8_t((255 * f.tick) / FadeTicks));

        // SNAP_PCAP_SPAWN=<species> arms a presented-frame capture burst at
        // mid-fade (0 arms on any species): the second half of the ramp is
        // where a broken fade is unmistakable in pixels.
        if (f.tick == FadeTicks / 2) {
            static const long pcapSpecies = []() {
                const char* env = std::getenv("SNAP_PCAP_SPAWN");
                return (env != nullptr) ? strtol(env, nullptr, 10) : -1L;
            }();
            const long species = long(rd_u32(f.userData + POKE_ID));
            if ((pcapSpecies >= 0) && ((pcapSpecies == 0) || (pcapSpecies == species)) &&
                (snap_frame_dump_pending.load(std::memory_order_relaxed) <= 0)) {
                snap_frame_dump_pending.store(90);
                printf("[SNAP-PCAP] armed on species %ld mid-fade\n", species);
                fflush(stdout);
            }
        }
        i++;
    }
}

// Called just before the game's draw pass builds the display list: whatever
// the alpha byte holds HERE is what the fade renderer will read. If it no
// longer holds what the ticker wrote, something in the game's update pass
// rewrote it, and that writer is a bug to find.
void spawn_fade_predraw_check(uint8_t* rdram) {
    if ((g_fadeCount == 0) || (rdram == nullptr) || !snapdiag::statsEnabled()) {
        return;
    }
    g_rdram_local = rdram;
    for (int i = 0; i < g_fadeCount; i++) {
        const Fade& f = g_fades[i];
        if (!fade_still_valid(f)) {
            continue;
        }
        const uint8_t expected = uint8_t((255 * f.tick) / FadeTicks);
        const uint8_t now = rd_u8(f.userData + POKE_ALPHA);
        if (now != expected) {
            printf("[SNAP-FADE] PREDRAW MISMATCH gobj %08X species %u wrote %u draw sees %u\n",
                   f.gobj, rd_u32(f.userData + POKE_ID), expected, now);
        }
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
