#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Player/Mario.hxx>

class TMenuDirector : public JDrama::TDirector {
public:
    TMenuDirector();
    virtual ~TMenuDirector();

    s32 direct() override;

    void setFixedStageValue();
    void rsetup();
    s32 setup(JDrama::TDisplay *, TMarioGamePad *);
    void setupThreadFunc(void *);

    bool _18;
    u32 _1C;
    u32 _20;
    u32 _24;
    u32 _28;
    u32 _2C;
    u16 _30;
    f32 mVSyncTimesPerSec;
    u32 _38;
    u32 _3C;
    u32 _40;
    u32 _44;
    u32 _48;
    u32 _4C;
    u8 _50;
    u32 _54;
};