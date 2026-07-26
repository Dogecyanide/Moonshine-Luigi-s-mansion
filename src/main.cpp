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
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"
#include "susamune/features.hxx"
#include "susamune/warp.hxx"
#include "susamune/savestate.hxx"
#include "susamune/addresses.hxx"
#include "SMS/Manager/RumbleManager.hxx"

int gLoadMenu = 0;

SavestateManager* gSavestateMgr = nullptr;

// Replaces the game's OSGetArenaLo. The mod is linked into the bottom of the
// heap arena, [__OSArenaLo, __OSArenaLo + kArenaReserve); reporting the raised
// floor here keeps the root heap from allocating over it. The top of the arena
// is avoided because the apploader keeps the FST there. kArenaReserve must
// match mod_region_size in scripts/patches.py.
extern "C" void* getArenaLo() {
    const u32 kArenaReserve = 0x8000;
    return (void*)(*(volatile u32*)SUSAMUNE_ADDR_OS_ARENA_LO + kArenaReserve);
}

// Replaces the `bl TApplication::initialize` in main() (see patches.py). main()
// is only initialize(); proc(); finalize(), so this is the last point before
// the game's app-state machine starts running -- the logo, the progressive-mode
// prompt, the title screen and gameplay all live inside proc(). Settings have
// to be live by now: featuresApply() runs from onUpdate, which is hooked into
// gameLoop(), and proc() calls gameLoop() for the logo and title states too --
// so initialising settings any later (as onSetup used to) left every feature
// reading zeroed BSS for the whole boot sequence. Codes that patch the boot
// path itself, like Intro Skip, need it earlier still.
extern "C" void onAppInit(TApplication* app) {
    app->initialize();
    gSettings.init();
}

extern "C" u8 onUpdateGameMode(TMarDirector* director) {
    u8 state = director->updateGameMode();

#if ENABLE_MENU
    auto controller = gpApplication.mGamePads[0];

    // changing to pause menu state, and Y is held? don't pause
    if (director->mCurState != state && state == 0x5 && (controller->mButtons.mInput & TMarioGamePad::Y)) {
        state = director->mCurState;
    }

    if (Warp::pending()) {
        Warp::execute();
        // QF timer reset flag
        if (SUSAMUNE_ADDR_QF_TIMER_RESET != 0) {
            volatile u8* flag = reinterpret_cast<volatile u8*>(SUSAMUNE_ADDR_QF_TIMER_RESET);
            *flag = 1;
        }
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

    // Settings are already initialised, much earlier, by onAppInit.

    JKRHeap *oldHeap = JKRHeap::sSystemHeap->becomeCurrentHeap();
#if ENABLE_MENU
    menuInit();
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

    // Apply/restore the toggled memory-patch features (ported gecko codes).
    // Runs every frame like the gecko handler; no-ops when nothing changed.
    featuresApply();

    if (gSavestateMgr) {
        gSavestateMgr->updateHook(gpApplication.mGamePads[0]);
    }
#if ENABLE_MENU
    if (gMenu) {
        gMenu->update(gpApplication.mGamePads[0]);
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
    // The original call is a full GXDrawDone barrier. Process queued loads
    // immediately afterward: director, fader, audio, and the current frame's
    // GPU work are all complete, while the next game frame has not begun.
    THPPlayerDrawDone();
    if (gSavestateMgr)
        gSavestateMgr->processPendingLoad();
    {
        J2DOrthoGraph ortho(0, 0, 640, 480);
        ortho.setup2D();

        GXSetViewport(0, 0, 640, 480, 0, 1);
        {
            Mtx44 mtx;
            C_MTXOrtho(mtx, 0, 480,0, 640, -1, 1);
            GXSetProjection(mtx, GX_ORTHOGRAPHIC);
        }        
        
#if ENABLE_MENU
        if (gMenu)
            gMenu->draw(&ortho);
#endif
        if (gSavestateMgr)
            gSavestateMgr->draw(&ortho);
    }
}
