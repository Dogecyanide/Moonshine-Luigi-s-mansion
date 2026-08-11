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
    CreationStyle style = {
        16, 416, 100,
        255, 255, 255, 255,
        0, 0, 0, 128,
        100, 2,
    };
    return style;
}

void QftDisplay::resetDefaults() {
    mStyle          = defaults();
    mEditor.reset();
    mDirty          = false;
    mDirtyBeforeEdit = false;
}

void QftDisplay::clamp() {
    mStyle.x          = (u16)clampi(mStyle.x, 0, 640);
    mStyle.y          = (u16)clampi(mStyle.y, 0, 480);
    mStyle.scale      = (u8)clampi(mStyle.scale, 50, 150);
    mStyle.brightness = (u8)clampi(mStyle.brightness, 25, 200);
    mStyle.padding    = (u8)clampi(mStyle.padding, 0, 16);
}

void QftDisplay::adopt(const volatile SusamuneQftDisplayCfg *src) {
    if (!src || src->magic != SUSAMUNE_QFT_DISPLAY_CFG_MAGIC ||
        src->version != SUSAMUNE_QFT_DISPLAY_CFG_VERSION) {
        return;
    }

    const u16 p = src->present;
    if (p & SUSAMUNE_QFT_DISPLAY_X)          mStyle.x = src->x;
    if (p & SUSAMUNE_QFT_DISPLAY_Y)          mStyle.y = src->y;
    if (p & SUSAMUNE_QFT_DISPLAY_SCALE)      mStyle.scale = src->scale;
    if (p & SUSAMUNE_QFT_DISPLAY_TEXT_R)     mStyle.textR = src->textR;
    if (p & SUSAMUNE_QFT_DISPLAY_TEXT_G)     mStyle.textG = src->textG;
    if (p & SUSAMUNE_QFT_DISPLAY_TEXT_B)     mStyle.textB = src->textB;
    if (p & SUSAMUNE_QFT_DISPLAY_TEXT_A)     mStyle.textA = src->textA;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_R)       mStyle.bgR = src->bgR;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_G)       mStyle.bgG = src->bgG;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_B)       mStyle.bgB = src->bgB;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_A)       mStyle.bgA = src->bgA;
    if (p & SUSAMUNE_QFT_DISPLAY_BRIGHTNESS) mStyle.brightness = src->brightness;
    if (p & SUSAMUNE_QFT_DISPLAY_PADDING)    mStyle.padding = src->padding;
    clamp();
    mDirty = false;
}

void QftDisplay::stageInto(volatile SusamuneQftDisplayCfg *dst) const {
    dst->magic      = SUSAMUNE_QFT_DISPLAY_CFG_MAGIC;
    dst->version    = SUSAMUNE_QFT_DISPLAY_CFG_VERSION;
    dst->present    = SUSAMUNE_QFT_DISPLAY_ALL;
    dst->x          = mStyle.x;
    dst->y          = mStyle.y;
    dst->scale      = mStyle.scale;
    dst->textR      = mStyle.textR;
    dst->textG      = mStyle.textG;
    dst->textB      = mStyle.textB;
    dst->textA      = mStyle.textA;
    dst->bgR        = mStyle.bgR;
    dst->bgG        = mStyle.bgG;
    dst->bgB        = mStyle.bgB;
    dst->bgA        = mStyle.bgA;
    dst->brightness = mStyle.brightness;
    dst->padding    = mStyle.padding;
    for (u32 i = 0; i < sizeof(dst->reserved); i++) dst->reserved[i] = 0;
}

void QftDisplay::draw(Menu *menu, const char *text) const {
    Creation::drawTextBox(menu, mStyle, text);
}

void QftDisplay::beginEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditor.begin(&mStyle);
}

void QftDisplay::updateEditor(TMarioGamePad *pad) {
    const u8 result = mEditor.update(pad, defaults());
    if (result & CreationEditor::UPDATE_CHANGED) {
        clamp();
        mDirty = true;
    }
    if (result & CreationEditor::UPDATE_CANCELLED) {
        mDirty = mDirtyBeforeEdit;
    }
}

void QftDisplay::drawEditor(Menu *menu) const {
    draw(menu, "1:23.456");
    mEditor.draw(menu, "QFT timer editor");
}
