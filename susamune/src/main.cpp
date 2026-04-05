#include "Dolphin/GX_types.h"
#include "J2D/J2DTextBox.hxx"
#include "JKernel/JKRHeap.hxx"
#include "SMS/System/Application.hxx"
#include "JSystem/J2D/J2DPane.hxx"
#include "JSystem/J2D/J2DPicture.hxx"
#include "JSystem/J2D/J2DOrthoGraph.hxx"
#include "Dolphin/THP.h"


int g_load_menu = 0;

J2DTextBox *g_textbox = nullptr; 
J2DTextBox *g_textbox2 = nullptr;
bool g_textbox_init = false;

extern "C" u8 onUpdateGameMode(TMarDirector* director) {
    u8 state = director->updateGameMode();

    auto controller = gpApplication.mGamePads[0];
    if ((controller->mButtons.mInput & TMarioGamePad::X) && (controller->mButtons.mInput & TMarioGamePad::Y)) {
        g_load_menu = 1;
        state = 12; 
    }
    return state;
}

// TODO: this isnt really the init hook we want.. this runs every time a stage loads
extern "C" void onSetup(TMarDirector* director) {
    static bool inited = false;
    director->setupObjects();
    
    // TODO: is this sufficient to avoid re-calling?
    // seems this function will run once at the main menu
    // but we'd also want to destroy stuff at reset
    if (inited) return; else inited = true;

    g_textbox = new J2DTextBox(gpSystemFont->mFont, "test1");
    g_textbox->mCharSizeX = 50;
    g_textbox->mCharSizeY = 50;
    g_textbox->mGradientBottom = {0,255,0,255};
    g_textbox->mGradientTop = {0,0,255,255};

    g_textbox2 = new J2DTextBox(gpSystemFont->mFont, "test2");
    g_textbox2->mCharSizeX = 40;
    g_textbox2->mCharSizeY = 40;
    g_textbox2->mGradientBottom = {255,0,0,255};
    g_textbox2->mGradientTop = {255,255,0,255};
}

extern "C" s32 onUpdate(JDrama::TDirector* director) {    
    int state = director->direct();

    if (g_load_menu) {
        g_load_menu = 0;
        return 9;
    } else {
        return state;
    }
}

extern "C" void afterDraw() {
    THPPlayerDrawDone();
    {
        J2DOrthoGraph ortho(0, 0, 640, 448);
        ortho.setup2D();

        GXSetViewport(0, 0, 640, 480, 0, 1);
        {
            Mtx44 mtx;
            C_MTXOrtho(mtx, 16, 496, -20, 620, -1, 1);
            GXSetProjection(mtx, GX_ORTHOGRAPHIC);
        }

        J2DFillBox(0, 100, 100, 100, {255, 0, 0, 255});

        if (g_textbox && g_textbox2) {
            g_textbox->draw(50, 100);
            g_textbox2->draw(0, 400);
        }
    }
}