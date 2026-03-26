#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRRect.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>

namespace JDrama {
    class TEfbCtrl : public TViewObj {
    public:
        TEfbCtrl(const char *name) : TViewObj(name) {}
        ~TEfbCtrl() override;

        void perform(u32, JDrama::TGraphics *) override;

        void setSrcRect(const TRect &);

        TRect mSrcRect;
    };

    class TEfbCtrlTex final : public TEfbCtrl {
    public:
        TEfbCtrlTex(const char *name, TFlagT<u16>);
        ~TEfbCtrlTex() override;

        void perform(u32, JDrama::TGraphics *) override;

        void setTexAttb(const GXTexObj &);

        u16 mFlags;
        size_t mTexSizeS;
        size_t mTexSizeT;
        GXTexObj mTexObj;
    };

    class TEfbCtrlDisp final : public TEfbCtrl {
    public:
        TEfbCtrlDisp() : TEfbCtrl("<TEfbCtrlDisp>") {}
        ~TEfbCtrlDisp() override;

        void perform(u32, JDrama::TGraphics *) override;
    };
}  // namespace JDrama