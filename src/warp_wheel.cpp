// =====================================================================
// warp_wheel.cpp
//
// Warp destinations, the restart binds and the wheel UI. See
// warp_wheel.hxx for the two-phase warp design.
// =====================================================================

#include "susamune/warp_wheel.hxx"

#include "SMS/Manager/FlagManager.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/CardManager.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/binds.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/iling.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/settings.hxx"

namespace {

typedef JUtility::TColor Color;

const u8 kAreaHotel  = 7;
const u8 kAreaCasino = 14;
const u32 kPostCoronaFlag = 0x103AE;

constexpr u8 kParentAreas[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 6,  // 0x00
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 5, 6, 0xFF,  // 0x08
    9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x10
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 3, 9,  // 0x18
    4, 4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 0x20
    6, 5, 8, 0xFF, 9, 0xFF, 2, 2,  // 0x28
    3, 0xFF, 5, 6, 0xFF, 0xFF, 0xFF, 2,  // 0x30
    6, 9, 5, 3,  // 0x38
};

static_assert(sizeof(kParentAreas) == 0x3C, "parent-area table size changed");
static_assert(kParentAreas[0x37] == 2 && kParentAreas[0x2F] == 2 &&
                  kParentAreas[0x2E] == 2 && kParentAreas[0x3B] == 3 &&
                  kParentAreas[0x1E] == 3 && kParentAreas[0x30] == 3 &&
                  kParentAreas[0x20] == 4 && kParentAreas[0x21] == 4 &&
                  kParentAreas[0x3A] == 5 && kParentAreas[0x32] == 5 &&
                  kParentAreas[0x29] == 5 && kParentAreas[0x0D] == 5 &&
                  kParentAreas[0x33] == 6 && kParentAreas[0x28] == 6 &&
                  kParentAreas[0x38] == 6 && kParentAreas[0x07] == 6 &&
                  kParentAreas[0x0E] == 6 && kParentAreas[0x2A] == 8 &&
                  kParentAreas[0x2C] == 9 && kParentAreas[0x39] == 9 &&
                  kParentAreas[0x1F] == 9 && kParentAreas[0x10] == 9,
              "parent-area mapping changed");

}  // namespace

u8 LevelWarp::parentArea(u8 area) {
    return area < sizeof(kParentAreas) ? kParentAreas[area] : 0xFF;
}

// =====================================================================
// Warp mechanism
// =====================================================================

namespace {

bool             sArmed;
bool             sKeepSpawn;
bool             sOverrideSource;
LevelWarp::Dest  sDest;
LevelWarp::Dest  sSource;
LevelWarp::Dest  sLast;
bool             sLastValid;
bool             sTailPending;
bool             sClassicInstantHeld;
bool             sClassicInstantPending;
bool             sDeathSequence;
bool             sWaitForSave;
u8               sSaveIdleFrames;

bool saveFlowActive() {
    if (!gpMarDirector) return false;
    if (gpMarDirector->mCurState == TMarDirector::STATE_SAVE_CARD) return true;
    if (gpMarDirector->mCurState == TMarDirector::STATE_PAUSE_MENU &&
        gpMarDirector->mPauseMenu &&
        gpMarDirector->mPauseMenu->mState == TPauseMenu2::MENU_SAVING) {
        return true;
    }
    return gpCardManager &&
        gpCardManager->getLastStatus() == CARD_ERROR_BUSY;
}

u8 currentGameInt3() {
    u8 value = (u8)TFlagManager::smInstance->getFlag(0x40003);
    if (gpApplication.mCurrentScene.mAreaID == TGameSequence::AREA_DOLPIC &&
        TFlagManager::smInstance->getBool(kPostCoronaFlag)) {
        value |= LevelWarp::Dest::POST_CORONA;
    }
    return value;
}

enum ClassicCommand {
    CLASSIC_NONE,
    CLASSIC_WARP,
    CLASSIC_RESTART_KEEP,
    CLASSIC_RESTART_DEFAULT,
    CLASSIC_WARP_LAST,
    CLASSIC_RESTART_PARENT,
};

const u16 kSelectorMods = JUTGamePad::L | JUTGamePad::R | JUTGamePad::Z |
                          JUTGamePad::X | JUTGamePad::Y;
const u16 kInstantBase = JUTGamePad::B | JUTGamePad::DPAD_UP;

// C-stick rows from the familiar Level Select chart: seven main stages,
// Secrets at up-left and Special in the centre.
const u8 kMainAreas[7] = { 2, 3, 4, 5, 6, 8, 9 };

const LevelWarp::Dest kSublevels[7] = {
    { 0x37, 0, 1 },  // Windmill
    { 0x1e, 0, 1 },  // Blooper race
    { 0x21, 0, 3 },  // Sand bird
    { 0x3a, 0, 7 },  // Balloons
    { 0x0e, 0, 3 },  // Casino (episode 4)
    { 0x2c, 0, 2 },  // Noki 3 bottle
    { 0x39, 0, 3 },  // Eel
};

const LevelWarp::Dest kPlazas[7] = {
    { 1, 0, 0 },  // Bianco plant
    { 1, 1, 0 },  // Bianco chase
    { 1, 5, 0 },  // Ricco / Gelato plants
    { 1, 2, 0 },  // Peaceful
    { 1, 7, 0 },  // Pinna cutscene
    { 1, 8, 0 },  // Yoshi unlock
    { 1, 9, 0 },  // Flooded plaza
};

// The two extra selector rows present in sup39's source but easy to miss on
// the compact chart: X+Z selects Pinna Park scenarios, Y+Z Sirena Hotel.
const LevelWarp::Dest kPinnaPark[7] = {
    { 0x0d, 0, 0 }, { 0x0d, 1, 2 }, { 0x0d, 2, 4 }, { 0x0d, 3, 5 },
    { 0x0d, 4, 6 }, { 0x0d, 5, 7 }, { 0x0d, 7, 0 },
};

const LevelWarp::Dest kSirenaHotel[7] = {
    { 0x07, 0, 1 }, { 0x07, 1, 2 }, { 0x07, 2, 3 }, { 0x07, 2, 4 },
    { 0x07, 3, 6 }, { 0x07, 4, 7 }, { 0x07, 0, 0 },
};

struct ModifierDest {
    u16             mods;
    LevelWarp::Dest dest;
};

const ModifierDest kSecrets[] = {
    { 0,                         { 0x2f, 0, 2 } },
    { JUTGamePad::L,             { 0x2e, 0, 5 } },
    { JUTGamePad::R,             { 0x30, 0, 3 } },
    { JUTGamePad::L | JUTGamePad::R, { 0x20, 0, 0 } },
    { JUTGamePad::Z,             { 0x32, 0, 1 } },
    { JUTGamePad::Z | JUTGamePad::L, { 0x29, 0, 5 } },
    { JUTGamePad::Z | JUTGamePad::R, { 0x33, 0, 1 } },
    { JUTGamePad::Z | JUTGamePad::L | JUTGamePad::R, { 0x28, 0, 3 } },
    { JUTGamePad::X,             { 0x2a, 0, 4 } },
    { JUTGamePad::X | JUTGamePad::L, { 0x1f, 0, 5 } },
    { JUTGamePad::Y,             { 0x3a, 1, 0 } },
    { JUTGamePad::Y | JUTGamePad::L, { 0x3c, 0, 0 } },
};

const ModifierDest kSpecials[] = {
    { JUTGamePad::L,             { 0x14, 0, 0 } },
    { JUTGamePad::R,             { 0x15, 0, 0 } },
    { JUTGamePad::L | JUTGamePad::R, { 0x16, 0, 0 } },
    { JUTGamePad::Z | JUTGamePad::L, { 0x17, 0, 0 } },
    { JUTGamePad::Z | JUTGamePad::R, { 0x18, 0, 0 } },
    { JUTGamePad::Z | JUTGamePad::L | JUTGamePad::R, { 0x1d, 0, 0 } },
    { JUTGamePad::X,             { 0x34, 0, 0 } },
    { JUTGamePad::X | JUTGamePad::L, { 0x00, 0, 0 } },
    { JUTGamePad::Y | JUTGamePad::L, { 0x10, 0, 7 } },
};

int selectorStickSlot(TMarioGamePad *pad) {
    const f32 x = pad->mCStick.mStickX;
    const f32 y = pad->mCStick.mStickY;
    if (x * x + y * y < 0.25f) {
        return 8;
    }

    // Eight 45-degree wedges, clockwise from up. Avoid atan2f: this runs in
    // the stage-exit path too, and the integer comparisons are deterministic
    // at the diagonals runners are used to holding.
    const f32 ax = x < 0.0f ? -x : x;
    const f32 ay = y < 0.0f ? -y : y;
    if (ay > ax * 2.41421356f) return y > 0.0f ? 0 : 4;
    if (ax > ay * 2.41421356f) return x > 0.0f ? 2 : 6;
    if (x > 0.0f) return y > 0.0f ? 1 : 3;
    return y > 0.0f ? 7 : 5;
}

bool findModifierDest(const ModifierDest *table, u32 count, u16 mods,
                      LevelWarp::Dest *dest) {
    for (u32 i = 0; i < count; i++) {
        if (table[i].mods == mods) {
            *dest = table[i].dest;
            return true;
        }
    }
    return false;
}

ClassicCommand resolveClassicSelector(TMarioGamePad *pad, bool instant,
                                      LevelWarp::Dest *dest) {
    u16 buttons = JUTGamePad::mPadStatus[0].mButton;
    const u16 allowed = (u16)(kSelectorMods | (instant ? kInstantBase : 0));
    if ((buttons & ~allowed) != 0) {
        return CLASSIC_NONE;
    }
    if (instant && (buttons & kInstantBase) != kInstantBase) {
        return CLASSIC_NONE;
    }
    const u16 mods = (u16)(buttons & kSelectorMods);
    const int slot = selectorStickSlot(pad);

    if (slot < 7) {
        if (mods == (JUTGamePad::X | JUTGamePad::Z) ||
            mods == (JUTGamePad::X | JUTGamePad::Z | JUTGamePad::L)) {
            *dest = kPinnaPark[slot];
            return CLASSIC_WARP;
        }
        if (mods == (JUTGamePad::Y | JUTGamePad::Z) ||
            mods == (JUTGamePad::Y | JUTGamePad::Z | JUTGamePad::L)) {
            *dest = kSirenaHotel[slot];
            return CLASSIC_WARP;
        }
        if (mods == JUTGamePad::X || mods == (JUTGamePad::X | JUTGamePad::L)) {
            *dest = kSublevels[slot];
            return CLASSIC_WARP;
        }
        if (mods == JUTGamePad::Y || mods == (JUTGamePad::Y | JUTGamePad::L)) {
            *dest = kPlazas[slot];
            return CLASSIC_WARP;
        }

        int episode = -1;
        switch (mods) {
        case 0: episode = 0; break;
        case JUTGamePad::L: episode = 1; break;
        case JUTGamePad::R: episode = 2; break;
        case JUTGamePad::L | JUTGamePad::R: episode = 3; break;
        case JUTGamePad::Z: episode = 4; break;
        case JUTGamePad::Z | JUTGamePad::L: episode = 5; break;
        case JUTGamePad::Z | JUTGamePad::R: episode = 6; break;
        case JUTGamePad::Z | JUTGamePad::L | JUTGamePad::R: episode = 7; break;
        }
        if (episode >= 0) {
            dest->area = kMainAreas[slot];
            dest->episode = (u8)episode;
            dest->gameInt3 = (u8)episode;
            return CLASSIC_WARP;
        }
        return CLASSIC_NONE;
    }

    if (slot == 7) {
        return findModifierDest(kSecrets, sizeof(kSecrets) / sizeof(kSecrets[0]),
                                mods, dest) ? CLASSIC_WARP : CLASSIC_NONE;
    }

    // Centre-stick commands differ slightly between the original transition
    // selector and Instant Level Select, just as the chart documents.
    if (instant && mods == 0) return CLASSIC_RESTART_KEEP;
    if (mods == JUTGamePad::Z) return CLASSIC_RESTART_DEFAULT;
    if (instant && mods == JUTGamePad::Y) return CLASSIC_WARP_LAST;
    if (!instant && mods == JUTGamePad::Y) return CLASSIC_RESTART_PARENT;

    return findModifierDest(kSpecials, sizeof(kSpecials) / sizeof(kSpecials[0]),
                            mods, dest) ? CLASSIC_WARP : CLASSIC_NONE;
}

LevelWarp::Dest currentDest(bool parent) {
    const TGameSequence &cur = gpApplication.mCurrentScene;
    LevelWarp::Dest dest = { cur.mAreaID, cur.mEpisodeID, currentGameInt3() };
    if (!parent) {
        return dest;
    }

    // Y in the transition selector returns sublevels to their parent beach;
    // Z keeps the exact area. Main stages and standalone specials stay put.
    // Pinna Park is a main area here, while IL route matching also treats it
    // as the parent of its embedded scenario scenes.
    if (cur.mAreaID == 0x0D) return dest;
    const u8 parentArea = LevelWarp::parentArea(cur.mAreaID);
    if (parentArea == 0xFF) return dest;
    dest.area = parentArea;
    const TGameSequence &prev = gpApplication.mPrevScene;
    const int ilEpisode = ILing::activeParentEpisode(parentArea);
    const u8 parentEpisode = ilEpisode >= 0
        ? (u8)ilEpisode
        : prev.mAreaID == parentArea
              ? prev.mEpisodeID
              : (u8)(dest.gameInt3 & ~LevelWarp::Dest::POST_CORONA);
    dest.episode = parentEpisode;
    dest.gameInt3 = parentEpisode;
    return dest;
}

bool updateClassicInstant(TMarioGamePad *pad) {
    const u16 buttons = JUTGamePad::mPadStatus[0].mButton;
    const bool held = (buttons & kInstantBase) == kInstantBase;
    if (!held) {
        sClassicInstantHeld = false;
        sClassicInstantPending = false;
        return false;
    }
    if (sClassicInstantHeld) {
        return false;
    }

    LevelWarp::Dest dest;
    const ClassicCommand command = resolveClassicSelector(pad, true, &dest);
    if (command == CLASSIC_RESTART_KEEP && !sClassicInstantPending) {
        // Give Z/Y and the C-stick one frame to join a staggered B+D-Up chord.
        // Otherwise the current-area restart departs before the larger command
        // can be observed.
        sClassicInstantPending = true;
        return true;
    }
    sClassicInstantHeld = true;
    sClassicInstantPending = false;

    switch (command) {
    case CLASSIC_WARP:            LevelWarp::warpTo(dest); return true;
    case CLASSIC_RESTART_KEEP:    LevelWarp::restart(true); return true;
    case CLASSIC_RESTART_DEFAULT: LevelWarp::restartFull(); return true;
    case CLASSIC_WARP_LAST:       LevelWarp::warpToLast(); return true;
    default: return false;
    }
}

void markQuickFreezeReset() {
    gQFTTimer.requestReset();
    if (SUSAMUNE_ADDR_QF_TIMER_RESET != 0) {
        *(volatile u8 *)SUSAMUNE_ADDR_QF_TIMER_RESET = 1;
    }
}

// Redirect the stage the app is about to load, plus the save flags the
// destination is entered with. Runs on the frame the exit fade finishes.
void applyDest(const LevelWarp::Dest &dest) {
    TFlagManager *flags = TFlagManager::smInstance;
    flags->setFlag(0x40003, dest.gameInt3 & ~LevelWarp::Dest::POST_CORONA);
    flags->setFlag(0x40002, 0);      // coin count
    flags->setBool(true, 0x30006);   // got a shine in the previous stage --
    flags->setBool(false, 0x30004);  // together these suppress the death and
                                     // Peach-kidnapped openings
    if (dest.area == TGameSequence::AREA_DOLPIC) {
        flags->setBool((dest.gameInt3 & LevelWarp::Dest::POST_CORONA) != 0,
                       kPostCoronaFlag);
    }

    gpApplication.mNextScene.mAreaID    = dest.area;
    gpApplication.mNextScene.mEpisodeID = dest.episode;

    // Hold the stick neutral through the hotel/casino arrival, where Mario
    // otherwise walks off the spawn.
    TMarioGamePad *pad     = gpApplication.mGamePads[0];
    pad->_E4               = (dest.area == kAreaHotel || dest.area == kAreaCasino) ? 59 : 0;
    pad->mState.mReadInput = false;

    markQuickFreezeReset();
}

void overrideSourceForDefaultSpawn(const LevelWarp::Dest &source) {
    // Airstrip's only spawn ignores mPrevScene. Its special load path needs the
    // live source sequence rather than the synthetic self-source courses use.
    if (source.area == TGameSequence::AREA_AIRPORT) return;
    gpApplication.mCurrentScene.mAreaID    = source.area;
    gpApplication.mCurrentScene.mEpisodeID = source.episode;
    gpApplication.mCurrentScene.mFlag      = 0;
}

void prepareArmedDeparture() {
    sArmed       = false;
    sTailPending = true;

    if (sKeepSpawn) {
        sKeepSpawn = false;
        // decideMarioPosIdx() picks Mario's entry point from mPrevScene, which
        // TApplication fills from mCurrentScene as it loads -- so naming the
        // area he arrived from leaves the entry point where it was and he comes
        // back out of the same pipe. applyDest() rewrites mNextScene later.
        TGameSequence &cur = gpApplication.mCurrentScene;
        cur.mAreaID        = gpApplication.mPrevScene.mAreaID;
        cur.mEpisodeID     = gpApplication.mPrevScene.mEpisodeID;
    }

    gpApplication.mNextScene.mAreaID    = sDest.area;
    gpApplication.mNextScene.mEpisodeID = sDest.episode;
}

__attribute__((noinline)) void armWarp(const LevelWarp::Dest &dest,
                                       bool keepSpawn, bool overrideSource) {
    sDest = dest;
    sArmed = true;
    sKeepSpawn = keepSpawn;
    sOverrideSource = overrideSource;
    sWaitForSave = false;
}

}  // namespace

namespace LevelWarp {

void warpTo(const Dest &dest) {
    armWarp(dest, false, false);
    sLast      = dest;
    sLastValid = true;
}

void warpFrom(const Dest &source, const Dest &dest) {
    warpTo(dest);
    sSource = source;
    sOverrideSource = true;
}

void warpToLast() {
    if (sLastValid) armWarp(sLast, false, false);
}

void restart(bool keepSpawn) {
    const TGameSequence &cur = gpApplication.mCurrentScene;
    const Dest dest = { cur.mAreaID, cur.mEpisodeID, currentGameInt3() };
    armWarp(dest, keepSpawn, false);
}

void restartFull() {
    const Dest dest = currentDest(true);
    armWarp(dest, false, true);
    sSource = dest;
    // Entering from the destination itself selects the level's default spawn.
}

void restartAfterSave(bool keepSpawn) {
    restart(keepSpawn);
    sWaitForSave = true;
    sSaveIdleFrames = 0;
}

u8 kick(TMarDirector *director, u8 state) {
    if (!sArmed) {
        return state;
    }
    if (sWaitForSave) {
        if (saveFlowActive()) {
            sSaveIdleFrames = 0;
            return state;
        }
        if (++sSaveIdleFrames < 2) return state;
        sWaitForSave = false;
    }
    if (gpCardManager && gpCardManager->getLastStatus() == CARD_ERROR_BUSY) {
        return state;
    }
    prepareArmedDeparture();

    director->moveStage();
    return TMarDirector::STATE_STAGE_EXIT;
}

s32 onDirected(s32 appState) {
    // Non-zero means the director has finished leaving the stage and is
    // handing the app its next state; anything else is an ordinary frame.
    if (appState == 0) {
        return appState;
    }

    // If the director reaches an app-state handoff before another game-mode
    // tick, the card becoming idle is sufficient to release the queued exit.
    if (sArmed && sWaitForSave) {
        if (saveFlowActive()) return 0;
        sWaitForSave = false;
    }
    if (sArmed) {
        sWaitForSave = false;
        prepareArmedDeparture();
    }
    sDeathSequence = false;

    if (sTailPending) {
        sTailPending = false;
        applyDest(sDest);
        if (sOverrideSource) {
            overrideSourceForDefaultSpawn(sSource);
            sOverrideSource = false;
        }
        ILing::onWarpTail();
        return TApplication::CONTEXT_DIRECT_STAGE;
    }

    // Original Level Select: while an ordinary file/stage departure finishes,
    // a held chart combination redirects the next scene. Instant Level Select
    // uses the same resolver earlier in WarpWheel::update().
    const TGameSequence &next = gpApplication.mNextScene;
    const bool selectorExit =
        (next.mAreaID == TGameSequence::AREA_DOLPIC && next.mEpisodeID <= 9) ||
        (next.mAreaID == TGameSequence::AREA_AIRPORT && next.mEpisodeID == 0);
    if (appState > 1 && selectorExit && gpApplication.mGamePads[0]) {
        Dest selected;
        ClassicCommand command =
            resolveClassicSelector(gpApplication.mGamePads[0], false, &selected);
        bool parentRestart = false;
        if (command == CLASSIC_RESTART_DEFAULT) {
            selected = currentDest(false);
            command = CLASSIC_WARP;
        } else if (command == CLASSIC_RESTART_PARENT) {
            selected = currentDest(true);
            command = CLASSIC_WARP;
            parentRestart = true;
        }
        if (command == CLASSIC_WARP) {
            applyDest(selected);
            if (parentRestart) {
                overrideSourceForDefaultSpawn(selected);
            }
            return TApplication::CONTEXT_DIRECT_STAGE;
        }
    }

    if (appState > 1 && gSettings.getBool(SETTING_AREA_LOCK)) {
        // Turn every departure into a restart of the area being left, with
        // the spawn point it would have had on the way in.
        TGameSequence &cur = gpApplication.mCurrentScene;
        Dest dest          = { cur.mAreaID, cur.mEpisodeID, currentGameInt3() };
        cur.mAreaID        = gpApplication.mPrevScene.mAreaID;
        cur.mEpisodeID     = gpApplication.mPrevScene.mEpisodeID;
        applyDest(dest);
        return TApplication::CONTEXT_DIRECT_STAGE;
    }

    return appState;
}

}  // namespace LevelWarp

// =====================================================================
// Destination tables
// =====================================================================

namespace {

// Slices run clockwise from the up notch; index 8 is the centre.
const int kNumSlots = 9;

// One region of a wheel. `slot` is where it sits: slices run clockwise from
// the up notch and 8 is the centre. Labels live in the same order in the NUL
// pool below, keeping each sparse destination to four bytes.
struct Slot {
    u8 slot;
    u8 area;
    u8 episode;
    u8 gameInt3;
};

template <unsigned N>
constexpr int stringCount(const char (&pool)[N]) {
    int count = 0;
    for (unsigned i = 0; i < N; i++) {
        if (pool[i] == '\0') count++;
    }
    return count;
}

constexpr char kSlotLabels[] =
    "Windmill\0"
    "Bianco 3\0"
    "Bianco 6\0"
    "Blooper\0"
    "Blooper Race\0"
    "Ricco 4\0"
    "Gelato 1\0"
    "Sand Bird\0"
    "Mecha-Bowser\0"
    "Pinna 2\0"
    "Pinna 6\0"
    "Balloons\0"
    "Sirena 2\0"
    "Sirena 4\0"
    "King Boo\0"
    "Pianta 5\0"
    "Bottle\0"
    "Eel\0"
    "Noki 6\0"
    "Red Fish\0"
    "Ep2 Hotel\0"
    "Ep3 Hotel\0"
    "Ep4 Hotel\0"
    "Ep4 Casino\0"
    "Ep5 Hotel\0"
    "Ep5 Casino\0"
    "Ep7 Hotel\0"
    "Ep8 Reds\0"
    "Bianco Plant\0"
    "Bianco Chase\0"
    "R" SUSAMUNE_GLYPH_AMP "G Plants\0"
    "Peaceful\0"
    "Pinna Cut\0"
    "Yoshi\0"
    "Flooded\0"
    "Post-Corona\0"
    "Beach Pipe\0"
    "Pachinko\0"
    "Grass Pipe\0"
    "Lilypad\0"
    "Jail\0"
    "Airstrip\0"
    "Air Reds\0"
    "Bowser\0"
    "Corona";

// Every stored wheel's regions end to end, grouped in kWheels order.
constexpr Slot kSlots[] = {
    // Bianco subareas
    { 1, 0x37, 0, 1 },
    { 2, 0x2F, 0, 2 },
    { 5, 0x2E, 0, 5 },
    // Ricco subareas. Both Gooper Blooper fights are the one scene ricco8
    // behind area 0x3B, which has a single scenario, so there is only one.
    { 0, 0x3B, 0, 0 },
    { 1, 0x1E, 0, 1 },
    { 3, 0x30, 0, 3 },
    // Gelato subareas
    { 0, 0x20, 0, 0 },
    { 3, 0x21, 0, 3 },
    // Pinna subareas
    { 0, 0x3A, 1, 0 },
    { 1, 0x32, 0, 1 },
    { 5, 0x29, 0, 5 },
    { 7, 0x3A, 0, 7 },
    // Sirena subareas
    { 1, 0x33, 0, 1 },
    { 3, 0x28, 0, 3 },
    { 4, 0x38, 0, 4 },
    // Pianta subareas
    { 4, 0x2A, 0, 4 },
    // Noki subareas
    { 2, 0x2C, 0, 2 },
    { 3, 0x39, 0, 3 },
    { 5, 0x1F, 0, 5 },
    { 7, 0x10, 0, 7 },
    // Sirena hotel. The hotel and the casino each hide several scenarios
    // behind one area id; gameInt3 is what separates them.
    { 0, 7, 0, 1 },
    { 1, 7, 1, 2 },
    { 2, 7, 2, 3 },
    { 3, 14, 0, 3 },
    { 4, 7, 2, 4 },
    { 5, 14, 1, 4 },
    { 6, 7, 3, 6 },
    { 7, 7, 4, 7 },
    // Delfino Plaza
    { 0, 1, 0, 0 },
    { 1, 1, 1, 0 },
    { 2, 1, 5, 0 },
    { 3, 1, 2, 0 },
    { 4, 1, 7, 0 },
    { 5, 1, 8, 0 },
    { 6, 1, 9, 0 },
    { 7, 1, 2, LevelWarp::Dest::POST_CORONA },
    // Delfino secrets
    { 0, 0x15, 0, 0 },
    { 1, 0x16, 0, 0 },
    { 2, 0x17, 0, 0 },
    { 3, 0x18, 0, 0 },
    { 4, 0x1D, 0, 0 },
    { 5, 0x00, 0, 0 },
    { 6, 0x14, 0, 0 },
    { 7, 0x3C, 0, 0 },
    { 8, 0x34, 0, 0 },
};

static_assert(sizeof(Slot) == 4, "Slot must stay packed");
static_assert(stringCount(kSlotLabels) == sizeof(kSlots) / sizeof(kSlots[0]),
              "Slot labels must match kSlots");

// A stored wheel's half-open range of kSlots. Titles follow the same order in
// kWheelTitles, so a wheel needs only the two range bytes.
struct Wheel {
    u8 first;
    u8 count;
};

constexpr char kWheelTitles[] =
    "Bianco Subareas\0"
    "Ricco Subareas\0"
    "Gelato Subareas\0"
    "Pinna Subareas\0"
    "Sirena Subareas\0"
    "Pianta Subareas\0"
    "Noki Subareas\0"
    "Sirena Hotel\0"
    "Delfino Plaza\0"
    "Delfino Secrets";

constexpr Wheel kWheels[] = {
    { 0, 3 },
    { 3, 3 },
    { 6, 2 },
    { 8, 4 },
    { 12, 3 },
    { 15, 1 },
    { 16, 4 },
    { 20, 8 },
    { 28, 8 },
    { 36, 9 },
};

constexpr int kNumWheels = sizeof(kWheels) / sizeof(kWheels[0]);
static_assert(sizeof(Wheel) == 2, "Wheel must stay packed");
static_assert(stringCount(kWheelTitles) == kNumWheels,
              "Wheel titles must match kWheels");

// The ranges must tile kSlots exactly, which catches a group whose size
// changed without the following groups' offsets moving with it.
static_assert(kWheels[kNumWheels - 1].first + kWheels[kNumWheels - 1].count ==
                  sizeof(kSlots) / sizeof(kSlots[0]),
              "kWheels ranges must cover kSlots exactly");

// A region of the root wheel. `episodeArea` non-zero means its main wheel is
// generated rather than stored: eight slices, one per episode of that area.
struct Root {
    u8 episodeArea;
    s8 mainWheel;
    s8 subWheel;
};

constexpr char kRootLabels[] =
    "Bianco\0"
    "Ricco\0"
    "Gelato\0"
    "Pinna\0"
    "Sirena\0"
    "Pianta\0"
    "Noki\0"
    "Hotel\0"
    "Delfino";

constexpr Root kRoot[kNumSlots] = {
    { 2, -1, 0 },
    { 3, -1, 1 },
    { 4, -1, 2 },
    { 5, -1, 3 },
    { 6, -1, 4 },
    { 8, -1, 5 },
    { 9, -1, 6 },
    { 0, 7, -1 },
    { 0, 8, 9 },
};

static_assert(sizeof(Root) == 3, "Root must stay packed");
static_assert(stringCount(kRootLabels) == kNumSlots,
              "Root labels must match kRoot");

}  // namespace

// =====================================================================
// Wheel UI
// =====================================================================

namespace {

// Unit vectors every 22.5 degrees, Q12, clockwise from the up notch. Even
// entries point at a slice's centre, odd ones at the boundary between two.
const s16 kUnit[16][2] = {
    { 0, -4096 },     { 1567, -3784 }, { 2896, -2896 }, { 3784, -1567 },
    { 4096, 0 },      { 3784, 1567 },  { 2896, 2896 },  { 1567, 3784 },
    { 0, 4096 },      { -1567, 3784 }, { -2896, 2896 }, { -3784, 1567 },
    { -4096, 0 },     { -3784, -1567 },{ -2896, -2896 },{ -1567, -3784 },
};

// The wheel is an octagon of circumradius kRadiusOuter with an octagon of
// kRadiusInner taken out of the middle; the ring between them is cut on the
// eight boundary directions and each piece slid kGap out along its own centre
// direction. kRadiusLabel is the mid-ring apothem, where the flat edges are.
const int kCx = 320;
const int kCy = 245;
const int kRadiusOuter = 180;
const int kRadiusInner = 78;
const int kRadiusLabel = 125;
const int kGap = 6;

const int kTitleY   = 32;
const int kTitleSz  = 20;
const int kLabelSz  = 12;
const int kRootSz   = 16;
const int kDigitSz  = 26;
const int kFooterY  = 430;
const int kFooterSz = 12;

const int kCentreSlot = 8;

inline Color col(u8 r, u8 g, u8 b, u8 a) { return Color(r, g, b, a); }

inline Color cBackdrop() { return col(6, 8, 14, 150); }
inline Color cSlice()    { return col(34, 48, 61, 235); }
inline Color cSelected() { return col(255, 194, 61, 255); }
inline Color cLabel()    { return col(233, 241, 247, 255); }
inline Color cOnAccent() { return col(27, 18, 6, 255); }
inline Color cFooter()   { return col(127, 149, 166, 255); }

bool  sShown;
s8    sRoot = -1;  // -1 while the root wheel is up
bool  sSubMode;

// Index into kWheels for the wheel on screen; -1 means the episodes of
// kRoot[sRoot].episodeArea, which are generated rather than stored.
s8 currentWheel() {
    const Root &root = kRoot[sRoot];
    return (sSubMode && root.subWheel >= 0) ? root.subWheel : root.mainWheel;
}

// The wheel on screen, or null when it is a generated episode wheel.
const Wheel *currentWheelData() {
    if (sRoot < 0) {
        return nullptr;
    }
    s8 wheel = currentWheel();
    return wheel >= 0 ? &kWheels[wheel] : nullptr;
}

// The region at slice `i` of `wheel`, or null when that angle is empty.
const Slot *findSlot(const Wheel *wheel, int i) {
    for (int k = 0; k < wheel->count; k++) {
        if (kSlots[wheel->first + k].slot == i) {
            return &kSlots[wheel->first + k];
        }
    }
    return nullptr;
}

const char *currentTitle() {
    if (sRoot < 0) {
        return "Warp";
    }
    s8 wheel = currentWheel();
    return wheel >= 0 ? PackedText::at(kWheelTitles, wheel)
                      : PackedText::at(kRootLabels, sRoot);
}

// Label for slice `i` of the wheel on screen, or null when it is empty.
const char *slotLabel(int i, char *episodeLabel) {
    if (sRoot < 0) {
        return PackedText::at(kRootLabels, i);
    }
    const Wheel *wheel = currentWheelData();
    if (wheel) {
        const Slot *slot = findSlot(wheel, i);
        return slot ? PackedText::at(kSlotLabels, (int)(slot - kSlots)) : nullptr;
    }
    if (i >= 8) {
        return nullptr;
    }
    episodeLabel[0] = (char)('1' + i);
    episodeLabel[1] = '\0';
    return episodeLabel;
}

// Resolve slice `i` to a destination. False when the slice is empty.
bool slotDest(int i, LevelWarp::Dest *out) {
    const Wheel *wheel = currentWheelData();
    if (wheel) {
        const Slot *slot = findSlot(wheel, i);
        if (!slot) {
            return false;
        }
        out->area     = slot->area;
        out->episode  = slot->episode;
        out->gameInt3 = slot->gameInt3;
        return true;
    }
    if (i >= 8) {
        return false;
    }
    out->area     = kRoot[sRoot].episodeArea;
    out->episode  = (u8)i;
    out->gameInt3 = (u8)i;
    return true;
}

int stickSlot(TMarioGamePad *pad) {
    f32 x = pad->mControlStick.mStickX;
    f32 y = pad->mControlStick.mStickY;
    if (x * x + y * y < 0.25f) {
        return kCentreSlot;
    }
    int best    = 0;
    f32 bestDot = -2.0f;
    for (int i = 0; i < 8; i++) {
        // Stick Y points up, screen Y points down.
        f32 dot = x * kUnit[i * 2][0] - y * kUnit[i * 2][1];
        if (dot > bestDot) {
            bestDot = dot;
            best    = i;
        }
    }
    return best;
}

// Octagon vertex `k` at `radius`, displaced by kGap along direction centreK.
void corner(int k, int centreK, int radius, s16 *out) {
    out[0] = (s16)(kCx + (kUnit[k][0] * radius + kUnit[centreK][0] * kGap) / 4096);
    out[1] = (s16)(kCy + (kUnit[k][1] * radius + kUnit[centreK][1] * kGap) / 4096);
}

void drawCentred(const char *text, int cx, int y, int size, Color color) {
    gMenu->drawText(text, cx - Menu::textWidth(text, size) / 2, y, size, size, color);
}

void drawSlice(int i, bool selected) {
    char episodeLabel[2];
    const char *label = slotLabel(i, episodeLabel);
    if (!label) {
        return;
    }

    const int centreK = i * 2;
    s16 quad[8];
    corner((centreK + 15) & 15, centreK, kRadiusOuter, quad + 0);
    corner((centreK + 1) & 15, centreK, kRadiusOuter, quad + 2);
    corner((centreK + 1) & 15, centreK, kRadiusInner, quad + 4);
    corner((centreK + 15) & 15, centreK, kRadiusInner, quad + 6);
    gMenu->fillPoly(quad, 4, selected ? cSelected() : cSlice());

    const int size = (sRoot < 0) ? kRootSz : ((!currentWheelData()) ? kDigitSz : kLabelSz);
    drawCentred(label, kCx + kUnit[centreK][0] * kRadiusLabel / 4096,
                kCy + kUnit[centreK][1] * kRadiusLabel / 4096 - size / 2, size,
                selected ? cOnAccent() : cLabel());
}

void drawCentre(bool selected) {
    char episodeLabel[2];
    const char *label = slotLabel(kCentreSlot, episodeLabel);
    if (!label) {
        return;
    }
    s16 octagon[16];
    for (int k = 0; k < 8; k++) {
        octagon[k * 2]     = (s16)(kCx + kUnit[k * 2 + 1][0] * kRadiusInner / 4096);
        octagon[k * 2 + 1] = (s16)(kCy + kUnit[k * 2 + 1][1] * kRadiusInner / 4096);
    }
    const int size = (sRoot < 0) ? kRootSz : kLabelSz;
    gMenu->fillPoly(octagon, 8, selected ? cSelected() : cSlice());
    drawCentred(label, kCx, kCy - size / 2, size,
                selected ? cOnAccent() : cLabel());
}

void close() {
    sShown   = false;
    sRoot    = -1;
    sSubMode = false;
}

}  // namespace

namespace WarpWheel {

void update(TMarioGamePad *pad) {
    if (!gpMarDirector) {
        close();
        return;
    }

    const u8 state = gpMarDirector->mCurState;
    if (state == TMarDirector::STATE_DEATH) {
        sDeathSequence = true;
    }
    if (state != TMarDirector::STATE_NORMAL) {
        close();
        sClassicInstantHeld = false;
        sClassicInstantPending = false;
        const bool deathExit = sDeathSequence &&
            (state == TMarDirector::STATE_DEATH ||
             state == TMarDirector::STATE_STAGE_EXIT ||
             state == TMarDirector::STATE_STAGE_EXIT_2);
        const bool saveDialog = state == TMarDirector::STATE_SAVE_CARD ||
            (state == TMarDirector::STATE_PAUSE_MENU &&
             gpMarDirector->mPauseMenu &&
             gpMarDirector->mPauseMenu->mState == TPauseMenu2::MENU_SAVING);
        // The save dialog owns the director, so that path waits for an idle tick.
        if ((deathExit || saveDialog) &&
            gBinds.wasPressed(BIND_INSTANT_RESTART)) {
            if (saveDialog)
                LevelWarp::restartAfterSave(true);
            else
                LevelWarp::restart(true);
            if (gMenu && gSettings.getBool(SETTING_RESTART_QUEUED_FEEDBACK))
                gMenu->toast("Restart queued");
        }
        return;
    }
    sDeathSequence = false;

    bool closeForCommand = false;
    if (updateClassicInstant(pad)) {
        // The chart combo owns this press. In particular, C-up+B+D-up means
        // Bianco 1 rather than the bare B+D-up restart bind.
        closeForCommand = true;
    } else if (gBinds.wasPressed(BIND_INSTANT_RESTART)) {
        LevelWarp::restart(true);
        closeForCommand = true;
    } else if (gBinds.wasPressed(BIND_FULL_RESTART)) {
        LevelWarp::restartFull();
        closeForCommand = true;
    } else if (gBinds.wasPressed(BIND_WARP_LAST)) {
        LevelWarp::warpToLast();
        closeForCommand = true;
    }
    if (closeForCommand) {
        // Z may have opened the wheel one frame before a larger chord
        // completed. Do not let that overlay freeze an already-armed warp.
        close();
    }

    if (gMenu && gMenu->shown()) {
        close();
        return;
    }

    if (gBinds.wasPressed(BIND_WARP_WHEEL)) {
        if (sShown) {
            close();
        } else {
            sShown = true;
        }
    }
    if (!sShown) {
        return;
    }

    const u32 pressed = pad->mButtons.mFrameInput;
    const int slot    = stickSlot(pad);

    if (pressed & JUTGamePad::B) {
        if (sRoot < 0) {
            close();
        } else {
            sRoot = -1;
        }
    } else if (pressed & JUTGamePad::X) {
        sSubMode = !sSubMode;
    } else if (pressed & JUTGamePad::A) {
        if (sRoot < 0) {
            sRoot = (s8)slot;
        } else {
            LevelWarp::Dest dest;
            if (slotDest(slot, &dest)) {
                LevelWarp::warpTo(dest);
                close();
            }
        }
    }

    if (sShown) {
        // The stage is frozen while the wheel is up, but the buttons are still
        // sampled by anything outside the director's perform lists.
        pad->mButtons.mInput      = 0;
        pad->mButtons.mFrameInput = 0;
        pad->mButtons.mRapidInput = 0;
    }
}

bool shown() { return sShown; }

void draw() {
    if (!sShown || !gMenu) {
        return;
    }

    const int selected = stickSlot(gpApplication.mGamePads[0]);

    gMenu->fillBox(0, 0, 640, 480, cBackdrop());
    drawCentred(currentTitle(), kCx, kTitleY, kTitleSz, cSelected());

    for (int i = 0; i < 8; i++) {
        drawSlice(i, i == selected);
    }
    drawCentre(selected == kCentreSlot);

    const char *hint = SUSAMUNE_GLYPH_A " Select    " SUSAMUNE_GLYPH_B
                       " Back    " SUSAMUNE_GLYPH_X " Subareas";
    drawCentred(hint, kCx, kFooterY, kFooterSz, cFooter());
}

}  // namespace WarpWheel
