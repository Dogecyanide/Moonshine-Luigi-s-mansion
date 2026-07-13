#include "Dolphin/GX_types.h"
#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "J2D/J2DTextBox.hxx"
#include "JKernel/JKRHeap.hxx"
#include "JUtility/JUTGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "JSystem/J2D/J2DPane.hxx"
#include "JSystem/J2D/J2DPicture.hxx"
#include "JSystem/J2D/J2DOrthoGraph.hxx"
#include "Dolphin/THP.h"
#include "susamune/settings_menu.hxx"
#include "susamune/savestate.hxx"
#include "SMS/Manager/RumbleManager.hxx"

int gLoadMenu = 0;

SettingsMenu* gSettingsMenu = nullptr;
SavestateManager* gSavestateMgr = nullptr;

extern "C" u8 onUpdateGameMode(TMarDirector* director) {
    u8 state = director->updateGameMode();

#if ENABLE_WARP_MENU
    auto controller = gpApplication.mGamePads[0];

    // changing to pause menu state, and Y is held? don't pause
    if (director->mCurState != state && state == 0x5 && (controller->mButtons.mInput & TMarioGamePad::Y)) {
        state = director->mCurState;
    }

    if (gSettingsMenu && gSettingsMenu->mChangeStageReady) {
        gSettingsMenu->changeStageHook();
        gSettingsMenu->mChangeStageReady = false;
        // QF timer reset flag
        volatile u8* flag = ((volatile u8*)(0x817f00b3));
        *flag = 1;
        director->moveStage();
        state = 9;
    }
#endif

    // to load the developer stage warp menu
    //if ((controller->mButtons.mInput & TMarioGamePad::X) && (controller->mButtons.mInput & TMarioGamePad::Y)) {
    //    gLoadMenu = 1;
    //    state = 12; 
    //}
    return state;
}

// extern "C" void onFinishAppState(RumbleMgr* rumble) {
//     rumble->init();
// }

// TODO: maybe this isnt really the init hook we want.. this runs every time a stage loads
extern "C" void onSetup(TMarDirector* director) {
    static bool inited = false;
    director->setupObjects();
    
    if (inited) return; else inited = true;

    JKRHeap *oldHeap = JKRHeap::sSystemHeap->becomeCurrentHeap();
#if ENABLE_WARP_MENU
    gSettingsMenu = new SettingsMenu();
#endif
    gSavestateMgr = new SavestateManager();
    
    if (oldHeap) {
        oldHeap->becomeCurrentHeap();
    } else {
        JKRHeap::sCurrentHeap = nullptr;
    }
}


extern "C" s32 onUpdate(JDrama::TDirector* director) {    
    int state = director->direct();

    if (gSavestateMgr) {
        gSavestateMgr->updateHook(gpApplication.mGamePads[0]);
    }
#if ENABLE_WARP_MENU
    if (gSettingsMenu) {
        gSettingsMenu->processInput(gpApplication.mGamePads[0]);
    }
#endif

    if (gLoadMenu) {
        gLoadMenu = 0;
        return 9;
    } else {
        return state;
    }
}

extern "C" void afterDraw() {
    THPPlayerDrawDone();
    {
        J2DOrthoGraph ortho(0, 0, 640, 480);
        ortho.setup2D();

        GXSetViewport(0, 0, 640, 480, 0, 1);
        {
            Mtx44 mtx;
            C_MTXOrtho(mtx, 0, 480,0, 640, -1, 1);
            GXSetProjection(mtx, GX_ORTHOGRAPHIC);
        }        
        
#if ENABLE_WARP_MENU
        if (gSettingsMenu)
            gSettingsMenu->draw(&ortho);
#endif
        if (gSavestateMgr)
            gSavestateMgr->draw(&ortho);
    }
}