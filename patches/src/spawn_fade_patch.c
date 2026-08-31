/**
 * The spawn fade's game half: declare the fading model to the renderer.
 *
 * Six attempts to fade Pokemon by rewriting RDP state around their models
 * all failed in presented-frame captures -- fog silhouettes, black bodies,
 * untextured wing shards -- and a state-level mode dump proved the injected
 * state reaches the draw intact while the output is still wrong. Whatever
 * decides those pixels lives below the display list, so the fade lives
 * there too: the renderer blends the model itself (rt64_state.cpp forces
 * the translucent shader path, RasterPS.hlsl multiplies in the fade), and
 * the ONLY job of these wrappers is to declare which draws are fading and
 * how much.
 *
 * The declaration is G_EX_OBJECTFADE_V1, an RT64 extended display-list
 * command minted for this port: payload bit 8 arms the fade, bits 0-7
 * carry the alpha, zero ends the span. Between the begin and the end the
 * model draws through the UNTOUCHED stock composite -- fog setup, cycle
 * type, combiners, render modes, all byte-for-byte the pokemon_detect.c
 * originals, in the fading branch as much as the stock one. The TypeJ and
 * TypeD composites also queue their translucent pieces on the deferred
 * list, so that list gets the same bracket and those pieces fade with the
 * body.
 *
 * Only the port's spawn-fade module (src/spawn_fade.cpp) ever writes the
 * alpha byte below 255, gated on the Spawn Fade setting; at 255 every
 * wrapper takes the plain stock call with no commands emitted at all.
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

/* G_EX_OBJECTFADE_V1: RT64's extended opcode byte (0x64) carrying sub-op
 * 0x37 (lib/rt64/include/rt64_extended_gbi.h). */
#define SNAP_EX_OBJECTFADE(list, payload)                                   \
    {                                                                       \
        gMainGfxPos[list]->words.w0 = 0x64000037;                           \
        gMainGfxPos[list]->words.w1 = (payload);                            \
        gMainGfxPos[list]++;                                                \
    }

#define SNAP_FADE_BEGIN(obj, list)                                          \
    SNAP_EX_OBJECTFADE(list, 0x100 | GET_POKEMON(obj)->unk_E4)
#define SNAP_FADE_END(list)                                                 \
    SNAP_EX_OBJECTFADE(list, 0)

/* The stock wrapper shape shared by all seven (pokemon_detect.c), with the
 * fading branch drawing the SAME stock body inside the fade span. */
#define SNAP_POKEMON_RENDER(obj, fadeBody, stockBody)                       \
    {                                                                       \
        if (!Pokemon_GetFlag100(obj) && !PokemonDetector_ReturnFalse(obj)) {\
            if (GET_POKEMON(obj)->unk_E4 != 255) {                          \
                fadeBody                                                    \
            }                                                               \
            else {                                                          \
                stockBody                                                   \
            }                                                               \
            if (PokemonDetector_IsEnabled) {                                \
                PokemonDetector_SaveRegion(obj);                            \
            }                                                               \
        }                                                                   \
    }

void renderPokemonModelTypeI(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_BEGIN(obj, 0); renRenderModelTypeI(obj); SNAP_FADE_END(0); },
        { renRenderModelTypeI(obj); })
}

void renderPokemonModelTypeIFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_BEGIN(obj, 0); renderModelTypeIFogged(obj); SNAP_FADE_END(0); },
        { renderModelTypeIFogged(obj); })
}

void renderPokemonModelTypeJFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_BEGIN(obj, 0); SNAP_FADE_BEGIN(obj, 1);
          renderModelTypeJFogged(obj);
          SNAP_FADE_END(0); SNAP_FADE_END(1); },
        { renderModelTypeJFogged(obj); })
}

void renderPokemonModelTypeB(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_BEGIN(obj, 0); renRenderModelTypeB(obj); SNAP_FADE_END(0); },
        { renRenderModelTypeB(obj); })
}

void renderPokemonModelTypeBFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_BEGIN(obj, 0); renderModelTypeBFogged(obj); SNAP_FADE_END(0); },
        { renderModelTypeBFogged(obj); })
}

void renderPokemonModelTypeD(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_BEGIN(obj, 0); renRenderModelTypeD(obj); SNAP_FADE_END(0); },
        { renRenderModelTypeD(obj); })
}

void renderPokemonModelTypeDFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_BEGIN(obj, 0); SNAP_FADE_BEGIN(obj, 1);
          renderModelTypeDFogged(obj);
          SNAP_FADE_END(0); SNAP_FADE_END(1); },
        { renderModelTypeDFogged(obj); })
}
