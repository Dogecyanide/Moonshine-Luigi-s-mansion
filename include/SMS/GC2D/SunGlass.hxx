#pragma once

#include <JDrama/JDRRect.hxx>
#include <JDrama/JDRViewObj.hxx>
#include <JUtility/JUTColor.hxx>
#include <SMS/Player/MarioGamePad.hxx>

class TSunGlass : public JDrama::TViewObj {
public:
    TSunGlass(const char *);
    ~TSunGlass() override;

    void load(JSUMemoryInputStream &) override;
    void loadAfter() override;
    void perform(u32, JDrama::TGraphics *) override;

    virtual void draw(const JDrama::TRect &, JUtility::TColor color);

    TMarioGamePad *mGamepad;  // _10
    JUtility::TColor mColor;  // _14
    u16 _18;
    u16 mBlendFlags;    // _1A
    u8 mToAlpha;        // _1C
    u8 mFromAlpha;      // _1D
    u8 mSelectorA;      // _1E
    u8 mSelectorB;      // _1F
    u16 _20;            // _20
    u16 mLerpTimerMax;  // _22
    u16 mLerpTimer;     // _24
    bool mIsUpdating;   // _26
};