#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JDrama/JDRScreen.hxx>
#include <JSystem/JKernel/JKRMemArchive.hxx>
#include <JSystem/JParticle/JPAResourceManager.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/Player/MarioGamePad.hxx>
#include <SMS/GC2D/SelectMenu.hxx>
#include <SMS/GC2D/SelectGrad.hxx>
#include <SMS/Manager/SelectShineManager.hxx>

class TSelectDir : public JDrama::TDirector {
public:
    TSelectDir();
    virtual ~TSelectDir();

    s32 direct() override;

    void changeOrder();
    void rsetup();
    s32 setup(JDrama::TDisplay *display, TMarioGamePad *controller, u8 areaID);
    void setupThreadFunc(void *);

    TMarioGamePad *mController;
    JDrama::TDisplay *mDisplay;
    TSelectMenu *mMenu;
    TSelectGrad *mGradient;
    TSelectShineManager *mManager;
    JKRMemArchive *mArchive;
    void *mEmitterManager1;  // JPAEmitterManager *
    void *mEmitterManager2;  // JPAEmitterManager *
    bool mIsInitialized;
    u32 _3C;
    u8 mAreaID;
    JDrama::TScreen *mScreen1;
    JDrama::TScreen *mScreen2;
    bool mIsResetting;
};