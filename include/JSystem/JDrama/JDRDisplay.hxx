#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRVideo.hxx>

namespace JDrama {

    class TDisplay {
    public:
        TDisplay(u16 retraceCount, void *, void *, const GXRenderModeObj &);

        void startRendering();
        void endRendering();

        bool _00;
        GXRenderModeObj mRenderObj;
        u8 _40[0xC];
        u16 mRetraceCount;
        u16 _4E;
        u32 _50[0x10 / 4];
        JDrama::TVideo *mVideo;  // 0x0060
        u16 _64;
    };

}  // namespace JDrama