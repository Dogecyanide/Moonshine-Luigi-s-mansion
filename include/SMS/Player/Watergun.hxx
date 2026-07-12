#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <SMS/Player/NozzleBase.hxx>
#include <SMS/Player/NozzleTrigger.hxx>
#include <SMS/System/Params.hxx>

class TMario;
class TMarioControllerWork;

class TWaterGun {
public:
    enum TNozzleType : u8 { Spray, Rocket, Underwater, Yoshi, Hover, Turbo, Sniper };

    TWaterGun(TMario *);

    virtual void perform(u32, JDrama::TGraphics *);

    void calcAnimation(JDrama::TGraphics *);
    void changeBackup();
    void changeNozzle(TNozzleType, bool);
    bool damage();
    void emit();
    TNozzleType getCurrentNozzle() const;
    Mtx *getEmitMtx(int);
    void getEmitPosDirSpeed(int, TVec3f *pos, TVec3f *dir, TVec3f *speed);
    Mtx *getNozzleMtx();
    f32 getPressure();
    f32 getPressureMax();
    void init();
    void initInLoadAfter();
    bool isEmitting();
    bool isPressureOn();
    void movement();
    void rotateProp(f32);
    void setAmountToRate(f32);
    void setBaseTRMtx(Mtx);
    void suck();
    void triggerPressureMovement(const TMarioControllerWork &);

    u32 _04;                                // 0x0000
    TMario *mMario;                         // 0x0008
    TNozzleBase mNozzleDeform;              // 0x000C
    TNozzleTrigger mNozzleDeformBomb;       // 0x0390
    TNozzleTrigger mNozzleRocket;           // 0x0720
    TNozzleBase mNozzleUnderWater;          // 0x0AB0
    TNozzleBase mNozzleYoshiDeform;         // 0x0E34
    TNozzleTrigger mNozzleYoshiDeformBomb;  // 0x11B8
    TNozzleTrigger mNozzleHover;            // 0x1548
    TNozzleTrigger mNozzleTurbo;            // 0x18D8
    TNozzleBase *mNozzleList[6];            // 0x1C68
    s32 mCurrentWater;                      // 0x1C80
    u8 mCurrentNozzle;                      // 0x1C84
    u8 mSecondNozzle;                       // 0x1C85
    bool mIsEmitWater;                      // 0x1C86
    u8 _13;                                 // 0x1C87
    u32 _14[0x8 / 4];                       // 0x1C88
    TVec3f mGeometry[10];                   // 0x1C90
    u32 _15;                                // 0x1D08
    TWaterEmitInfo *mEmitInfo;              // 0x1D0C
    u32 _16;                                // 0x1D10
    char *mPrmPath;                         // 0x1D14
    char *mRocketPath;                      // 0x1D18
    u32 _17[0x24 / 4];                      // 0x1D1C
    float mHoverMaxHeight;                  // 0x1D40
    u32 _18[0x78 / 4];                      // 0x1D44
                                            // 1DBC
};