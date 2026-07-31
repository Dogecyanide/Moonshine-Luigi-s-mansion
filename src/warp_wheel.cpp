// =====================================================================
// warp_wheel.cpp
//
// Warp destinations, the restart binds and the wheel UI. See
// warp_wheel.hxx for the two-phase warp design.
// =====================================================================

#include "susamune/warp_wheel.hxx"

#include "SMS/Manager/FlagManager.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/binds.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

namespace {

typedef JUtility::TColor Color;

const u8 kAreaHotel  = 7;
const u8 kAreaCasino = 14;

}  // namespace

// =====================================================================
// Warp mechanism
// =====================================================================

namespace {

enum Kind {
    KIND_NONE,
    KIND_REENTER,  // re-enter the current area without the flag setup
    KIND_WARP,
};

Kind             sKind = KIND_NONE;
LevelWarp::Dest  sDest;
LevelWarp::Dest  sLast;
bool             sLastValid;
bool             sTailPending;

u8 currentGameInt3() { return (u8)TFlagManager::smInstance->getFlag(0x40003); }

void markQuickFreezeReset() {
    if (SUSAMUNE_ADDR_QF_TIMER_RESET != 0) {
        *(volatile u8 *)SUSAMUNE_ADDR_QF_TIMER_RESET = 1;
    }
}

// Redirect the stage the app is about to load, plus the save flags the
// destination is entered with. Runs on the frame the exit fade finishes.
void applyDest(const LevelWarp::Dest &dest) {
    TFlagManager *flags = TFlagManager::smInstance;
    flags->setFlag(0x40003, dest.gameInt3);
    flags->setFlag(0x40002, 0);      // coin count
    flags->setBool(true, 0x30006);   // got a shine in the previous stage --
    flags->setBool(false, 0x30004);  // together these suppress the death and
                                     // Peach-kidnapped openings

    gpApplication.mNextScene.mAreaID    = dest.area;
    gpApplication.mNextScene.mEpisodeID = dest.episode;

    // Hold the stick neutral through the hotel/casino arrival, where Mario
    // otherwise walks off the spawn.
    TMarioGamePad *pad     = gpApplication.mGamePads[0];
    pad->_E4               = (dest.area == kAreaHotel || dest.area == kAreaCasino) ? 59 : 0;
    pad->mState.mReadInput = false;

    markQuickFreezeReset();
}

}  // namespace

namespace LevelWarp {

void warpTo(const Dest &dest) {
    sDest      = dest;
    sLast      = dest;
    sLastValid = true;
    sKind      = KIND_WARP;
}

void warpToLast() {
    if (sLastValid) {
        sDest = sLast;
        sKind = KIND_WARP;
    }
}

void restart(bool full) {
    if (!full) {
        sKind = KIND_REENTER;
        return;
    }
    const TGameSequence &cur = gpApplication.mCurrentScene;
    sDest.area               = cur.mAreaID;
    sDest.episode            = cur.mEpisodeID;
    sDest.gameInt3           = currentGameInt3();
    sKind                    = KIND_WARP;
}

u8 kick(TMarDirector *director, u8 state) {
    if (sKind == KIND_NONE) {
        return state;
    }

    TGameSequence &cur  = gpApplication.mCurrentScene;
    TGameSequence &next = gpApplication.mNextScene;

    if (sKind == KIND_REENTER) {
        next.mAreaID    = cur.mAreaID;
        next.mEpisodeID = cur.mEpisodeID;
        next.mFlag.mVal = cur.mFlag.mVal;
        // Blanking the current scene is what keeps the re-entry cheap: the
        // stage it names is not the one being loaded, so nothing carries over.
        cur.mAreaID    = 0;
        cur.mEpisodeID = 0;
        cur.mFlag.mVal = 0x40;
        markQuickFreezeReset();
    } else {
        next.mAreaID    = sDest.area;
        next.mEpisodeID = sDest.episode;
        sTailPending    = true;
    }

    sKind = KIND_NONE;
    director->moveStage();
    return TMarDirector::STATE_STAGE_EXIT;
}

s32 onDirected(s32 appState) {
    // Non-zero means the director has finished leaving the stage and is
    // handing the app its next state; anything else is an ordinary frame.
    if (appState == 0) {
        return appState;
    }

    if (sTailPending) {
        sTailPending = false;
        applyDest(sDest);
        return TApplication::CONTEXT_DIRECT_STAGE;
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

// One region of a wheel. A null label means the angle is empty and no slice
// is drawn there.
struct Slot {
    const char *label;
    u8          area;
    u8          episode;
    u8          gameInt3;
};

// Slices run clockwise from the up notch; index 8 is the centre.
const int kNumSlots = 9;

#define EMPTY { nullptr, 0, 0, 0 }

const Slot kBiancoSub[kNumSlots] = {
    EMPTY,
    { "Windmill", 0x37, 0, 1 },
    { "Bianco 3", 0x2F, 0, 2 },
    EMPTY,
    EMPTY,
    { "Bianco 6", 0x2E, 0, 5 },
    EMPTY,
    EMPTY,
    EMPTY,
};

// Both Gooper Blooper fights are the one scene ricco8 behind area 0x3B, which
// has a single scenario -- there is no second destination to point slot 5 at.
const Slot kRiccoSub[kNumSlots] = {
    { "Blooper", 0x3B, 0, 0 },
    { "Blooper Race", 0x1E, 0, 1 },
    EMPTY,
    { "Ricco 4", 0x30, 0, 3 },
    EMPTY,
    EMPTY,
    EMPTY,
    EMPTY,
    EMPTY,
};

const Slot kGelatoSub[kNumSlots] = {
    { "Gelato 1", 0x20, 0, 0 },
    EMPTY,
    EMPTY,
    { "Sand Bird", 0x21, 0, 3 },
    EMPTY,
    EMPTY,
    EMPTY,
    EMPTY,
    EMPTY,
};

const Slot kPinnaSub[kNumSlots] = {
    { "Mecha-Bowser", 0x3A, 1, 0 },
    { "Pinna 2", 0x32, 0, 1 },
    EMPTY,
    EMPTY,
    EMPTY,
    { "Pinna 6", 0x29, 0, 5 },
    EMPTY,
    { "Balloons", 0x3A, 0, 7 },
    EMPTY,
};

const Slot kSirenaSub[kNumSlots] = {
    EMPTY,
    { "Sirena 2", 0x33, 0, 1 },
    EMPTY,
    { "Sirena 4", 0x28, 0, 3 },
    { "King Boo", 0x38, 0, 4 },
    EMPTY,
    EMPTY,
    EMPTY,
    EMPTY,
};

const Slot kPiantaSub[kNumSlots] = {
    EMPTY,
    EMPTY,
    EMPTY,
    EMPTY,
    { "Pianta 5", 0x2A, 0, 4 },
    EMPTY,
    EMPTY,
    EMPTY,
    EMPTY,
};

const Slot kNokiSub[kNumSlots] = {
    EMPTY,
    EMPTY,
    { "Bottle", 0x2C, 0, 2 },
    { "Eel", 0x39, 0, 3 },
    EMPTY,
    { "Noki 6", 0x1F, 0, 5 },
    EMPTY,
    { "Red Fish", 0x10, 0, 7 },
    EMPTY,
};

// The hotel and the casino each hide several scenarios behind one area id;
// gameInt3 is what separates them.
const Slot kHotel[kNumSlots] = {
    { "Ep3 Hotel", 7, 1, 2 },
    { "Ep4 Hotel", 7, 2, 3 },
    { "Ep4 Casino", 14, 0, 3 },
    { "Ep5 Hotel", 7, 2, 4 },
    { "Ep5 Casino", 14, 1, 4 },
    { "Ep7 Hotel", 7, 3, 6 },
    { "Ep8 Reds", 7, 4, 7 },
    EMPTY,
    EMPTY,
};

const Slot kPlaza[kNumSlots] = {
    { "Bianco Plant", 1, 0, 0 },
    { "Bianco Chase", 1, 1, 0 },
    { "R/G Plants", 1, 5, 0 },
    { "Peaceful", 1, 7, 0 },
    { "Pinna Cut", 1, 8, 0 },
    { "Yoshi", 1, 9, 0 },
    { "Flooded", 1, 2, 0 },
    EMPTY,
    EMPTY,
};

const Slot kPlazaSecrets[kNumSlots] = {
    { "Beach Pipe", 0x15, 0, 0 },
    { "Pachinko", 0x16, 0, 0 },
    { "Grass Pipe", 0x17, 0, 0 },
    { "Lilypad", 0x18, 0, 0 },
    { "Jail", 0x1D, 0, 0 },
    { "Airstrip", 0x00, 0, 0 },
    { "Air Reds", 0x14, 0, 0 },
    { "Bowser", 0x3C, 0, 0 },
    { "Corona", 0x34, 0, 0 },
};

#undef EMPTY

struct Wheel {
    const char *title;
    const Slot *slots;
};

const Wheel kWheels[] = {
    { "Bianco Subareas", kBiancoSub },
    { "Ricco Subareas", kRiccoSub },
    { "Gelato Subareas", kGelatoSub },
    { "Pinna Subareas", kPinnaSub },
    { "Sirena Subareas", kSirenaSub },
    { "Pianta Subareas", kPiantaSub },
    { "Noki Subareas", kNokiSub },
    { "Sirena Hotel", kHotel },
    { "Delfino Plaza", kPlaza },
    { "Delfino Secrets", kPlazaSecrets },
};

// A region of the root wheel. `episodeArea` non-zero means its main wheel is
// generated rather than stored: eight slices, one per episode of that area.
struct Root {
    const char *label;
    u8          episodeArea;
    s8          mainWheel;
    s8          subWheel;
};

const Root kRoot[kNumSlots] = {
    { "Bianco", 2, -1, 0 },
    { "Ricco", 3, -1, 1 },
    { "Gelato", 4, -1, 2 },
    { "Pinna", 5, -1, 3 },
    { "Sirena", 6, -1, 4 },
    { "Pianta", 8, -1, 5 },
    { "Noki", 9, -1, 6 },
    { "Hotel", 0, 7, -1 },
    { "Delfino", 0, 8, 9 },
};

const char kEpisodeLabels[8][2] = { "1", "2", "3", "4", "5", "6", "7", "8" };

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
const int kCy = 262;
const int kRadiusOuter = 180;
const int kRadiusInner = 78;
const int kRadiusLabel = 125;
const int kGap = 6;

const int kTitleY   = 44;
const int kTitleSz  = 20;
const int kLabelSz  = 15;
const int kDigitSz  = 26;
const int kFooterY  = 448;
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

const Slot *currentSlots() {
    if (sRoot < 0) {
        return nullptr;
    }
    s8 wheel = currentWheel();
    return wheel >= 0 ? kWheels[wheel].slots : nullptr;
}

const char *currentTitle() {
    if (sRoot < 0) {
        return "Warp";
    }
    s8 wheel = currentWheel();
    return wheel >= 0 ? kWheels[wheel].title : kRoot[sRoot].label;
}

// Label for slice `i` of the wheel on screen, or null when it is empty.
const char *slotLabel(int i) {
    if (sRoot < 0) {
        return kRoot[i].label;
    }
    const Slot *slots = currentSlots();
    if (slots) {
        return slots[i].label;
    }
    return i < 8 ? kEpisodeLabels[i] : nullptr;
}

// Resolve slice `i` to a destination. False when the slice is empty.
bool slotDest(int i, LevelWarp::Dest *out) {
    const Slot *slots = currentSlots();
    if (slots) {
        if (!slots[i].label) {
            return false;
        }
        out->area     = slots[i].area;
        out->episode  = slots[i].episode;
        out->gameInt3 = slots[i].gameInt3;
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
    const char *label = slotLabel(i);
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

    const int size = (sRoot >= 0 && !currentSlots()) ? kDigitSz : kLabelSz;
    drawCentred(label, kCx + kUnit[centreK][0] * kRadiusLabel / 4096,
                kCy + kUnit[centreK][1] * kRadiusLabel / 4096 - size / 2, size,
                selected ? cOnAccent() : cLabel());
}

void drawCentre(bool selected) {
    const char *label = slotLabel(kCentreSlot);
    if (!label) {
        return;
    }
    s16 octagon[16];
    for (int k = 0; k < 8; k++) {
        octagon[k * 2]     = (s16)(kCx + kUnit[k * 2 + 1][0] * kRadiusInner / 4096);
        octagon[k * 2 + 1] = (s16)(kCy + kUnit[k * 2 + 1][1] * kRadiusInner / 4096);
    }
    gMenu->fillPoly(octagon, 8, selected ? cSelected() : cSlice());
    drawCentred(label, kCx, kCy - kLabelSz / 2, kLabelSz,
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
    // Only normal gameplay reaches the game-mode hook that starts a warp.
    if (!gpMarDirector || gpMarDirector->mCurState != TMarDirector::STATE_NORMAL) {
        close();
        return;
    }

    if (gBinds.wasPressed(BIND_INSTANT_RESTART)) {
        LevelWarp::restart(false);
    } else if (gBinds.wasPressed(BIND_FULL_RESTART)) {
        LevelWarp::restart(true);
    } else if (gBinds.wasPressed(BIND_WARP_LAST)) {
        LevelWarp::warpToLast();
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
