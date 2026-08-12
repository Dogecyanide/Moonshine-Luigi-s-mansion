#include "susamune/qft_display.hxx"

#include "susamune/menu.hxx"

namespace {

inline int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

}  // namespace

QftDisplay gQftDisplay;

CreationStyle QftDisplay::defaults() {
    return CreationStyle{
        16, 416, 100, 255,
        0, 0, 0, 128,
        100, 2,
    };
}

void QftDisplay::resetDefaults() {
    mStyle           = defaults();
    Creation::fillWhite(mTextRgb, SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS);
    mEditor.reset();
    mDirty           = false;
    mDirtyBeforeEdit = false;
    mLeadingZero     = false;
}

void QftDisplay::clamp() {
    mStyle.x              = (u16)clampi(mStyle.x, 0, 640);
    mStyle.y              = (u16)clampi(mStyle.y, 0, 480);
    mStyle.scale          = (u8)clampi(mStyle.scale, 50, 200);
    mStyle.textBrightness = (u8)clampi(mStyle.textBrightness, 25, 200);
    if (mStyle.padding != 0xff)
        mStyle.padding = (u8)clampi(mStyle.padding, 0, 16);
}

void QftDisplay::adopt(const volatile SusamuneQftDisplayCfg *src) {
    if (!src || src->magic != SUSAMUNE_QFT_DISPLAY_CFG_MAGIC ||
        (src->version != 1 &&
         src->version != SUSAMUNE_QFT_DISPLAY_CFG_VERSION)) {
        return;
    }

    const u16 p = src->present;
    if (p & SUSAMUNE_QFT_DISPLAY_X)          mStyle.x = src->x;
    if (p & SUSAMUNE_QFT_DISPLAY_Y)          mStyle.y = src->y;
    if (p & SUSAMUNE_QFT_DISPLAY_SCALE)      mStyle.scale = src->scale;
    if (p & SUSAMUNE_QFT_DISPLAY_TEXT_A)     mStyle.textA = src->textA;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_R)       mStyle.bgR = src->bgR;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_G)       mStyle.bgG = src->bgG;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_B)       mStyle.bgB = src->bgB;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_A)       mStyle.bgA = src->bgA;
    if (p & SUSAMUNE_QFT_DISPLAY_TEXT_BRIGHTNESS)
        mStyle.textBrightness = src->textBrightness;
    if (p & SUSAMUNE_QFT_DISPLAY_PADDING)    mStyle.padding = src->padding;
    if (p & SUSAMUNE_QFT_DISPLAY_LEADING_ZERO)
        mLeadingZero = src->leadingZero != 0;

    for (int i = 0; i < SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS; i++) {
        if (p & SUSAMUNE_QFT_DISPLAY_TEXT_R)
            mTextRgb[i][0] = src->textR;
        if (p & SUSAMUNE_QFT_DISPLAY_TEXT_G)
            mTextRgb[i][1] = src->textG;
        if (p & SUSAMUNE_QFT_DISPLAY_TEXT_B)
            mTextRgb[i][2] = src->textB;
        if (src->version == SUSAMUNE_QFT_DISPLAY_CFG_VERSION &&
            (src->slotPresent & SUSAMUNE_QFT_DISPLAY_SLOT(i))) {
            for (int c = 0; c < 3; c++)
                mTextRgb[i][c] = src->textRgb[i][c];
        }
    }
    clamp();
    mDirty = false;
}

void QftDisplay::stageInto(volatile SusamuneQftDisplayCfg *dst) const {
    dst->magic          = SUSAMUNE_QFT_DISPLAY_CFG_MAGIC;
    dst->version        = SUSAMUNE_QFT_DISPLAY_CFG_VERSION;
    dst->present        = SUSAMUNE_QFT_DISPLAY_ALL;
    dst->x              = mStyle.x;
    dst->y              = mStyle.y;
    dst->scale          = mStyle.scale;
    dst->textR          = mTextRgb[0][0];
    dst->textG          = mTextRgb[0][1];
    dst->textB          = mTextRgb[0][2];
    dst->textA          = mStyle.textA;
    dst->bgR            = mStyle.bgR;
    dst->bgG            = mStyle.bgG;
    dst->bgB            = mStyle.bgB;
    dst->bgA            = mStyle.bgA;
    dst->textBrightness = mStyle.textBrightness;
    dst->padding        = mStyle.padding;
    dst->leadingZero    = mLeadingZero ? 1 : 0;
    for (u32 i = 0; i < sizeof(dst->reservedV1); i++) dst->reservedV1[i] = 0;
    dst->slotPresent = SUSAMUNE_QFT_DISPLAY_ALL_SLOTS;
    for (int i = 0; i < SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS; i++) {
        for (int c = 0; c < 3; c++)
            dst->textRgb[i][c] = mTextRgb[i][c];
    }
    for (u32 i = 0; i < sizeof(dst->reserved); i++) dst->reserved[i] = 0;
}

void QftDisplay::draw(Menu *menu, const char *text) const {
    Creation::drawTextBox(menu, mStyle, mTextRgb,
                          SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS, text, true);
}

void QftDisplay::beginEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditor.begin(&mStyle, mTextRgb, mBackupRgb,
                  SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS);
}

void QftDisplay::updateEditor(TMarioGamePad *pad) {
    const u8 defaultsRgb[1][3] = {{255, 255, 255}};
    const u8 result = mEditor.update(pad, defaults(), defaultsRgb);
    if (result & CreationEditor::UPDATE_CHANGED) {
        clamp();
        mDirty = true;
    }
    if (result & CreationEditor::UPDATE_CANCELLED) {
        mDirty = mDirtyBeforeEdit;
    }
}

void QftDisplay::drawEditor(Menu *menu) const {
    const u16 selected = mEditor.target() ? mEditor.target() - 1 : 0xffff;
    Creation::drawTextBox(menu, mStyle, mTextRgb,
                          SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS, "12:34:567",
                          true, selected);
    mEditor.draw(menu, "QFT timer editor", "12:34:567");
}

void QftDisplay::toggleLeadingZero() {
    mLeadingZero = !mLeadingZero;
    mDirty = true;
}
