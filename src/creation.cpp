#include "susamune/creation.hxx"

#include "Dolphin/printf.h"
#include "SMS/Player/MarioGamePad.hxx"
#include "susamune/layout_editor.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"

namespace {

typedef JUtility::TColor Color;

const char kFieldNames[] =
    "Text red\0Text green\0Text blue\0Text opacity\0"
    "Background red\0Background green\0Background blue\0Background opacity\0"
    "Brightness\0Padding";

enum EditField {
    FIELD_TEXT_R,
    FIELD_TEXT_G,
    FIELD_TEXT_B,
    FIELD_TEXT_A,
    FIELD_BG_R,
    FIELD_BG_G,
    FIELD_BG_B,
    FIELD_BG_A,
    FIELD_BRIGHTNESS,
    FIELD_PADDING,
    FIELD_COUNT,
};

inline int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

u8 lit(u8 value, u8 brightness) {
    return (u8)clampi((int)value * brightness / 100, 0, 255);
}

u8 *field(CreationStyle *style, u8 which) {
    switch (which) {
    case FIELD_TEXT_R:     return &style->textR;
    case FIELD_TEXT_G:     return &style->textG;
    case FIELD_TEXT_B:     return &style->textB;
    case FIELD_TEXT_A:     return &style->textA;
    case FIELD_BG_R:       return &style->bgR;
    case FIELD_BG_G:       return &style->bgG;
    case FIELD_BG_B:       return &style->bgB;
    case FIELD_BG_A:       return &style->bgA;
    case FIELD_BRIGHTNESS: return &style->brightness;
    default:               return &style->padding;
    }
}

}  // namespace

void CreationEditor::reset() {
    mStyle   = nullptr;
    mField   = 0;
    mEditing = false;
}

void CreationEditor::begin(CreationStyle *style) {
    if (!style || mEditing) return;
    mStyle   = style;
    mBackup  = *style;
    mField   = 0;
    mEditing = true;
}

u8 CreationEditor::update(TMarioGamePad *pad, const CreationStyle &defaults) {
    if (!mEditing || !mStyle || !pad) return UPDATE_NONE;

    const u32 rapid = pad->mButtons.mRapidInput;
    if (rapid & TMarioGamePad::A) {
        mEditing = false;
        return UPDATE_FINISHED;
    }
    if (rapid & TMarioGamePad::B) {
        *mStyle = mBackup;
        mEditing = false;
        return UPDATE_FINISHED | UPDATE_CANCELLED;
    }

    u8 result = UPDATE_NONE;
    if (rapid & TMarioGamePad::Z) {
        *mStyle = defaults;
        result |= UPDATE_CHANGED;
    }
    if (LayoutEditor::updatePositionScale(
            rapid, mStyle->x, mStyle->y, mStyle->scale)) {
        result |= UPDATE_CHANGED;
    }
    if (rapid & TMarioGamePad::START) {
        mField = (u8)((mField + 1) % FIELD_COUNT);
    }

    int delta = 0;
    if (rapid & TMarioGamePad::X) delta = -8;
    if (rapid & TMarioGamePad::Y) delta = 8;
    if (delta) {
        u8 *value = field(mStyle, mField);
        int lo = mField == FIELD_BRIGHTNESS ? 25 : 0;
        int hi = mField == FIELD_BRIGHTNESS ? 200 :
                 (mField == FIELD_PADDING ? 16 : 255);
        if (mField == FIELD_PADDING) delta = delta < 0 ? -1 : 1;
        *value = (u8)clampi((int)*value + delta, lo, hi);
        result |= UPDATE_CHANGED;
    }
    return result;
}

void CreationEditor::draw(Menu *menu, const char *title) const {
    if (!menu || !mStyle) return;

    char status[112];
    snprintf(status, sizeof(status),
             "X:%u Y:%u  Size:%u%%  Bright:%u%%  Pad:%u",
             mStyle->x, mStyle->y, mStyle->scale,
             mStyle->brightness, mStyle->padding);
    LayoutEditor::drawHeader(menu, 88, title, status);
    snprintf(status, sizeof(status), "START Field: %s   X -   Y +",
             PackedText::at(kFieldNames, mField));
    menu->drawText(status, 18, 58, 11, 11,
                   Color(190, 220, 255, 255));
    menu->drawText("D-pad Move   L/R Size   A Keep   B Cancel   Z Reset",
                   18, 76, 11, 11, Color(190, 220, 255, 255));
}

namespace Creation {

void drawTextBox(Menu *menu, const CreationStyle &style, const char *text) {
    if (!menu || !text) return;
    const int size = clampi(20 * (int)style.scale / 100, 10, 30);
    const int pad  = style.padding;
    const int w    = Menu::textWidth(text, size);
    const Color bg(lit(style.bgR, style.brightness),
                   lit(style.bgG, style.brightness),
                   lit(style.bgB, style.brightness), style.bgA);
    const Color fg(lit(style.textR, style.brightness),
                   lit(style.textG, style.brightness),
                   lit(style.textB, style.brightness), style.textA);
    menu->fillBox((int)style.x - pad, (int)style.y - pad,
                  w + pad * 2, size + pad * 2, bg);
    menu->drawText(text, style.x, style.y, size, size, fg);
}

}  // namespace Creation
