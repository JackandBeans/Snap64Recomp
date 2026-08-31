/**
 * The spawn fade's game half: name the fading model to the renderer.
 *
 * Six attempts to fade Pokemon by rewriting RDP state around their models
 * all failed in presented-frame captures -- fog silhouettes, black bodies,
 * untextured wing shards -- and a state-level mode dump proved the injected
 * state reaches the draw intact while the output is still wrong. Whatever
 * decides those pixels lives below the display list, so the fade now lives
 * there too: the renderer itself blends the model (rt64_state.cpp forces
 * the translucent shader path, RasterPS.hlsl multiplies in the fade), and
 * the ONLY job of these wrappers is to tell it which draws are fading and
 * how much.
 *
 * That message travels in the fog color register: (230, 250, 180) -- the
 * palette of the game's own dormant fader, a value no course uses -- with
 * the fade level in its alpha. Writing a fog color is semantically inert
 * here: these models sit far inside the fog band's near edge (968 of 990),
 * so the fog factor is zero and no visible fog math changes. For the
 * fogged composites the marker is written AFTER the game's own enableFog
 * (which sets the course fog color), and the course color is restored from
 * the game's own live sFog globals afterwards, so nothing drawn later can
 * pick the marker up (a far bush once did, and wore it as a pale blob).
 *
 * Everything else -- cycle types, combiners, render modes, the detector
 * pass -- is byte-for-byte the stock pokemon_detect.c behavior, in both
 * branches. Only the port's spawn-fade module (src/spawn_fade.cpp) ever
 * writes the alpha byte below 255, gated on the Spawn Fade setting.
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

/* The fog environment pieces (src/app_render/4CDB0.c). */
void enableFog(GObj* obj);
void disableFog(GObj* obj);
void enableFogTrasparent(GObj* obj);
void disableFogTransparent(GObj* obj);
extern u8 sFogR;
extern u8 sFogG;
extern u8 sFogB;

extern Gfx* gMainGfxPos[];

#define SNAP_FADE_MARK(obj)                                                 \
    gDPSetFogColor(gMainGfxPos[0]++, 230, 250, 180, GET_POKEMON(obj)->unk_E4)
#define SNAP_FADE_UNMARK()                                                  \
    gDPSetFogColor(gMainGfxPos[0]++, sFogR, sFogG, sFogB, 0)

/* The stock wrapper shape shared by all seven (pokemon_detect.c), with the
 * body parameterised: the fading branch draws the same pipeline state the
 * stock branch does, plus the marker around the model. */
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
        { SNAP_FADE_MARK(obj); renRenderModelTypeI(obj); SNAP_FADE_UNMARK(); },
        { renRenderModelTypeI(obj); })
}

void renderPokemonModelTypeIFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { enableFog(obj); SNAP_FADE_MARK(obj); renRenderModelTypeI(obj);
          disableFog(obj); SNAP_FADE_UNMARK(); },
        { enableFog(obj); renRenderModelTypeI(obj); disableFog(obj); })
}

void renderPokemonModelTypeJFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { enableFog(obj); enableFogTrasparent(obj); SNAP_FADE_MARK(obj);
          renRenderModelTypeJ(obj);
          disableFog(obj); disableFogTransparent(obj); SNAP_FADE_UNMARK(); },
        { enableFog(obj); enableFogTrasparent(obj); renRenderModelTypeJ(obj);
          disableFog(obj); disableFogTransparent(obj); })
}

void renderPokemonModelTypeB(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_MARK(obj); renRenderModelTypeB(obj); SNAP_FADE_UNMARK(); },
        { renRenderModelTypeB(obj); })
}

void renderPokemonModelTypeBFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { enableFog(obj); SNAP_FADE_MARK(obj); renRenderModelTypeB(obj);
          disableFog(obj); SNAP_FADE_UNMARK(); },
        { enableFog(obj); renRenderModelTypeB(obj); disableFog(obj); })
}

void renderPokemonModelTypeD(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { SNAP_FADE_MARK(obj); renRenderModelTypeD(obj); SNAP_FADE_UNMARK(); },
        { renRenderModelTypeD(obj); })
}

void renderPokemonModelTypeDFogged(GObj* obj) {
    SNAP_POKEMON_RENDER(obj,
        { enableFog(obj); enableFogTrasparent(obj); SNAP_FADE_MARK(obj);
          renRenderModelTypeD(obj);
          disableFog(obj); disableFogTransparent(obj); SNAP_FADE_UNMARK(); },
        { enableFog(obj); enableFogTrasparent(obj); renRenderModelTypeD(obj);
          disableFog(obj); disableFogTransparent(obj); })
}
