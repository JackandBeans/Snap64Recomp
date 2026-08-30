/**
 * Teaches every Pokemon render type the fade the game only taught one.
 *
 * The game ships a whole-model fader it never calls: each Pokemon carries an
 * alpha byte (Pokemon+0xE4, set to 255 at construction and then written by
 * nothing), and pokemon.c's func_80360074_500484 draws a model through a
 * fog-register alpha blend -- a display list that points the blender at the
 * fog color, a gDPSetFogColor carrying the alpha, the model, and a restore.
 * But that fader hard-calls renderPokemonModelTypeI, and the game draws its
 * Pokemon through seven render callbacks across four geometry types: I, J,
 * B and D, fogged and not. On the Beach alone, Butterfree, Doduo and Meowth
 * draw through TypeBFogged and Pikachu and Snorlax through TypeJFogged --
 * all of them beyond the port's spawn fade until now.
 *
 * These replacements are the seven pokemon_detect.c render wrappers with one
 * added branch: when the alpha byte says anything but opaque, the model is
 * drawn through the game's own fade bracket around its own unfogged inner
 * renderer -- exactly the composition func_80360074_500484 performs for
 * TypeI, applied to the geometry type the species actually uses. At 255,
 * byte-for-byte the original behavior: the fogged variants call the same
 * app_render composite they always called, and the focus detector runs in
 * both paths, so a mid-fade Pokemon can still be photographed and detected.
 *
 * Only the port's spawn-fade module (src/spawn_fade.cpp) ever writes the
 * alpha byte below 255, and it is gated on the Spawn Fade setting, so with
 * the toggle off every path here is the stock one.
 */

#include "common.h"
#include "app_level/app_level.h"

/* The detector's own pieces (src/app_level/pokemon_detect.c). */
s32 Pokemon_GetFlag100(GObj* obj);
bool PokemonDetector_ReturnFalse(GObj* obj);
void PokemonDetector_SaveRegion(GObj* obj);
extern s32 PokemonDetector_IsEnabled;

/* The unfogged inner renderers (src/sys/render.c). */
void renRenderModelTypeI(GObj* obj);
void renRenderModelTypeJ(GObj* obj);
void renRenderModelTypeB(GObj* obj);
void renRenderModelTypeD(GObj* obj);

/* The fogged composites (src/app_render/4CDB0.c), untouched originals. */
void renderModelTypeIFogged(GObj* obj);
void renderModelTypeJFogged(GObj* obj);
void renderModelTypeBFogged(GObj* obj);
void renderModelTypeDFogged(GObj* obj);

extern Gfx* gMainGfxPos[];

/* The game's own fog set/clear pair (4CDB0.c). The restore replays them so
 * the COURSE fog color returns after the bracket's own fog-alpha write --
 * without this, far fogged world geometry drawn after a fading Pokemon
 * picked up the bracket's fog color (a pale blob where the fog band ate a
 * bush, captured). */
extern Gfx DListRMFogOpaSet[];
extern Gfx DListRMFogOpaClear[];

/* The lessons of three wrong brackets, so nobody builds a fourth. All three
 * were caught in presented-frame pixels, and each failed differently:
 *
 * (1) The game's own fader DLs (pokemon.c D_8038A3D0): its first blender
 * cycle mixes the FOG COLOR in by vertex SHADE alpha, and TypeB/TypeJ
 * models leave that alpha saturated -- the model painted flat fog-yellow.
 * (2) One-cycle blend with the inherited combiner: the blend WORKED (a
 * ramping translucent ghost), but the fogged composites author their
 * materials as two-cycle combiner programs and one cycle reinterprets
 * them -- black bodies. Overriding the combiner does not help; the model's
 * own material commands simply install theirs again (dark navy, capture
 * three).
 * (3) Two-cycle with a pass-through first blender cycle: textures right,
 * but the blend never engaged -- drawn opaque from the first frame.
 *
 * So the rule: the model must run in its authored cycle count with its own
 * combiners untouched, and the blend must be a shape the renderer provably
 * executes. For one-cycle (the unfogged types), that is the plain
 * fog-alpha blend -- pixel-proven by (2). For two-cycle (the fogged
 * types), it is the SAME fog-alpha blend in BOTH blender cycles: color
 * times fog-alpha over the framebuffer, twice -- a form the renderer's
 * blender emulation supports outright, with an effective factor of alpha
 * squared, which only steepens the ease-in. No combiner writes, no
 * geometry writes, no shade involvement anywhere. Structural flags (Z
 * compare, no Z write -- which is what the photo-score gate in
 * src/spawn_fade.cpp guards) match the game's own fade mode. Fog tint is
 * absent for the fade's under-a-second run. */
#define SNAP_FADE_RM(clk)                                                   \
    (Z_CMP | IM_RD | CVG_DST_FULL | ZMODE_XLU | FORCE_BL |                  \
     GBL_c##clk(G_BL_CLR_IN, G_BL_A_FOG, G_BL_CLR_MEM, G_BL_1MA))

#define SNAP_FADE_BRACKET(obj, inner, cyc)                                  \
    {                                                                       \
        gDPPipeSync(gMainGfxPos[0]++);                                      \
        gDPSetCycleType(gMainGfxPos[0]++, cyc);                             \
        gDPSetRenderMode(gMainGfxPos[0]++, SNAP_FADE_RM(1), SNAP_FADE_RM(2)); \
        gDPSetFogColor(gMainGfxPos[0]++, 230, 250, 180,                     \
                       GET_POKEMON(obj)->unk_E4);                           \
        inner(obj);                                                         \
        gSPDisplayList(gMainGfxPos[0]++, DListRMFogOpaSet);                 \
        gSPDisplayList(gMainGfxPos[0]++, DListRMFogOpaClear);               \
    }

#define SNAP_FADE_DRAW(obj, inner)     SNAP_FADE_BRACKET(obj, inner, G_CYC_1CYCLE)

#define SNAP_POKEMON_RENDER(obj, fadeDraw, inner, stock)                    \
    {                                                                       \
        if (!Pokemon_GetFlag100(obj) && !PokemonDetector_ReturnFalse(obj)) {\
            if (GET_POKEMON(obj)->unk_E4 != 255) {                          \
                fadeDraw(obj, inner)                                        \
            }                                                               \
            else {                                                          \
                stock(obj);                                                 \
            }                                                               \
            if (PokemonDetector_IsEnabled) {                                \
                PokemonDetector_SaveRegion(obj);                            \
            }                                                               \
        }                                                                   \
    }

/* Stock wrapper, byte-for-byte the pokemon_detect.c original. The B, D and
 * J families resist every display-list-level blend tried so far -- the mode
 * dump proved the bracket's blend survives to the draw untouched and the
 * output is still wrong (fog-tinted wings), so the interference is below
 * the display list. Until the renderer grows a forced-translucency path,
 * those types draw stock and their marquee pop (the Beach Butterfree) is
 * covered by the fly-in approach instead (butterfree_approach_patch.c).
 * Only the TypeI family fades: the geometry class the game's own fader was
 * written for, proven on Pidgey and Lapras. */
#define SNAP_POKEMON_RENDER_STOCK(obj, stock)                               \
    {                                                                       \
        if (!Pokemon_GetFlag100(obj) && !PokemonDetector_ReturnFalse(obj)) {\
            stock(obj);                                                     \
            if (PokemonDetector_IsEnabled) {                                \
                PokemonDetector_SaveRegion(obj);                            \
            }                                                               \
        }                                                                   \
    }

void renderPokemonModelTypeI(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW, renRenderModelTypeI, renRenderModelTypeI)
}

void renderPokemonModelTypeIFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW, renRenderModelTypeI, renderModelTypeIFogged)
}

void renderPokemonModelTypeJFogged(GObj* obj) {
    SNAP_POKEMON_RENDER_STOCK(obj, renderModelTypeJFogged)
}

void renderPokemonModelTypeB(GObj* obj) {
    SNAP_POKEMON_RENDER_STOCK(obj, renRenderModelTypeB)
}

void renderPokemonModelTypeBFogged(GObj* obj) {
    SNAP_POKEMON_RENDER_STOCK(obj, renderModelTypeBFogged)
}

void renderPokemonModelTypeD(GObj* obj) {
    SNAP_POKEMON_RENDER_STOCK(obj, renRenderModelTypeD)
}

void renderPokemonModelTypeDFogged(GObj* obj) {
    SNAP_POKEMON_RENDER_STOCK(obj, renderModelTypeDFogged)
}
