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

/* The fade bracket's display lists (pokemon.c's fader uses these). */
extern Gfx D_8038A3D0_52A7E0[];
extern Gfx D_8038A400_52A810[];

extern Gfx* gMainGfxPos[];

/* The game's own fade composition, per geometry type: the bracket makes the
 * blender read the fog color, the fog color carries the model's uniform
 * alpha, and the restore puts the pipeline back. The inner renderer is the
 * unfogged one -- the same choice the game's TypeI fader makes even for
 * species that normally draw fogged. */
#define SNAP_FADE_DRAW(obj, inner)                                          \
    {                                                                       \
        gSPDisplayList(gMainGfxPos[0]++, D_8038A3D0_52A7E0);                \
        gDPSetFogColor(gMainGfxPos[0]++, 230, 250, 180,                     \
                       GET_POKEMON(obj)->unk_E4);                           \
        inner(obj);                                                         \
        gSPDisplayList(gMainGfxPos[0]++, D_8038A400_52A810);                \
    }

#define SNAP_POKEMON_RENDER(obj, inner, stock)                              \
    {                                                                       \
        if (!Pokemon_GetFlag100(obj) && !PokemonDetector_ReturnFalse(obj)) {\
            if (GET_POKEMON(obj)->unk_E4 != 255) {                          \
                SNAP_FADE_DRAW(obj, inner)                                  \
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
    SNAP_POKEMON_RENDER(obj, renRenderModelTypeI, renRenderModelTypeI)
}

void renderPokemonModelTypeIFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, renRenderModelTypeI, renderModelTypeIFogged)
}

void renderPokemonModelTypeJFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, renRenderModelTypeJ, renderModelTypeJFogged)
}

void renderPokemonModelTypeB(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, renRenderModelTypeB, renRenderModelTypeB)
}

void renderPokemonModelTypeBFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, renRenderModelTypeB, renderModelTypeBFogged)
}

void renderPokemonModelTypeD(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, renRenderModelTypeD, renRenderModelTypeD)
}

void renderPokemonModelTypeDFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj, renRenderModelTypeD, renderModelTypeDFogged)
}
