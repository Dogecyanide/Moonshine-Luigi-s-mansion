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

// Replaces the `bl TApplication::initialize` in main() (see patches.py), the
// last point before proc() starts the app-state machine. Settings must be live
// by here: proc() runs gameLoop() -- and so featuresApply() -- for the logo and
// title states too, and Intro Skip below patches the boot path itself.
extern "C" void onAppInit(TApplication* app) {
    app->initialize();
    gSettings.init();

    // Intro Skip. Boot runs BOOT -> NLOGO -> intro-movie -> stage(AREA_OPTION),
    // where that last stage IS the title screen. Both halves are set up here
    // because proc() has not started yet and neither is reachable from the
    // per-frame hooks: onUpdate replaces only the *general* director->direct()
    // call in gameLoop, and the logo/intro states run before it ever fires.
    if (gSettings.getBool(SETTING_INTRO_SKIP)) {
        // 1. Logos. gameLoop directs the logo director only while bit 0 of
        //    sGameInit is clear and leaves NLOGO once sGameInit == 3 (bit 0 =
        //    logo done, bit 1 = setup thread joined), so presetting bit 0 means
        //    the logo never runs. The NLOGO/BOOT states themselves must still
        //    happen: their post-gameLoop tails call initialize_boot/nlogoAfter,
        //    which create the stage heap. Side effect (as upstream documents):
        //    the progressive/60Hz prompt lives in direct_nlogo() and is lost.
        *(volatile u32*)SUSAMUNE_ADDR_GAME_INIT_FLAGS |= 1;

        // 2. Intro movie. Repoint proc()'s app-state jump table so the
        //    intro-movie state dispatches straight into the stage case,
        //    bypassing the TMovieDirector entirely. Do NOT instead let the
        //    movie run and rewrite the state afterwards: that path also runs
        //    the movie state's own body, which overwrites mNextScene.
        *(volatile u32*)SUSAMUNE_ADDR_APP_STATE_JUMP_INTRO =
            SUSAMUNE_ADDR_APP_PROC_STAGE_CASE;

        // 3. The stage case reads mCurrentScene, which proc() fills from
        //    mNextScene at the end of every earlier iteration -- so staging
        //    AREA_OPTION here is what makes it load the title screen.
        gpApplication.mNextScene.mAreaID    = 15;  // AREA_OPTION
        gpApplication.mNextScene.mEpisodeID = 0;
        gpApplication.mNextScene.mFlag      = 0;
    }
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

    // Runs on every stage load, so this must stay above the once-only guard.
    featuresOnStageLoad();

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
