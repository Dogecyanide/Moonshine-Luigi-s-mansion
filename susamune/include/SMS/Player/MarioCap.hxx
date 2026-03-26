#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>

class TMario;

class TMarioCap {
public:
    TMarioCap(TMario *);

    virtual void perform(u32, JDrama::TGraphics *);

    void createMirrorModel();
    void mtxEffectHide();
    void mtxEffectShow();

    u32 _00;
    u32 _04;
    u16 _08;
    J3DModel *mCap1;
    J3DModel *mCap3;
    J3DModel *mDiverHelm;
    J3DModel *maGlass1;
};

extern const char *cDirtyFileName;
extern const char *cDirtyTexName;