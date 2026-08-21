//
// RT64
//

#include "rt64_enhancement_configuration.h"

namespace RT64 {
    // EnhancementConfiguration
    
    EnhancementConfiguration::EnhancementConfiguration() {
        framebuffer.reinterpretFixULS = true;
        presentation.mode = Presentation::Mode::SkipBuffering;
        presentation.removeBlackBorders = true;
        presentation.cropLeft = 0;
        presentation.cropRight = 0;
        presentation.cropTop = 0;
        presentation.cropBottom = 0;
        rect.fixRectLR = true;
        f3dex.forceBranch = false;
        s2dex.fixBilerpMismatch = true;
        s2dex.framebufferFastPath = true;
        textureLOD.scale = false;
    }
};