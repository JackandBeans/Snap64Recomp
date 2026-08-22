//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
    struct EnhancementConfiguration {
        struct Framebuffer {
            bool reinterpretFixULS;
        };

        struct Presentation {
            enum class Mode {
                Console,
                SkipBuffering,
                PresentEarly
            };

            Mode mode;
            bool removeBlackBorders;

            // Pokemon Snap port: framebuffer pixels to hide per side, the way
            // a CRT's overscan did. The game never draws its full buffer --
            // gameplay leaves dead margins of up to 16 pixels and the intro's
            // cinematics up to 30 on the left -- and its single VI mode makes
            // no attempt to compensate, because in 1999 every television
            // cropped the edges. Zero shows the raw buffer. One shape from
            // here to the shader parameters: left, right, top, bottom.
            uint32_t crop[4];
        };
        
        struct Rect {
            bool fixRectLR;
        };

        struct F3DEX {
            bool forceBranch;
        };

        struct S2DEX {
            bool fixBilerpMismatch;
            bool framebufferFastPath;
        };

        struct TextureLOD {
            bool scale;
        };

        Framebuffer framebuffer;
        Presentation presentation;
        Rect rect;
        F3DEX f3dex;
        S2DEX s2dex;
        TextureLOD textureLOD;

        EnhancementConfiguration();
    };
};