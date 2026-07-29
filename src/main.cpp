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
#include "susamune/actions.hxx"
#include "susamune/binds.hxx"
#if ENABLE_DEBUG_WARPS
#include "susamune/warp.hxx"
#endif
#include "susamune/savestate.hxx"
#include "susamune/addresses.hxx"
#include "SMS/Manager/RumbleManager.hxx"

SavestateManager* gSavestateMgr = nullptr;

// Replaces the game's OSGetArenaLo. The mod is linked into the bottom of the
// heap arena; reporting the raised floor here keeps the root heap from
// allocating over it. The top is avoided because the apploader keeps the FST
// there.
//
// The reserve is SUSAMUNE_ARENA_RESERVE_SIZE, not the region size: __OSArenaLo
// sits a debug stack below the __ArenaLo the blob links at. Adding only the
// region size puts the heap floor at MOD_BASE + 0x6000, inside the blob.
// SUSAMUNE_ARENA_RESERVE_SIZE must match arena_reserve in scripts/patches.py.
extern "C" void* getArenaLo() {
    return (void*)(*(volatile u32*)SUSAMUNE_ADDR_OS_ARENA_LO +
                   SUSAMUNE_ARENA_RESERVE_SIZE);
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

    // Opening the menu must not also pause the game. The default menu bind
    // includes Start, which is what the director is reacting to here, so
    // swallow the transition into the pause state on the frame it fires.
    if (director->mCurState != state && state == 0x5 &&
        gBinds.wasPressed(BIND_MENU_TOGGLE)) {
        state = director->mCurState;
    }

#if ENABLE_DEBUG_WARPS
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

    return state;
}

// extern "C" void onFinishAppState(RumbleMgr* rumble) {
//     rumble->init();
// }

extern "C" void onSetup(TMarDirector* director) {
    static bool inited = false;
    director->setupObjects();

    // Runs on every stage load, so this must stay above the once-only guard.
    featuresOnStageLoad();

    if (inited) return; else inited = true;

    // Settings are already initialised, much earlier, by onAppInit.

    JKRHeap *oldHeap = JKRHeap::sSystemHeap->becomeCurrentHeap();
    menuInit();
    gSavestateMgr = new SavestateManager();
    
    if (oldHeap) {
        oldHeap->becomeCurrentHeap();
    } else {
        JKRHeap::sCurrentHeap = nullptr;
    }
}


extern "C" s32 onUpdate(JDrama::TDirector* director) {
    // Sample the pad before direct(), not after: onUpdateGameMode runs inside
    // it and asks whether the menu bind was pressed this frame, which would
    // otherwise be answered from the previous frame's sample.
    gBinds.update();

    int state = director->direct();

    // Apply/restore the toggled memory-patch features (ported gecko codes).
    // Runs every frame like the gecko handler; no-ops when nothing changed.
    featuresApply();

    actionsApply();

    if (gSavestateMgr) {
        gSavestateMgr->updateHook();
    }
    if (gMenu) {
        gMenu->update(gpApplication.mGamePads[0]);
    }

    return state;
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
        
        if (gMenu)
            gMenu->draw(&ortho);
#if ENABLE_SAVESTATE_DBG
        if (gSavestateMgr)
            gSavestateMgr->draw(&ortho);
#endif
    }
}
