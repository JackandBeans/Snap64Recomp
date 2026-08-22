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
        presentation.crop[0] = presentation.crop[1] = presentation.crop[2] = presentation.crop[3] = 0;
        rect.fixRectLR = true;
        f3dex.forceBranch = false;
        s2dex.fixBilerpMismatch = true;
        s2dex.framebufferFastPath = true;
        textureLOD.scale = false;
    }
};