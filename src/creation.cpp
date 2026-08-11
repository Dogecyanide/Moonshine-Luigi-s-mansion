#include "susamune/creation.hxx"

#include "Dolphin/printf.h"
#include "SMS/Player/MarioGamePad.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/layout_editor.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"

namespace {

typedef JUtility::TColor Color;

const char kOptionNames[] =
    "Text red\0Text green\0Text blue\0Text opacity\0Text brightness\0"
    "Background red\0Background green\0Background blue\0"
    "Background opacity\0Padding";

enum EditOption {
    OPTION_TEXT_R,
    OPTION_TEXT_G,
    OPTION_TEXT_B,
    OPTION_TEXT_A,
    OPTION_TEXT_BRIGHTNESS,
    OPTION_BG_R,
    OPTION_BG_G,
    OPTION_BG_B,
    OPTION_BG_A,
    OPTION_PADDING,
    OPTION_COUNT,
};

enum ConfirmAction {
    CONFIRM_NONE,
    CONFIRM_KEEP,
    CONFIRM_CANCEL,
    CONFIRM_RESET,
};

inline int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

u8 lit(u8 value, u8 brightness) {
    return (u8)clampi((int)value * brightness / 100, 0, 255);
}

int glyphBytes(const char *text) {
    const u8 c = (u8)text[0];
    return ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) && text[1]
               ? 2 : 1;
}

const char *glyphAt(const char *text, u16 index) {
    if (!text) return nullptr;
    while (*text && index) {
        text += glyphBytes(text);
        index--;
    }
    return *text ? text : nullptr;
}

bool textChannel(const u8 (*textRgb)[3], u16 slots, u16 target,
                 int channel, u8 *value) {
    if (!textRgb || slots == 0) return false;
    const int first = target ? target - 1 : 0;
    const int end   = target ? first + 1 : slots;
    const u8 v = textRgb[first][channel];
    for (int i = first + 1; i < end; i++) {
        if (textRgb[i][channel] != v) return false;
    }
    *value = v;
    return true;
}

void adjustTextChannel(u8 (*textRgb)[3], u16 slots, u16 target,
                       int channel, int delta) {
    const int first = target ? target - 1 : 0;
    const int end   = target ? first + 1 : slots;
    for (int i = first; i < end; i++) {
        textRgb[i][channel] =
            (u8)clampi((int)textRgb[i][channel] + delta, 0, 255);
    }
}

void resetOption(CreationStyle &style, const CreationStyle &defaults,
                 u8 (*textRgb)[3], const u8 defaultRgb[3], u16 slots,
                 u8 option, u16 target) {
    if (option <= OPTION_TEXT_B) {
        const int first = target ? target - 1 : 0;
        const int end   = target ? first + 1 : slots;
        for (int i = first; i < end; i++)
            textRgb[i][option] = defaultRgb[option];
        return;
    }

    switch (option) {
    case OPTION_TEXT_A:          style.textA = defaults.textA; break;
    case OPTION_TEXT_BRIGHTNESS: style.textBrightness = defaults.textBrightness; break;
    case OPTION_BG_R:            style.bgR = defaults.bgR; break;
    case OPTION_BG_G:            style.bgG = defaults.bgG; break;
    case OPTION_BG_B:            style.bgB = defaults.bgB; break;
    case OPTION_BG_A:            style.bgA = defaults.bgA; break;
    default:                     style.padding = defaults.padding; break;
    }
}

const char *targetLabel(u16 target, const char *preview, char *out, int size) {
    if (target == 0) return "All";
    const char *glyph = glyphAt(preview, target - 1);
    if (!glyph || *glyph == ' ') {
        snprintf(out, size, "%u%s", target, glyph ? " (space)" : "");
        return out;
    }
    int used = snprintf(out, size, "%u '", target);
    int bytes = glyphBytes(glyph);
    for (int i = 0; i < bytes && used + 2 < size; i++) out[used++] = glyph[i];
    if (used + 1 < size) out[used++] = '\'';
    out[used] = '\0';
    return out;
}

}  // namespace

void CreationEditor::reset() {
    mStyle        = nullptr;
    mTextRgb      = nullptr;
    mBackupRgb    = nullptr;
    mRepeatMask   = 0;
    mTextSlots    = 0;
    mTargetSlots  = 0;
    mOption       = 0;
    mTextTarget   = 0;
    mRepeatFrames = 0;
    mConfirm      = CONFIRM_NONE;
    mEditing      = false;
}

void CreationEditor::begin(CreationStyle *style, u8 (*textRgb)[3],
                           u8 (*backupRgb)[3], u16 textSlots, u16 targetSlots) {
    if (!style || !textRgb || !backupRgb || textSlots == 0 || mEditing) return;
    mStyle        = style;
    mBackup       = *style;
    mTextRgb      = textRgb;
    mBackupRgb    = backupRgb;
    mTextSlots    = textSlots;
    mTargetSlots  = targetSlots && targetSlots < textSlots ? targetSlots : textSlots;
    for (u16 i = 0; i < mTextSlots; i++) {
        for (int c = 0; c < 3; c++) mBackupRgb[i][c] = mTextRgb[i][c];
    }
    mRepeatMask   = 0;
    mOption       = 0;
    mTextTarget   = 0;
    mRepeatFrames = 0;
    mConfirm      = CONFIRM_NONE;
    mEditing      = true;
}

u32 CreationEditor::repeatInput(TMarioGamePad *pad) {
    const u32 mask = TMarioGamePad::CSTICK_UP | TMarioGamePad::CSTICK_DOWN |
                     TMarioGamePad::CSTICK_LEFT | TMarioGamePad::CSTICK_RIGHT |
                     TMarioGamePad::L | TMarioGamePad::R;
    const u32 held = pad->mButtons.mInput & mask;
    u32 fired = pad->mButtons.mFrameInput & mask;
    fired |= held & ~mRepeatMask;

    if (!held) {
        mRepeatFrames = 0;
    } else if (held != mRepeatMask) {
        mRepeatFrames = 0;
    } else if (++mRepeatFrames >= 18) {
        fired |= held;
        mRepeatFrames = 14;
    }
    mRepeatMask = held;
    return fired;
}

u8 CreationEditor::update(TMarioGamePad *pad, const CreationStyle &defaults,
                          const u8 defaultRgb[3]) {
    if (!mEditing || !mStyle || !pad) return UPDATE_NONE;

    const u32 pressed = pad->mButtons.mRapidInput;
    if (mConfirm != CONFIRM_NONE) {
        if (pressed & TMarioGamePad::A) {
            if (mConfirm == CONFIRM_KEEP) {
                mConfirm = CONFIRM_NONE;
                mEditing = false;
                return UPDATE_FINISHED;
            }
            if (mConfirm == CONFIRM_CANCEL) {
                *mStyle = mBackup;
                for (u16 i = 0; i < mTextSlots; i++) {
                    for (int c = 0; c < 3; c++)
                        mTextRgb[i][c] = mBackupRgb[i][c];
                }
                mConfirm = CONFIRM_NONE;
                mEditing = false;
                return UPDATE_FINISHED | UPDATE_CANCELLED;
            }
            resetOption(*mStyle, defaults, mTextRgb, defaultRgb, mTextSlots,
                        mOption, mTextTarget);
            mConfirm = CONFIRM_NONE;
            return UPDATE_CHANGED;
        }
        if (pressed & TMarioGamePad::B) mConfirm = CONFIRM_NONE;
        return UPDATE_NONE;
    }

    if (pressed & TMarioGamePad::A) {
        mConfirm = CONFIRM_KEEP;
        return UPDATE_NONE;
    }
    if (pressed & TMarioGamePad::B) {
        mConfirm = CONFIRM_CANCEL;
        return UPDATE_NONE;
    }

    u8 result = UPDATE_NONE;
    if (pressed & TMarioGamePad::Z) {
        mConfirm = CONFIRM_RESET;
        return UPDATE_NONE;
    }
    if (pressed & TMarioGamePad::START) {
        mTextTarget = (u16)((mTextTarget + 1) % (mTargetSlots + 1));
    }

    const u32 repeat = repeatInput(pad);
    const u32 layout = (pad->mButtons.mRapidInput &
                        (TMarioGamePad::DPAD_LEFT | TMarioGamePad::DPAD_RIGHT |
                         TMarioGamePad::DPAD_UP | TMarioGamePad::DPAD_DOWN)) |
                       (repeat & (TMarioGamePad::L | TMarioGamePad::R));
    if (LayoutEditor::updatePositionScale(
            layout, mStyle->x, mStyle->y, mStyle->scale, 200)) {
        result |= UPDATE_CHANGED;
    }

    if (repeat & TMarioGamePad::CSTICK_UP) {
        mOption = (u8)(mOption ? mOption - 1 : OPTION_COUNT - 1);
    } else if (repeat & TMarioGamePad::CSTICK_DOWN) {
        mOption = (u8)((mOption + 1) % OPTION_COUNT);
    }

    int delta = 0;
    if (repeat & TMarioGamePad::CSTICK_LEFT) delta = -4;
    if (repeat & TMarioGamePad::CSTICK_RIGHT) delta = 4;
    if (!delta) return result;

    switch (mOption) {
    case OPTION_TEXT_R:
    case OPTION_TEXT_G:
    case OPTION_TEXT_B:
        adjustTextChannel(mTextRgb, mTextSlots, mTextTarget,
                          mOption - OPTION_TEXT_R, delta);
        break;
    case OPTION_TEXT_A:
        mStyle->textA = (u8)clampi((int)mStyle->textA + delta, 0, 255);
        break;
    case OPTION_TEXT_BRIGHTNESS:
        mStyle->textBrightness =
            (u8)clampi((int)mStyle->textBrightness + delta, 25, 200);
        break;
    case OPTION_BG_R:
        mStyle->bgR = (u8)clampi((int)mStyle->bgR + delta, 0, 255);
        break;
    case OPTION_BG_G:
        mStyle->bgG = (u8)clampi((int)mStyle->bgG + delta, 0, 255);
        break;
    case OPTION_BG_B:
        mStyle->bgB = (u8)clampi((int)mStyle->bgB + delta, 0, 255);
        break;
    case OPTION_BG_A:
        mStyle->bgA = (u8)clampi((int)mStyle->bgA + delta, 0, 255);
        break;
    default:
        if (mStyle->padding == 0xff) {
            if (delta > 0) mStyle->padding = 0;
        } else if (mStyle->padding == 0 && delta < 0) {
            mStyle->padding = 0xff;
        } else {
            mStyle->padding = (u8)clampi(
                (int)mStyle->padding + (delta > 0 ? 1 : -1), 0, 16);
        }
        break;
    }
    return result | UPDATE_CHANGED;
}

void CreationEditor::draw(Menu *menu, const char *title, const char *preview) const {
    if (!menu || !mStyle) return;

    const int panelY = mStyle->y < 240 ? 286 : 8;
    const int panelH = 172;
    menu->fillBox(8, panelY, 624, panelH, Color(0, 0, 0, 215));

    menu->drawText(title, 18, panelY + 9, 16, 16,
                   Color(255, 255, 255, 255));

    char targetBuf[24];
    const char *target = targetLabel(mTextTarget, preview, targetBuf, sizeof(targetBuf));
    char status[128];
    if (mTextTarget == 0)
        snprintf(status, sizeof(status), "START: Change to Single Character");
    else
        snprintf(status, sizeof(status), "START: Next Character (%s)", target);
    menu->drawText(status, 622 - Menu::textWidth(status, 12), panelY + 11,
                   12, 12, Color(190, 220, 255, 255));

    snprintf(status, sizeof(status), "Position X:%u Y:%u   Size:%u%%",
             mStyle->x, mStyle->y, mStyle->scale);
    menu->drawText(status, 18, panelY + 31, 12, 12,
                   Color(190, 220, 255, 255));

    u8 r, g, b;
    const bool sameR = textChannel(mTextRgb, mTextSlots, mTextTarget, 0, &r);
    const bool sameG = textChannel(mTextRgb, mTextSlots, mTextTarget, 1, &g);
    const bool sameB = textChannel(mTextRgb, mTextSlots, mTextTarget, 2, &b);
    const char *rgbLabel = mTextTarget == 0 ? "Text RGB" : "Character RGB";
    if (sameR && sameG && sameB) {
        snprintf(status, sizeof(status),
                 "%s:%03u,%03u,%03u   Background RGB:%03u,%03u,%03u",
                 rgbLabel, r, g, b, mStyle->bgR, mStyle->bgG, mStyle->bgB);
    } else {
        snprintf(status, sizeof(status),
                 "%s: Mixed   Background RGB:%03u,%03u,%03u",
                 rgbLabel, mStyle->bgR, mStyle->bgG, mStyle->bgB);
    }
    menu->drawText(status, 18, panelY + 48, 11, 11,
                   Color(190, 220, 255, 255));

    for (int i = 0; i < OPTION_COUNT; i++) {
        const int column = i / 5;
        const int row = i % 5;
        const int x = 18 + column * 306;
        const int y = panelY + 67 + row * 14;
        const bool selected = i == mOption;
        if (selected) {
            menu->fillBox(x - 3, y - 1, 294, 14, Color(90, 170, 255, 60));
            menu->drawText(">", x, y, 11, 11, Color(90, 170, 255, 255));
        }
        menu->drawText(PackedText::at(kOptionNames, i), x + 12, y, 11, 11,
                       selected ? Color(255, 255, 255, 255)
                                : Color(200, 206, 220, 255));

        const char *value = status;
        if (i <= OPTION_TEXT_B) {
            u8 v;
            if (textChannel(mTextRgb, mTextSlots, mTextTarget, i, &v))
                snprintf(status, sizeof(status), "%u", v);
            else
                value = "Mixed";
        } else if (i == OPTION_TEXT_A) {
            snprintf(status, sizeof(status), "%u", mStyle->textA);
        } else if (i == OPTION_TEXT_BRIGHTNESS) {
            snprintf(status, sizeof(status), "%u%%", mStyle->textBrightness);
        } else if (i == OPTION_BG_R) {
            snprintf(status, sizeof(status), "%u", mStyle->bgR);
        } else if (i == OPTION_BG_G) {
            snprintf(status, sizeof(status), "%u", mStyle->bgG);
        } else if (i == OPTION_BG_B) {
            snprintf(status, sizeof(status), "%u", mStyle->bgB);
        } else if (i == OPTION_BG_A) {
            snprintf(status, sizeof(status), "%u", mStyle->bgA);
        } else if (mStyle->padding == 0xff) {
            value = "Off";
        } else {
            snprintf(status, sizeof(status), "%u", mStyle->padding);
        }
        menu->drawText(value, x + 282 - Menu::textWidth(value, 11), y,
                       11, 11, Color(120, 220, 150, 255));
    }

    const char *controls = mTextTarget == 0
        ? SUSAMUNE_GLYPH_C " U/D Change Option   " SUSAMUNE_GLYPH_C
          " L/R Adjust   START Single Character"
        : SUSAMUNE_GLYPH_C " U/D Change Option   " SUSAMUNE_GLYPH_C
          " L/R Adjust   START Next Character";
    menu->drawText(controls, 18, panelY + 139, 10, 10,
                   Color(150, 170, 205, 255));
    menu->drawText("D-pad Move   L/R Size",
                   18, panelY + 154, 10, 10, Color(150, 170, 205, 255));
    const char *finish = SUSAMUNE_GLYPH_A " Keep  " SUSAMUNE_GLYPH_B
                         " Cancel  " SUSAMUNE_GLYPH_Z " Reset";
    menu->drawText(finish, 622 - Menu::textWidth(finish, 10), panelY + 154,
                   10, 10, Color(150, 170, 205, 255));

    if (mConfirm != CONFIRM_NONE) {
        const int boxY = panelY + 45;
        menu->fillBox(128, boxY, 384, 78, Color(8, 11, 20, 245));
        const char *prompt = mConfirm == CONFIRM_KEEP
                                 ? "Keep these changes?"
                             : mConfirm == CONFIRM_CANCEL
                                 ? "Discard all changes?"
                                 : "Reset selected option?";
        menu->drawText(prompt, 320 - Menu::textWidth(prompt, 15) / 2,
                       boxY + 13, 15, 15, Color(255, 255, 255, 255));
        const char *answer = SUSAMUNE_GLYPH_A " Confirm    "
                             SUSAMUNE_GLYPH_B " Go Back";
        menu->drawText(answer, 320 - Menu::textWidth(answer, 12) / 2,
                       boxY + 47, 12, 12, Color(190, 220, 255, 255));
    }
}

namespace Creation {

int glyphCount(const char *text) {
    int count = 0;
    while (text && *text) {
        text += glyphBytes(text);
        count++;
    }
    return count;
}

void drawTextLine(Menu *menu, const CreationStyle &style,
                  const u8 (*textRgb)[3], u16 textSlots, const char *text,
                  int x, int y, int size, u16 firstSlot, bool shadow) {
    if (!menu || !text || !textRgb || textSlots == 0) return;
    if (shadow) {
        menu->drawText(text, x + 1, y + 1, size, size, Color(0, 0, 0, 220));
    }

    char prefix[192];
    char run[192];
    int byte = 0;
    int glyph = 0;
    while (text[byte]) {
        const int startByte = byte;
        const u16 logicalSlot = (u16)(firstSlot + glyph);
        const u16 colorSlot = logicalSlot < textSlots ? logicalSlot : textSlots - 1;
        const u8 *rgb = textRgb[colorSlot];

        do {
            byte += glyphBytes(text + byte);
            glyph++;
            if (!text[byte]) break;
            const u16 nextLogical = (u16)(firstSlot + glyph);
            const u16 nextSlot = nextLogical < textSlots ? nextLogical : textSlots - 1;
            if (textRgb[nextSlot][0] != rgb[0] || textRgb[nextSlot][1] != rgb[1] ||
                textRgb[nextSlot][2] != rgb[2]) break;
        } while (byte - startByte + 1 < (int)sizeof(run));

        int runBytes = byte - startByte;
        for (int i = 0; i < startByte; i++) prefix[i] = text[i];
        prefix[startByte] = '\0';
        for (int i = 0; i < runBytes; i++) run[i] = text[startByte + i];
        run[runBytes] = '\0';
        menu->drawText(run, x + Menu::textWidth(prefix, size), y, size, size,
                       Color(lit(rgb[0], style.textBrightness),
                             lit(rgb[1], style.textBrightness),
                             lit(rgb[2], style.textBrightness), style.textA));
    }
}

void drawTextBox(Menu *menu, const CreationStyle &style,
                 const u8 (*textRgb)[3], u16 textSlots, const char *text,
                 bool rightAlignSlots) {
    if (!menu || !text) return;
    const int size = clampi(20 * (int)style.scale / 100, 10, 40);
    const int pad  = style.padding == 0xff ? 0 : style.padding;
    const int w    = Menu::textWidth(text, size);
    if (style.padding != 0xff) {
        menu->fillBox((int)style.x - pad, (int)style.y - pad,
                      w + pad * 2, size + pad * 2,
                      Color(style.bgR, style.bgG, style.bgB, style.bgA));
    }
    const int count = glyphCount(text);
    const u16 first = rightAlignSlots && count < textSlots
                          ? (u16)(textSlots - count) : 0;
    drawTextLine(menu, style, textRgb, textSlots, text, style.x, style.y,
                 size, first, false);
}

}  // namespace Creation
