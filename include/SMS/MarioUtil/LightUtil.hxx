#pragma once

#include <JDrama/JDRViewObj.hxx>
#include <JGeometry/JGMVec.hxx>
#include <JUtility/JUTColor.hxx>

class TLightCommon : public JDrama::TViewObj {
public:
    TLightCommon(const char *name);
    ~TLightCommon() override;

    void loadAfter() override;
    void perform(u32, JDrama::TGraphics *) override;

    virtual JUtility::TColor getAmbColor(int index) const;
    virtual JUtility::TColor getLightColor(int index) const;
    virtual TVec3f getLightPosition(int index);

    // Applies the light to the graphics object
    virtual void setLight(const JDrama::TGraphics *graphics, int index);

    f32 mSoftness;  // _10
    TVec3f _14;     // _14
    int mGlobalAmbColorIndex;
    int mGlobalLightColorIndex;
    bool mUseLocalLights;                // _28
    JUtility::TColor mAmbColorAry[2];    // _29
    JUtility::TColor mLightColorAry[4];  // _31
    bool _41;
    TVec3f mLightPosAry[4];  // _44

    static void *mAmbAry;
    static void *mLightAry;
    static void *mLightPos;
} /*__attribute__((packed))*/;