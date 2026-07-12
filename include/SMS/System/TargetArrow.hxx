#pragma once

#include <JSystem/JDrama/JDRViewObj.hxx>

#include <SMS/M3DUtil/MActor.hxx>

class TTargetArrow : public JDrama::TViewObj {
public:
    TTargetArrow(const char *name) : JDrama::TViewObj(name) {
        mActor     = nullptr;
        mIsVisible = false;
    }

    void loadAfter() override;
    void perform(u32, JDrama::TGraphics *) override;

    void setPos(const TVec3f &pos);

    u16 _0E;
    MActor *mActor;
    bool mIsVisible;
};

extern TTargetArrow *gpTargetArrow;