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

/* The lessons of two wrong brackets, so nobody builds a third:
 *
 * The game's own fader (pokemon.c D_8038A3D0) blends fog-by-SHADE-alpha in
 * its first cycle, and TypeB/TypeJ models leave shade alpha saturated -- the
 * whole model painted as the fog color, a flat pale-yellow silhouette. And a
 * one-cycle bracket breaks differently: the fogged composites run their
 * models in TWO-cycle with a two-cycle combiner installed, and dropping to
 * one cycle makes the RDP reinterpret that combiner -- black bodies, one
 * yellow wing (captured frame by frame both times).
 *
 * So the rule the brackets below follow: keep exactly the cycle count and
 * geometry state the stock path runs the model in, and change ONE thing --
 * the final blender cycle becomes model-color times the fog-alpha register
 * against the framebuffer. No combiner writes, no shade involvement; the
 * materials draw their textured selves at partial opacity. Structural flags
 * (Z compare, no Z write -- which is what the photo-score gate in
 * src/spawn_fade.cpp guards) match the game's own fade mode. */
#define SNAP_FADE_BLEND GBL_c2(G_BL_CLR_IN, G_BL_A_FOG, G_BL_CLR_MEM, G_BL_1MA)
#define SNAP_FADE_C2_OPA                                                    \
    (Z_CMP | IM_RD | CVG_DST_FULL | ZMODE_XLU | FORCE_BL | SNAP_FADE_BLEND)
#define SNAP_FADE_C2_XLU                                                    \
    (AA_EN | Z_CMP | IM_RD | CVG_DST_WRAP | CLR_ON_CVG | FORCE_BL |         \
     ZMODE_XLU | SNAP_FADE_BLEND)

/* Unfogged stock path runs the model in the one-cycle ambient state; the
 * fade does too, with the blend in both halves (one-cycle reads c2). */
#define SNAP_FADE_RM_1C(clk)                                                \
    (Z_CMP | IM_RD | CVG_DST_FULL | ZMODE_XLU | FORCE_BL |                  \
     GBL_c##clk(G_BL_CLR_IN, G_BL_A_FOG, G_BL_CLR_MEM, G_BL_1MA))

#define SNAP_FADE_DRAW(obj, inner)                                          \
    {                                                                       \
        gDPPipeSync(gMainGfxPos[0]++);                                      \
        gDPSetCycleType(gMainGfxPos[0]++, G_CYC_1CYCLE);                    \
        gDPSetRenderMode(gMainGfxPos[0]++, SNAP_FADE_RM_1C(1), SNAP_FADE_RM_1C(2)); \
        gDPSetFogColor(gMainGfxPos[0]++, 230, 250, 180,                     \
                       GET_POKEMON(obj)->unk_E4);                           \
        inner(obj);                                                         \
        gDPPipeSync(gMainGfxPos[0]++);                                      \
        gDPSetCycleType(gMainGfxPos[0]++, G_CYC_1CYCLE);                    \
        gDPSetRenderMode(gMainGfxPos[0]++, G_RM_AA_ZB_OPA_SURF, G_RM_NOOP2); \
    }

/* Fogged stock path (enableFog/disableFog): two-cycle, G_FOG geometry. The
 * fade keeps both and swaps the fog-shade first cycle for a pass-through,
 * so no shade alpha ever reaches the blender. Restores mirror the game's
 * DListRMFogOpaClear. */
#define SNAP_FADE_DRAW_FOG(obj, inner)                                      \
    {                                                                       \
        gDPPipeSync(gMainGfxPos[0]++);                                      \
        gDPSetCycleType(gMainGfxPos[0]++, G_CYC_2CYCLE);                    \
        gDPSetRenderMode(gMainGfxPos[0]++, G_RM_PASS, SNAP_FADE_C2_OPA);    \
        gSPSetGeometryMode(gMainGfxPos[0]++, G_FOG);                        \
        gDPSetFogColor(gMainGfxPos[0]++, 230, 250, 180,                     \
                       GET_POKEMON(obj)->unk_E4);                           \
        inner(obj);                                                         \
        gDPPipeSync(gMainGfxPos[0]++);                                      \
        gDPSetCycleType(gMainGfxPos[0]++, G_CYC_1CYCLE);                    \
        gDPSetRenderMode(gMainGfxPos[0]++, G_RM_AA_ZB_OPA_SURF, G_RM_NOOP2); \
        gSPClearGeometryMode(gMainGfxPos[0]++, G_FOG);                      \
    }

/* TypeJ/TypeD fogged stock paths also arm the deferred transparency list
 * (enableFogTrasparent on gMainGfxPos[1]); any of the model's translucent
 * pieces would otherwise draw at full authored strength mid-fade. Same
 * swap there: pass-through first cycle, fade blend with the XLU structural
 * flags second; restore mirrors DListRMFogXluClear. */
#define SNAP_FADE_DRAW_FOG_XLU(obj, inner)                                  \
    {                                                                       \
        gDPPipeSync(gMainGfxPos[1]++);                                      \
        gDPSetCycleType(gMainGfxPos[1]++, G_CYC_2CYCLE);                    \
        gDPSetRenderMode(gMainGfxPos[1]++, G_RM_PASS, SNAP_FADE_C2_XLU);    \
        gSPSetGeometryMode(gMainGfxPos[1]++, G_FOG);                        \
        gDPSetFogColor(gMainGfxPos[1]++, 230, 250, 180,                     \
                       GET_POKEMON(obj)->unk_E4);                           \
        SNAP_FADE_DRAW_FOG(obj, inner)                                      \
        gDPPipeSync(gMainGfxPos[1]++);                                      \
        gDPSetCycleType(gMainGfxPos[1]++, G_CYC_1CYCLE);                    \
        gDPSetRenderMode(gMainGfxPos[1]++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2); \
        gSPClearGeometryMode(gMainGfxPos[1]++, G_FOG);                      \
    }

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

void renderPokemonModelTypeI(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW, renRenderModelTypeI, renRenderModelTypeI)
}

void renderPokemonModelTypeIFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW_FOG, renRenderModelTypeI, renderModelTypeIFogged)
}

void renderPokemonModelTypeJFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW_FOG_XLU, renRenderModelTypeJ, renderModelTypeJFogged)
}

void renderPokemonModelTypeB(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW, renRenderModelTypeB, renRenderModelTypeB)
}

void renderPokemonModelTypeBFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW_FOG, renRenderModelTypeB, renderModelTypeBFogged)
}

void renderPokemonModelTypeD(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW, renRenderModelTypeD, renRenderModelTypeD)
}

void renderPokemonModelTypeDFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, SNAP_FADE_DRAW_FOG_XLU, renRenderModelTypeD, renderModelTypeDFogged)
}
