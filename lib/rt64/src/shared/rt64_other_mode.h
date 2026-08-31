//
// RT64
//

#pragma once

#include "rt64_hlsl.h"

#include "shared/rt64_f3d_defines.h"

#ifdef HLSL_CPU
namespace interop {
#endif
    struct OtherMode {
        uint L;
        uint H;

        uint alphaCompare() constmethod {
            return L & (3U << G_MDSFT_ALPHACOMPARE);
        }

        uint blenderInputs() constmethod {
            return (L >> 16) & 0xFFFF;
        }

        uint cycleType() constmethod {
            return H & (3U << G_MDSFT_CYCLETYPE);
        }

        uint combKey() constmethod {
            return H & (1U << G_MDSFT_COMBKEY);
        }

        uint cvgDst() constmethod {
            return L & (3U << 8);
        }

        bool clrOnCvg() constmethod {
            return L & CLR_ON_CVG;
        }

        bool cvgXAlpha() constmethod {
            return L & CVG_X_ALPHA;
        }

        bool alphaCvgSel() constmethod {
            return L & ALPHA_CVG_SEL;
        }

        bool forceBlend() constmethod {
            return L & FORCE_BL;
        }

        uint textPersp() constmethod {
            return H & (1U << G_MDSFT_TEXTPERSP);
        }

        uint textFilt() constmethod {
            return H & (3U << G_MDSFT_TEXTFILT);
        }

        uint textLOD() constmethod {
            return H & (1U << G_MDSFT_TEXTLOD);
        }

        uint textDetail() constmethod {
            return H & (3U << G_MDSFT_TEXTDETAIL);
        }

        uint textLUT() constmethod {
            return H & (3U << G_MDSFT_TEXTLUT);
        }

        uint alphaDither() constmethod {
            return H & (3U << G_MDSFT_ALPHADITHER);
        }

        uint rgbDither() constmethod {
            return H & (3U << G_MDSFT_RGBDITHER);
        }

        bool aaEn() constmethod {
            return L & AA_EN;
        }

        bool zCmp() constmethod {
            return L & Z_CMP;
        }

        bool zUpd() constmethod {
            return L & Z_UPD;
        }

        uint zMode() constmethod {
            return L & ZMODE_MASK;
        }

        uint zSource() constmethod {
            return L & (1U << G_MDSFT_ZSRCSEL);
        }
    };
#ifdef HLSL_CPU
};
#endif
#ifdef HLSL_CPU
namespace interop {
    // Pokemon Snap port: the forced-translucency rewrite applied to a draw's
    // otherMode L word by the spawn fade (rt64_state.cpp): a standard
    // translucent blend in both cycles, alpha-compare and coverage-cutout
    // bits preserved, everything else authored. Kept here so the state layer
    // and the shader warmer can never drift apart.
    static inline uint32_t snapFadeRewriteL(uint32_t L) {
        return (L & 0x3007u) | 0x00504A50u;
    }
};
#endif
