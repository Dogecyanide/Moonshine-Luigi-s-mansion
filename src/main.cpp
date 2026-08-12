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
#if IS_EMULATOR
#include "susamune/emulator_persistence.hxx"
#endif
#include "susamune/features.hxx"
#include "susamune/actions.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/binds.hxx"
#include "susamune/input_display.hxx"
#include "susamune/metadata_display.hxx"
#include "susamune/mem_diagnostics.hxx"
#include "susamune/iling.hxx"
#include "susamune/attempt_counter.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/qft_display.hxx"
#include "susamune/pattern_selector.hxx"
#include "susamune/warp_wheel.hxx"
#include "susamune/visible_goop.hxx"
#if ENABLE_DEBUG_WARPS
#include "susamune/debug_warp.hxx"
#endif
#include "susamune/savestate.hxx"
#include "susamune/addresses.hxx"
#include "SMS/Manager/RumbleManager.hxx"
#include "SMS/Manager/FlagManager.hxx"
#include "SMS/Manager/PollutionManager.hxx"
#include "susamune/nintendont_cfg.h"

SavestateManager* gSavestateMgr = nullptr;

// Replaces the game's OSGetArenaLo. The mod is linked into the bottom of the
// heap arena; reporting the raised floor here keeps the root heap from
// allocating over it. The top is avoided because the apploader keeps the FST
// there.
//
// The reserve is SUSAMUNE_ARENA_RESERVE_SIZE, not the region size: __OSArenaLo
// sits a debug stack below the __ArenaLo the blob links at. Adding only the
// region size puts the heap floor at MOD_BASE + 0x1E000, inside the blob.
// SUSAMUNE_ARENA_RESERVE_SIZE must match arena_reserve in scripts/patches.py.
extern "C" void* getArenaLo() {
    return (void*)(*(volatile u32*)SUSAMUNE_ADDR_OS_ARENA_LO +
                   SUSAMUNE_ARENA_RESERVE_SIZE);
}

// Replaces the `bl TApplication::initialize` in main() (see patches.py), the
// last point before proc() starts the app-state machine. Settings must be live
// by here: proc() runs gameLoop() -- and so featuresApply() -- for the logo and
// title states too, so initialising any later leaves every feature reading
// zeroed BSS for the whole boot sequence.
extern "C" void onAppInit(TApplication* app) {
    app->initialize();
    gSettings.init();
    gQFTTimer.init();
    gAttemptCounter.init();
    ILing::init();
#if ENABLE_MEM_DIAGNOSTICS
    memDiagnosticsInit();
#endif

#if !IS_EMULATOR
    // The launcher owns this option because Sunshine cannot persist its own
    // rumble preference without a memory card. Apply it after initialize(),
    // when both the option flags and SMSRumbleMgr have been constructed.
    volatile u32* ninCfgConfig = reinterpret_cast<volatile u32*>(
        SUSAMUNE_NIN_CFG_CONFIG_PPC_ADDR);
    DCInvalidateRange((void*)ninCfgConfig, sizeof(*ninCfgConfig));
    if ((*ninCfgConfig & SUSAMUNE_NIN_CFG_DISABLE_RUMBLE) != 0) {
        SMSRumbleMgr->setActive(false);
        TFlagManager::smInstance->setFlag(0x90000u, 0);
    }
#endif

#if !IS_EMULATOR
    // The launcher made persisted settings available before initialize().
    featuresApplyEarly();
#endif
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

    if (!gSettings.getBool(SETTING_DISABLE_WARPS))
        state = LevelWarp::kick(director, state);

#if ENABLE_DEBUG_WARPS
    if (Warp::pending()) {
        Warp::execute();
        gQFTTimer.requestReset();
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

    // TPollutionManager publishes itself through gpPollution but its retail
    // destructor never clears that global. Stages without a pollution manager
    // would otherwise inherit a pointer into the previous stage's freed heap.
    gpPollution = nullptr;
    ILing::beforeStageSetup();
    director->setupObjects();
    ILing::onStageSetup();

    // Runs on every stage load, so this must stay above the once-only guard.
    featuresOnStageLoad();
    actionsOnStageLoad();
    visibleGoopOnStageSetup();
    gQFTTimer.onStageSetup(director);
    gAttemptCounter.onStageSetup(director);
    gCreationExtras.onStageSetup();
#if ENABLE_MEM_DIAGNOSTICS
    memDiagnosticsOnStageSetup();
#endif

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
#if IS_EMULATOR
    static bool persistenceReady = false;
    if (!persistenceReady && gSettings.finishInit()) {
        persistenceReady = true;
        ILing::onPersistenceReady();
        // This is before direct() for the first Nintendo-logo frame. Applying
        // the boot patches after direct() is too late for that director.
        featuresApplyEarly();
    }
#endif

    // Sample the pad before direct(), not after: onUpdateGameMode runs inside
    // it and asks whether the menu bind was pressed this frame, which would
    // otherwise be answered from the previous frame's sample.
    gBinds.update();
    const bool creationEditing = gQftDisplay.editing() ||
                                 gInputDisplay.editing() ||
                                 gMetadataDisplay.editing() ||
                                 gCreationExtras.editing();
    if (!creationEditing)
        gInputDisplay.update();
    PatternSelector::update(!creationEditing);
    gQFTTimer.beginFrame();
    gQFTTimer.update();
    // Before direct(): while the wheel is open it takes the pad away from
    // the game.
    if (!creationEditing && !gSettings.getBool(SETTING_DISABLE_WARPS))
        WarpWheel::update(gpApplication.mGamePads[0]);

    // Freeze the stage while an overlay is up. direct() runs the movement and
    // animation perform lists only outside the pause and stage-exit states, so
    // lending it one of those for the call is the entire pause; state 12 is the
    // one whose own branch does nothing while the fader is up. Any app state it
    // did produce would be a state change we never asked for, so drop it.
    const bool freeze = gpMarDirector &&
                        gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
                        ((gMenu && gMenu->shown()) || WarpWheel::shown());
    gCreationExtras.prepareUpdate();
    if (freeze) {
        gpMarDirector->mCurState = TMarDirector::STATE_STAGE_EXIT_2;
    }
    int state = director->direct();
    if (freeze) {
        gpMarDirector->mCurState = TMarDirector::STATE_NORMAL;
        state = 0;
    }
    if (!gSettings.getBool(SETTING_DISABLE_WARPS))
        state = LevelWarp::onDirected(state);

#if IS_EMULATOR
    EmulatorPersistence::service();
#endif

    gQFTTimer.update();
    ILing::update();
    if (!creationEditing)
        gAttemptCounter.update();

    // Apply/restore the toggled memory-patch features (ported gecko codes).
    // Runs every frame like the gecko handler; no-ops when nothing changed.
    featuresApply();

    actionsApply(!creationEditing);
    gCreationExtras.update(!freeze);

    if (gSavestateMgr && !creationEditing) {
        gSavestateMgr->updateHook();
    }
    if (gMenu) {
        gMenu->update(gpApplication.mGamePads[0]);
    }
#if ENABLE_MEM_DIAGNOSTICS
    memDiagnosticsUpdate();
#endif

    return state;
}

extern "C" void afterDraw() {
    // The original call is a full GXDrawDone barrier. Process queued loads
    // immediately afterward: director, fader, audio, and the current frame's
    // GPU work are all complete, while the next game frame has not begun.
    THPPlayerDrawDone();
    if (gSavestateMgr && !gQftDisplay.editing() && !gInputDisplay.editing() &&
        !gMetadataDisplay.editing() && !gCreationExtras.editing())
        gSavestateMgr->processPendingLoad();
    // gpPollution is stale until the async setup thread reaches onSetup.
    if (gpMarDirector && gpMarDirector->_260 != 0 &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING) {
        visibleGoopUpdate();
    }

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
#if ENABLE_MEM_DIAGNOSTICS
        memDiagnosticsDraw(gMenu);
#endif
        ILing::draw(gMenu);
        if (!gMenu || !gMenu->shown())
            PatternSelector::draw(gMenu);
        if (!gSettings.getBool(SETTING_DISABLE_WARPS))
            WarpWheel::draw();
#if ENABLE_SAVESTATE_DBG
        if (gSavestateMgr)
            gSavestateMgr->draw(&ortho);
#endif
    }
}
