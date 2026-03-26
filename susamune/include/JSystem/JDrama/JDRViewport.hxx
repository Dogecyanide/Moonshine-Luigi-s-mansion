#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>

namespace JDrama {

    class TViewport : public TViewObj {
    public:
        TViewport(const TRect &, const char *);
        virtual ~TViewport();

        virtual void load(JSUMemoryInputStream &stream);
        virtual void perform(u32, TGraphics *);

        TRect mViewportRect;  // 10
        u16 _20;
    };

}  // namespace JDrama