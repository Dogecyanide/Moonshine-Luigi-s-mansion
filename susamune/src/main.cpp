#include "Dolphin/GX_types.h"
#include "Dolphin/OS.h"
#include "J2D/J2DTextBox.hxx"
#include "JKernel/JKRHeap.hxx"
#include "SMS/System/Application.hxx"
#include "JSystem/J2D/J2DPane.hxx"
#include "JSystem/J2D/J2DPicture.hxx"
#include "JSystem/J2D/J2DOrthoGraph.hxx"
#include "Dolphin/THP.h"
#include "susamune/settings_menu.hxx"
#include "SMS/Manager/RumbleManager.hxx"

int gLoadMenu = 0;

SettingsMenu* gSettingsMenu = nullptr;

extern "C" u8 onUpdateGameMode(TMarDirector* director) {
    u8 state = director->updateGameMode();

    auto controller = gpApplication.mGamePads[0];

    if (gSettingsMenu && gSettingsMenu->mChangeStageReady) {
        gSettingsMenu->changeStageHook();
        gSettingsMenu->mChangeStageReady = false;
        // QF timer reset flag
        volatile u8* flag = ((volatile u8*)(0x817f00b3));
        *flag = 1;
        director->moveStage();
        state = 9;
    }

    if ((controller->mButtons.mInput & TMarioGamePad::X) && (controller->mButtons.mInput & TMarioGamePad::Y)) {
        gLoadMenu = 1;
        state = 12; 
    }
    return state;
}

extern "C" void onFinishAppState(RumbleMgr* rumble) {
    rumble->init();
}

// TODO: this isnt really the init hook we want.. this runs every time a stage loads
extern "C" void onSetup(TMarDirector* director) {
    //static bool inited = false;
    director->setupObjects();
    
    // TODO: is this sufficient to avoid re-calling?
    // seems this function will run once at the main menu
    // but we'd also want to destroy stuff at reset
    // nope. dont do this. everything allocated here will be destroyed
    // at the next setup call, and it will be reinitialized.
    // so TODO: hook into destruction code? or we need to find a hook 
    // where the heap is initialized, but not set to some arena that will 
    // be cleared. 
    //if (inited) return; else inited = true;

    //g_textbox = new J2DTextBox(gpSystemFont->mFont, "test1");
    //g_textbox->mCharSizeX = 50;
    //g_textbox->mCharSizeY = 50;
    //g_textbox->mGradientBottom = {0,255,0,255};
    //g_textbox->mGradientTop = {0,0,255,255};
//
    //g_textbox2 = new J2DTextBox(gpSystemFont->mFont, "test2");
    //g_textbox2->mCharSizeX = 40;
    //g_textbox2->mCharSizeY = 40;
    //g_textbox2->mGradientBottom = {255,0,0,255};
    //g_textbox2->mGradientTop = {255,255,0,255};

    gSettingsMenu = new SettingsMenu();
}

extern "C" s32 onUpdate(JDrama::TDirector* director) {    
    int state = director->direct();

    if (gSettingsMenu) {
        gSettingsMenu->processInput(gpApplication.mGamePads[0]);
    }

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

        if (gSettingsMenu)
            gSettingsMenu->draw(&ortho);
    }
}