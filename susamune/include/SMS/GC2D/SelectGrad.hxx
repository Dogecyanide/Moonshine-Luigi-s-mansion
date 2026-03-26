#pragma once

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JUtility/JUTColor.hxx>

class TSelectGrad : public JDrama::TViewObj {
public:
    TSelectGrad(const char *name);
    virtual ~TSelectGrad();

    virtual void perform(u32, JDrama::TGraphics *) override;

    void setStageColor(u8 stageID);

    u32 _10;
    u32 _14;
    u32 _18;
    JUtility::TColor mColorX1;
    JUtility::TColor mColorX2;
};