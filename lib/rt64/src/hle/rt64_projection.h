//
// RT64
//

#pragma once

#include "shared/rt64_point_light.h"

#include "rt64_game_call.h"
#include "rt64_light_manager.h"

namespace RT64 {
    struct Projection {
        enum class Type {
            None,
            Perspective,
            Orthographic,
            Rectangle,
            Triangle
        };

        Type type;
        uint32_t transformsIndex;
        std::vector<GameCall> gameCalls;
        uint32_t gameCallCount;
        LightManager lightManager;
        std::vector<interop::PointLight> pointLights;
        uint32_t pointLightCount;
        FixedRect scissorRect;
        bool used;

        // Pokemon Snap port: the projection's scissor blended between the
        // matched previous frame and this one, recomputed per sub-frame by the
        // projection processor. The game animates its camera viewport -- the
        // photo mode shrinks the scene into an inset over several ticks -- and
        // a scissor that steps at the game's rate crops a smoothly scaling
        // scene to a stepping edge. Only valid on sub-frames where the view
        // itself interpolated; a cut snaps this along with everything else.
        FixedRect snapBlendedScissor;
        bool snapScissorBlended;

        void reset();
        void addGameCall(const GameCall &gameCall);
        void addPointLight(const interop::PointLight &light);
        bool usesViewport() const;
    };
};