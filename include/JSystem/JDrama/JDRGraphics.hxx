#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRRect.hxx>

namespace JDrama {

    class TGraphics {
    public:
        void setViewport(const TRect &, f32, f32);

        u8 _00[0x20];
        bool _20;
        u8 _21[0x54 - 0x21];
        TRect mViewPortSpace;
    };

}  // namespace JDrama