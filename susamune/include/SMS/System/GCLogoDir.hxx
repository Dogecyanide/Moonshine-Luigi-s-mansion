#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Player/Mario.hxx>

class TNintendo2D : public JDrama::TViewObj {
    void perform(u32, JDrama::TGraphics *) override;
};

class TGCLogoDir : public JDrama::TDirector {
public:
    TGCLogoDir();
    virtual ~TGCLogoDir();

    s32 direct() override;
    s32 direct_dolby();
    s32 direct_nlogo();
    s32 setup(JDrama::TDisplay *, TMarioGamePad *);

    u32 _18;
    u32 _1C;
    u32 _20;
    u32 _24;
    f32 mVSyncTimesPerSec;
    u32 _2C;
    u32 _30;
    u32 _34;
    u32 _38;
    u32 _3C;
    u32 _40;
    u16 _44;
};
