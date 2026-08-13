// =====================================================================
// binds.cpp
//
// The bind table and the recorder behind it. See binds.hxx for the design;
// actions.cpp reads binds through gBinds and menu.cpp's Binds tab renders
// and records them generically off the packed description pools.
// =====================================================================

#include "susamune/binds.hxx"

#include "JSystem/JUtility/JUTGamePad.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/packed_text.hxx"
#include "Dolphin/string.h"

namespace {

// kButtonShifts reconstructs each mask with `1u << shift`; reject any future
// row that is not exactly one representable u16 bit.
#define SUSAMUNE_ASSERT_BIND_BUTTON(bit, token, display)                  \
    static_assert((bit) != 0 && (((bit) & ((bit) - 1)) == 0) &&          \
                      (unsigned)(bit) <= 0x8000u,                         \
                  "bind button must be one nonzero u16 bit");
SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_ASSERT_BIND_BUTTON)
#undef SUSAMUNE_ASSERT_BIND_BUTTON

#define SUSAMUNE_BIND_BUTTON_OR(bit, token, display) | (unsigned)(bit)
constexpr unsigned kListedButtonMask =
    0 SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_BIND_BUTTON_OR);
#undef SUSAMUNE_BIND_BUTTON_OR
static_assert(kListedButtonMask == SUSAMUNE_BIND_BUTTON_MASK,
              "bind button mask and button list differ");

// The ini tokens are launcher-only. Store glyphs densely and reconstruct the
// one-hot button bits from their positions.
#define SUSAMUNE_BIND_BUTTON_DISPLAY(bit, token, display) display "\0"
const char kButtonDisplays[] =
    SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_BIND_BUTTON_DISPLAY);
#undef SUSAMUNE_BIND_BUTTON_DISPLAY

#define SUSAMUNE_BIND_BUTTON_SHIFT(bit, token, display)                     \
    (u8)__builtin_ctz((unsigned)(bit)),
const u8 kButtonShifts[] = {
    SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_BIND_BUTTON_SHIFT)
};
#undef SUSAMUNE_BIND_BUTTON_SHIFT

const int kNumButtons = (int)(sizeof(kButtonShifts) / sizeof(kButtonShifts[0]));

const char kDisplaySeparator[] = SUSAMUNE_GLYPH_AMP;

// Button bits, spelled with the JUTGamePad names so the defaults read like the
// combos in doc/gecko_codes.md.
const u16 kA     = JUTGamePad::A;
const u16 kB     = JUTGamePad::B;
const u16 kX     = JUTGamePad::X;
const u16 kY     = JUTGamePad::Y;
const u16 kR     = JUTGamePad::R;
const u16 kZ     = JUTGamePad::Z;
const u16 kStart = JUTGamePad::START;
const u16 kDUp   = JUTGamePad::DPAD_UP;
const u16 kDDown = JUTGamePad::DPAD_DOWN;
const u16 kDLeft = JUTGamePad::DPAD_LEFT;
const u16 kDRght = JUTGamePad::DPAD_RIGHT;

// Names and defaults share one canonical row list without paying for a pointer
// beside every string.
#define BIND_DESC(name, defaultMask) name "\0"
const char kBindNames[] =
#include "binds_descs.inc"
;
#undef BIND_DESC

#define BIND_DESC(name, defaultMask) (u16)(defaultMask),
const u16 kDefaultMasks[] = {
#include "binds_descs.inc"
};
#undef BIND_DESC

#define BIND_DESC(name, defaultMask) +1
const int kBindDescCount = 0
#include "binds_descs.inc"
;
#undef BIND_DESC

int popCount(u16 v) {
    int n = 0;
    for (; v; v &= (u16)(v - 1)) {
        n++;
    }
    return n;
}

}  // namespace

Binds gBinds;

void Binds::resetDefaults() {
    for (int i = 0; i < BIND_COUNT; i++) {
        mMask[i] = kDefaultMasks[i];
    }
    mHeld       = 0;
    mPrevHeld   = 0;
    mRecAccum   = 0;
    mRecState   = REC_OFF;
    mRecTarget  = (BindId)0;
    mRecSilent  = false;
    mDirty      = false;
}

namespace {

// Reject anything the recorder could not have produced. The mask arrives from
// susamune.ini as well, where it may have been hand-written with any number of
// buttons; over-long combos are dropped rather than trimmed, so an obviously
// wrong line leaves the compiled-in default visible in the menu.
bool validMask(u16 mask) {
    return popCount((u16)(mask & SUSAMUNE_BIND_BUTTON_MASK)) <=
           SUSAMUNE_BIND_MAX_BUTTONS;
}

}  // namespace

void Binds::set(BindId id, u16 mask) {
    mask &= SUSAMUNE_BIND_BUTTON_MASK;
    if (!validMask(mask)) {
        return;
    }
    if (mMask[id] != mask) {
        mDirty = true;
    }
    mMask[id] = mask;
}

void Binds::adopt(u16 mask, BindId id) {
    mask &= SUSAMUNE_BIND_BUTTON_MASK;
    if (validMask(mask)) {
        mMask[id] = mask;
    }
}

void Binds::stageInto(volatile u16 *dst) const {
    for (int i = 0; i < BIND_COUNT; i++) {
        dst[i] = mMask[i];
    }
}

bool Binds::isHeld(BindId id) const {
    u16 m = mMask[id];
    return m != 0 && live() && mHeld == m;
}

bool Binds::wasPressed(BindId id) const {
    u16 m = mMask[id];
    return m != 0 && live() && mHeld == m && mPrevHeld != m;
}

void Binds::update() {
    mPrevHeld = mHeld;
    mHeld = (u16)(JUTGamePad::mPadStatus[0].mButton & SUSAMUNE_BIND_BUTTON_MASK);

    if (mRecSilent && mRecState == REC_OFF && mHeld == 0) {
        mRecSilent = false;
    }

    switch (mRecState) {
    case REC_OFF:
        break;

    case REC_WAIT_RELEASE:
        // The A press that opened the recorder is still down; capturing from
        // here would immediately record "A". Wait for a clean pad first.
        if (mHeld == 0) {
            mRecState = REC_CAPTURING;
        }
        break;

    case REC_CAPTURING:
        if ((mRecAccum & ~mHeld) != 0) {
            // A button that was down has come back up: the combo is whatever
            // was held immediately before that, not what is left now.
            commitRecord();
        } else {
            mRecAccum = mHeld;
            if (popCount(mRecAccum) >= SUSAMUNE_BIND_MAX_BUTTONS) {
                commitRecord();  // full combo -- no need to wait for a release
            }
        }
        break;
    }
}

void Binds::beginRecord(BindId id) {
    mRecTarget = id;
    mRecAccum  = 0;
    mRecState  = REC_WAIT_RELEASE;
    mRecSilent = true;
}

void Binds::cancelRecord() {
    mRecState = REC_OFF;
    mRecAccum = 0;
}

void Binds::commitRecord() {
    if (mRecAccum != 0) {
        set((BindId)mRecTarget, mRecAccum);
    }
    mRecState = REC_OFF;
    mRecAccum = 0;
}

void Binds::format(u16 mask, char *out) {
    out[0] = '\0';
    mask &= SUSAMUNE_BIND_BUTTON_MASK;

    const char *display = kButtonDisplays;
    for (int i = 0; i < kNumButtons; i++) {
        const char *next = display;
        while (*next++) {}
        if (mask & (u16)(1u << kButtonShifts[i])) {
            if (out[0]) {
                strcat(out, kDisplaySeparator);
            }
            strcat(out, display);
        }
        display = next;
    }

    if (!out[0]) {
        strcat(out, SUSAMUNE_BIND_NONE_TOKEN);
    }
}

const char *Binds::name(BindId id) {
    return PackedText::at(kBindNames, (int)id);
}

static_assert(sizeof(kDefaultMasks) / sizeof(kDefaultMasks[0]) == BIND_COUNT,
              "bind defaults must match SUSAMUNE_BIND_LIST");
static_assert(kBindDescCount == BIND_COUNT,
              "bind descriptions must match SUSAMUNE_BIND_LIST");
