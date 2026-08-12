#include "susamune/creation_extras.hxx"

#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "JSystem/J2D/J2DPicture.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"

namespace {

typedef JUtility::TColor Color;

const u32 kHudPaneTags[] = {
    't_ba', 'c_ba', 'r_ba', 'd_ba', 'm_ba', 's_ba', 'm_tx',
    '\0m_x', 'm_n1', 'm_n2', 'm_n3', 't_n1', 't_n2', 't_n3', 't_n4',
    't_n5', 't_n6', 't_n7', 't_n8', 't_n9', 't_n0', 't_c1', 't_c2',
    't_c3', 't_tx',
};
const u8 kHudPaneColors[] = {
    SUSAMUNE_CREATION_TIMER_BG, SUSAMUNE_CREATION_COIN_BG,
    SUSAMUNE_CREATION_RED_BG,
    SUSAMUNE_CREATION_BLUE_BG, SUSAMUNE_CREATION_LIVES_BG,
    SUSAMUNE_CREATION_SHINES_BG, SUSAMUNE_CREATION_LIFE_TEXT,
    SUSAMUNE_CREATION_LIFE_TEXT, SUSAMUNE_CREATION_LIFE_TEXT,
    SUSAMUNE_CREATION_LIFE_TEXT, SUSAMUNE_CREATION_LIFE_TEXT,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 0,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 1,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 2,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 3,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 4,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 5,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 6,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 7,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 8,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 9,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 10,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 11,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 12,
    SUSAMUNE_CREATION_TIMER_LABEL,
};
static_assert(sizeof(kHudPaneTags) / sizeof(kHudPaneTags[0]) ==
                  CreationExtras::HUD_PANE_COUNT,
              "HUD pane cache changed");
static_assert(sizeof(kHudPaneColors) / sizeof(kHudPaneColors[0]) ==
                  CreationExtras::HUD_PANE_COUNT,
              "HUD colour table changed");

constexpr char kTimerNames[] =
    "Normal digit 1\0Normal digit 2\0Normal digit 3\0Normal digit 4\0"
    "Normal digit 5\0Normal digit 6\0Countdown digit 1\0Countdown digit 2\0"
    "Countdown digit 3\0Countdown digit 4\0Separator 1\0Separator 2\0Separator 3";

constexpr char kMenuText[] =
    "HUD\0FLUDD water\0Coin streak\0Red coin streak\0Blue coin streak\0"
    "Lives streak\0Shines streak\0Life counter\0CUSTOM TEXT\0"
    "Word 1 text\0Word 1 style\0Word 1 visible\0"
    "Word 2 text\0Word 2 style\0Word 2 visible\0"
    "Word 3 text\0Word 3 style\0Word 3 visible\0"
    "MOD MENU\0Menu background";

const u32 kPreviewRootTags[] = {
    '\0t_0', '\0c_0', '\0r_0', '\0d_0', '\0m_0', '\0s_0',
};

constexpr int packedEntries(const char *pool, u32 bytes) {
    int count = 1;
    for (u32 i = 0; i + 1 < bytes; i++)
        if (!pool[i]) count++;
    return count;
}
static_assert(packedEntries(kMenuText, sizeof(kMenuText)) ==
                  CreationExtras::MENU_ROW_COUNT,
              "Creation menu row table changed");
static_assert(packedEntries(kTimerNames, sizeof(kTimerNames)) ==
                  SUSAMUNE_CREATION_TIMER_CHAR_COUNT,
              "Sunshine timer colour table changed");

const char kLettersLower[] = "abcdefghijklmnopqrstuvwxyz.,!?-_";
const char kLettersUpper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?-_";
const char kSymbols[] = "0123456789+-*/=()[]<>!?:;'\"_#%&@";

inline int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void copyRgb(u8 dst[3], const u8 src[3]) {
    for (int c = 0; c < 3; c++) dst[c] = src[c];
}

bool sameRgb(const u8 a[3], const u8 b[3]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

}  // namespace

CreationExtras gCreationExtras;

CreationStyle CreationExtras::defaultWordStyle(int index) {
    return CreationStyle{
        220, (u16)(80 + index * 42), 100, 255,
        0, 0, 0, 128, 100, 2,
    };
}

void CreationExtras::resetDefaults() {
    Creation::fillWhite(mColors, SUSAMUNE_CREATION_COLOR_COUNT);
    Creation::fillWhite(mDefaultColors, SUSAMUNE_CREATION_COLOR_COUNT);
    const u8 menuBg[] = {24, 28, 40};
    copyRgb(mColors[SUSAMUNE_CREATION_MENU_BG], menuBg);
    copyRgb(mDefaultColors[SUSAMUNE_CREATION_MENU_BG], menuBg);
    for (int word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++) {
        mWordStyle[word] = defaultWordStyle(word);
        Creation::fillWhite(mWordRgb[word], SUSAMUNE_CREATION_WORD_CHARS);
        snprintf(mWords[word], SUSAMUNE_CREATION_WORD_TEXT_SIZE,
                 "Custom Text %d", word + 1);
        mWordLength[word] = (u8)strlen(mWords[word]);
        mWordVisible[word] = 0;
    }
    for (u32 i = 0; i < sizeof(mHudPictures) / sizeof(mHudPictures[0]); i++)
        mHudPictures[i] = nullptr;
    mHudScreen = nullptr;
    mColorStyle = CreationStyle{
        0xffff, 0xffff, 100, 255, 0, 0, 0, 0xff, 100, 0xff,
    };
    mPreviewPaneCount = 0;
    mPreviewVisible = 0;
    mEditor.reset();
    mEditTitle = nullptr;
    mEditMode = EDIT_NONE;
    mKeyboard = false;
    mUppercase = false;
    mKeyboardConfirm = 0;
    mColorPresent = 0;
    mColorPresentBeforeEdit = 0;
    mTimerLabelVisible = 1;
    mDirty = false;
    mDirtyBeforeEdit = false;
}

void CreationExtras::clampWord(int index) {
    CreationStyle &s = mWordStyle[index];
    s.x = (u16)clampi(s.x, 0, 640);
    s.y = (u16)clampi(s.y, 0, 456);
    s.scale = (u8)clampi(s.scale, 50, 200);
    s.textBrightness = (u8)clampi(s.textBrightness, 25, 200);
    if (s.padding != 0xff) s.padding = (u8)clampi(s.padding, 0, 16);
    mWordLength[index] = (u8)clampi(mWordLength[index], 0,
                                    SUSAMUNE_CREATION_WORD_CHARS);
    mWords[index][mWordLength[index]] = '\0';
    mWordVisible[index] = mWordVisible[index] ? 1 : 0;
}

void CreationExtras::adopt(const volatile SusamuneCreationCfg *src) {
    if (!src || src->magic != SUSAMUNE_CREATION_CFG_MAGIC ||
        src->version != SUSAMUNE_CREATION_CFG_VERSION) return;
    for (int i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++) {
        if (src->colorPresent & SUSAMUNE_CREATION_COLOR(i))
            for (int c = 0; c < 3; c++) mColors[i][c] = src->rgb[i][c];
    }
    mColorPresent = src->colorPresent &
                    ((1u << SUSAMUNE_CREATION_COLOR_COUNT) - 1u);
    mColorPresent &=
        ~SUSAMUNE_CREATION_COLOR(SUSAMUNE_CREATION_LEGACY_WATER_TEXT) &
        ~SUSAMUNE_CREATION_COLOR(SUSAMUNE_CREATION_LEGACY_MARIO_HAT);
    if (src->timerLabelVisiblePresent)
        mTimerLabelVisible = src->timerLabelVisible ? 1 : 0;
    for (int word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++) {
        const volatile SusamuneCreationWordCfg &in = src->words[word];
        CreationStyle &s = mWordStyle[word];
        s.x = in.x; s.y = in.y; s.scale = in.scale; s.textA = in.textA;
        s.bgR = in.bgR; s.bgG = in.bgG; s.bgB = in.bgB; s.bgA = in.bgA;
        s.textBrightness = in.textBrightness; s.padding = in.padding;
        mWordVisible[word] = in.visible;
        mWordLength[word] = in.length;
        for (int i = 0; i < SUSAMUNE_CREATION_WORD_TEXT_SIZE; i++)
            mWords[word][i] = in.text[i];
        for (int i = 0; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
            for (int c = 0; c < 3; c++) mWordRgb[word][i][c] = in.rgb[i][c];
        clampWord(word);
    }
    mDirty = false;
}

void CreationExtras::stageInto(volatile SusamuneCreationCfg *dst) const {
    dst->magic = SUSAMUNE_CREATION_CFG_MAGIC;
    dst->version = SUSAMUNE_CREATION_CFG_VERSION;
    dst->reserved0 = 0;
    dst->colorPresent = mColorPresent;
    dst->timerScale = 100;
    dst->timerX = 0xffff;
    dst->timerY = 0xffff;
    dst->timerPositionPresent = 0;
    dst->timerLabelVisible = mTimerLabelVisible;
    dst->timerLabelVisiblePresent = 1;
    for (int i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++)
        for (int c = 0; c < 3; c++) dst->rgb[i][c] = mColors[i][c];
    dst->reserved1 = 0;
    for (int word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++) {
        volatile SusamuneCreationWordCfg &out = dst->words[word];
        const CreationStyle &s = mWordStyle[word];
        out.x = s.x; out.y = s.y; out.scale = s.scale; out.textA = s.textA;
        out.bgR = s.bgR; out.bgG = s.bgG; out.bgB = s.bgB; out.bgA = s.bgA;
        out.textBrightness = s.textBrightness; out.padding = s.padding;
        out.visible = mWordVisible[word]; out.length = mWordLength[word];
        for (int i = 0; i < SUSAMUNE_CREATION_WORD_TEXT_SIZE; i++)
            out.text[i] = mWords[word][i];
        for (int i = 0; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
            for (int c = 0; c < 3; c++) out.rgb[i][c] = mWordRgb[word][i][c];
        out.reserved = 0;
    }
}

void CreationExtras::onStageSetup() {
    for (u32 i = 0; i < sizeof(mHudPictures) / sizeof(mHudPictures[0]); i++)
        mHudPictures[i] = nullptr;
    mHudScreen = nullptr;
    if (!gpMarDirector || !gpMarDirector->mGCConsole ||
        !gpMarDirector->mGCConsole->mMainScreen) return;
    J2DScreen *screen = gpMarDirector->mGCConsole->mMainScreen;
    mHudScreen = screen;
    mPreviewPaneCount = 0;
    mPreviewVisible = 0;
    u32 captured = 0;
    for (u32 i = 0; i < HUD_PANE_COUNT; i++) {
        J2DPane *pane = screen->search(kHudPaneTags[i]);
        if (pane && pane->mTypeMagic == 'PIC1') {
            mHudPictures[i] = static_cast<J2DPicture *>(pane);
            const u32 bit = SUSAMUNE_CREATION_COLOR(kHudPaneColors[i]);
            if (!(captured & bit)) {
                const JUtility::TColor &original = mHudPictures[i]->mColorMask;
                mDefaultColors[kHudPaneColors[i]][0] = original.r;
                mDefaultColors[kHudPaneColors[i]][1] = original.g;
                mDefaultColors[kHudPaneColors[i]][2] = original.b;
                if (!(mColorPresent & bit))
                    copyRgb(mColors[kHudPaneColors[i]],
                            mDefaultColors[kHudPaneColors[i]]);
                captured |= bit;
            }
        }
    }

    const JUtility::TColor water[] = {
        gpMarDirector->mGCConsole->mWaterLeftPanelColor,
        gpMarDirector->mGCConsole->mWaterRightPanelColor,
    };
    for (int i = 0; i < 2; i++) {
        mWaterFillDefault[i][0] = water[i].r;
        mWaterFillDefault[i][1] = water[i].g;
        mWaterFillDefault[i][2] = water[i].b;
    }
    if (!(mColorPresent & SUSAMUNE_CREATION_COLOR(
              SUSAMUNE_CREATION_FLUDD_WATER))) {
        copyRgb(mDefaultColors[SUSAMUNE_CREATION_FLUDD_WATER],
                mWaterFillDefault[0]);
        copyRgb(mColors[SUSAMUNE_CREATION_FLUDD_WATER],
                mWaterFillDefault[0]);
    }

    applyHud();
}

void CreationExtras::applyHud() {
    for (u32 i = 0; i < HUD_PANE_COUNT; i++) {
        J2DPicture *picture = mHudPictures[i];
        if (!picture) continue;
        if (!(mColorPresent &
              SUSAMUNE_CREATION_COLOR(kHudPaneColors[i]))) continue;
        const u8 *rgb = mColors[kHudPaneColors[i]];
        picture->mColorMask.r = rgb[0];
        picture->mColorMask.g = rgb[1];
        picture->mColorMask.b = rgb[2];
        const u8 color = kHudPaneColors[i];
        // Retail streaks encode their hue in both black/white endpoints.
        if (color >= SUSAMUNE_CREATION_TIMER_BG &&
            color <= SUSAMUNE_CREATION_SHINES_BG) {
            picture->mColorOverlay.r = rgb[0];
            picture->mColorOverlay.g = rgb[1];
            picture->mColorOverlay.b = rgb[2];
        }
    }

    if (!mTimerLabelVisible && mHudPictures[HUD_PANE_COUNT - 1])
        mHudPictures[HUD_PANE_COUNT - 1]->mIsVisible = false;

    if (!gpMarDirector || !gpMarDirector->mGCConsole ||
        !(mColorPresent & SUSAMUNE_CREATION_COLOR(
              SUSAMUNE_CREATION_FLUDD_WATER))) return;
    const u8 *rgb = mColors[SUSAMUNE_CREATION_FLUDD_WATER];
    JUtility::TColor *fill[] = {
        &gpMarDirector->mGCConsole->mWaterLeftPanelColor,
        &gpMarDirector->mGCConsole->mWaterRightPanelColor,
    };
    const bool original = sameRgb(
        rgb, mDefaultColors[SUSAMUNE_CREATION_FLUDD_WATER]);
    for (int i = 0; i < 2; i++) {
        fill[i]->r = original ? mWaterFillDefault[i][0] : rgb[0];
        fill[i]->g = original ? mWaterFillDefault[i][1] : rgb[1];
        fill[i]->b = original ? mWaterFillDefault[i][2] : rgb[2];
    }
}

void CreationExtras::addPreviewPane(J2DPane *pane) {
    if (!pane || mPreviewPaneCount >= PREVIEW_PANE_COUNT) return;
    const u32 index = mPreviewPaneCount++;
    mPreviewPanes[index] = pane;
    if (pane->mIsVisible) mPreviewVisible |= 1u << index;
    pane->mIsVisible = true;
}

void CreationExtras::beginHudPreview(int color) {
    endHudPreview();
    if (!mHudScreen) return;
    int root;
    if (color == SUSAMUNE_CREATION_TIMER_LABEL) {
        root = 0;
    } else {
        if (color < SUSAMUNE_CREATION_TIMER_BG ||
            color > SUSAMUNE_CREATION_SHINES_BG) return;
        root = color - SUSAMUNE_CREATION_TIMER_BG;
    }
    addPreviewPane(mHudScreen->search(kPreviewRootTags[root]));
    for (u32 i = 0; i < HUD_PANE_COUNT; i++)
        if (kHudPaneColors[i] == color) addPreviewPane(mHudPictures[i]);
}

void CreationExtras::endHudPreview() {
    for (u32 i = 0; i < mPreviewPaneCount; i++)
        if (mPreviewPanes[i])
            mPreviewPanes[i]->mIsVisible = (mPreviewVisible & (1u << i)) != 0;
    mPreviewPaneCount = 0;
    mPreviewVisible = 0;
}

void CreationExtras::update() {
    // The cached panes live in the stage heap. Do not follow them while that
    // heap is being torn down or rebuilt by the setup thread.
    if (!gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL ||
        !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != mHudScreen) return;
    applyHud();
    for (u32 i = 0; i < mPreviewPaneCount; i++)
        if (mPreviewPanes[i]) mPreviewPanes[i]->mIsVisible = true;
}

void CreationExtras::draw(Menu *menu) const {
    if (!menu) return;
    for (int i = 0; i < SUSAMUNE_CREATION_WORD_COUNT; i++) {
        if (!mWordVisible[i] || !mWordLength[i]) continue;
        Creation::drawTextBox(menu, mWordStyle[i], mWordRgb[i],
                              SUSAMUNE_CREATION_WORD_CHARS, mWords[i]);
    }
}

bool CreationExtras::menuRowSeparator(int row) {
    return row == 0 || row == 8 || row == 18;
}

const char *CreationExtras::menuRowName(int row) {
    return row >= 0 && row < MENU_ROW_COUNT
               ? PackedText::at(kMenuText, row) : "";
}

const char *CreationExtras::menuRowValue(int row) const {
    if (row >= 9 && row <= 17) {
        const int local = row - 9;
        if (local % 3 == 2)
            return mWordVisible[local / 3] ? "On" : "Off";
    }
    return menuRowSeparator(row) ? "" : "Edit";
}

void CreationExtras::beginColorEditor(int first, int count, const char *title,
                                      const char *names) {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mColorPresentBeforeEdit = mColorPresent;
    mEditMode = EDIT_COLOR;
    mEditFirst = (u8)first;
    mEditCount = (u8)count;
    mEditTitle = title;
    mEditor.begin(&mColorStyle, mColors + first, mColorBackup + first,
                  (u16)count, (u16)count, names ? names : title, 0);
    beginHudPreview(first);
}

void CreationExtras::beginTimerCharacterEditor() {
    if (editing()) return;
    beginColorEditor(SUSAMUNE_CREATION_TIMER_CHAR_FIRST,
                     SUSAMUNE_CREATION_TIMER_CHAR_COUNT,
                     "Sunshine timer characters", kTimerNames);
    mEditMode = EDIT_TIMER;
}

void CreationExtras::toggleTimerLabel() {
    mTimerLabelVisible ^= 1;
    mDirty = true;
    if (mHudPictures[HUD_PANE_COUNT - 1])
        mHudPictures[HUD_PANE_COUNT - 1]->mIsVisible = mTimerLabelVisible != 0;
}

void CreationExtras::beginWordEditor(int index) {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_WORD_STYLE;
    mEditFirst = 0;
    mEditWord = (u8)index;
    mEditTitle = "Custom text style";
    mEditor.begin(&mWordStyle[index], mWordRgb[index], mWordBackup,
                  SUSAMUNE_CREATION_WORD_CHARS, mWordLength[index]);
}

void CreationExtras::beginKeyboard(int index) {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditWord = (u8)index;
    for (int i = 0; i < SUSAMUNE_CREATION_WORD_TEXT_SIZE; i++)
        mTextBackup[i] = mWords[index][i];
    mKeyboardCursor = 0;
    mKeyboardPage = 0;
    mKeyboardConfirm = 0;
    mUppercase = false;
    mKeyboard = true;
}

void CreationExtras::adjustMenuRow(int row, int direction) {
    (void)direction;
    if (row >= 1 && row <= 7) {
        const int color = row == 1 ? SUSAMUNE_CREATION_FLUDD_WATER
                                   : SUSAMUNE_CREATION_COIN_BG + row - 2;
        beginColorEditor(color, 1, menuRowName(row));
    } else if (row >= 9 && row <= 17) {
        const int local = row - 9;
        const int word = local / 3;
        if (local % 3 == 0) beginKeyboard(word);
        else if (local % 3 == 1) beginWordEditor(word);
        else {
            mWordVisible[word] = !mWordVisible[word];
            mDirty = true;
        }
    } else if (row == 19) {
        beginColorEditor(SUSAMUNE_CREATION_MENU_BG, 1, menuRowName(row));
    }
}

void CreationExtras::updateEditor(TMarioGamePad *pad) {
    if (mKeyboard) {
        updateKeyboard(pad);
        return;
    }
    if (!mEditor.editing()) return;
    const u8 (*defaults)[3] = mEditMode == EDIT_WORD_STYLE
                                  ? mDefaultColors
                                  : mDefaultColors + mEditFirst;
    const CreationStyle defaultStyle =
        mEditMode == EDIT_WORD_STYLE ? defaultWordStyle(mEditWord)
                                     : mColorStyle;
    const u8 result = mEditor.update(
        pad, defaultStyle, defaults,
        mEditMode != EDIT_WORD_STYLE ? mEditCount : 1);
    if (result & CreationEditor::UPDATE_CHANGED) {
        if (mEditMode == EDIT_WORD_STYLE) {
            clampWord(mEditWord);
        } else {
            for (int i = 0; i < mEditCount; i++) {
                const int slot = mEditFirst + i;
                mColorPresent |= SUSAMUNE_CREATION_COLOR(slot);
            }
        }
        mDirty = true;
        applyHud();
    }
    if (result & CreationEditor::UPDATE_CANCELLED) {
        mDirty = mDirtyBeforeEdit;
        if (mEditMode != EDIT_WORD_STYLE) {
            mColorPresent = mColorPresentBeforeEdit;
            const u32 savedPresent = mColorPresent;
            for (int i = 0; i < mEditCount; i++)
                mColorPresent |= SUSAMUNE_CREATION_COLOR(mEditFirst + i);
            applyHud();
            mColorPresent = savedPresent;
        }
    }
    if (!mEditor.editing()) {
        endHudPreview();
        mEditMode = EDIT_NONE;
    }
}

void CreationExtras::updateKeyboard(TMarioGamePad *pad) {
    const u32 pressed = pad->mButtons.mRapidInput;
    if (mKeyboardConfirm) {
        if (pressed & TMarioGamePad::A) {
            if (mKeyboardConfirm == 1) {
                mDirty = true;
                mKeyboard = false;
            } else if (mKeyboardConfirm == 2) {
                for (int i = 0; i < SUSAMUNE_CREATION_WORD_TEXT_SIZE; i++)
                    mWords[mEditWord][i] = mTextBackup[i];
                mWordLength[mEditWord] = (u8)strlen(mWords[mEditWord]);
                mDirty = mDirtyBeforeEdit;
                mKeyboard = false;
            } else {
                mWords[mEditWord][0] = '\0';
                mWordLength[mEditWord] = 0;
                mDirty = true;
            }
            mKeyboardConfirm = 0;
        } else if (pressed & TMarioGamePad::B) {
            mKeyboardConfirm = 0;
        }
        return;
    }
    if (pressed & TMarioGamePad::START) {
        mKeyboardConfirm = (pad->mButtons.mInput & TMarioGamePad::X) ? 2 : 1;
        return;
    }
    if (pressed & TMarioGamePad::Z) {
        mKeyboardConfirm = 3;
        return;
    }
    const int columns = 8;
    const int count = mKeyboardPage ? (int)sizeof(kSymbols) - 1 : 32;
    if (pressed & TMarioGamePad::DPAD_LEFT)
        mKeyboardCursor = (u8)((mKeyboardCursor + count - 1) % count);
    else if (pressed & TMarioGamePad::DPAD_RIGHT)
        mKeyboardCursor = (u8)((mKeyboardCursor + 1) % count);
    else if (pressed & TMarioGamePad::DPAD_UP)
        mKeyboardCursor = (u8)((mKeyboardCursor + count - columns) % count);
    else if (pressed & TMarioGamePad::DPAD_DOWN)
        mKeyboardCursor = (u8)((mKeyboardCursor + columns) % count);
    if (pressed & (TMarioGamePad::L | TMarioGamePad::R)) {
        mKeyboardPage ^= 1;
        mKeyboardCursor = 0;
    }
    if (pressed & TMarioGamePad::Y) mUppercase = !mUppercase;
    if ((pressed & TMarioGamePad::B) && mWordLength[mEditWord]) {
        mWords[mEditWord][--mWordLength[mEditWord]] = '\0';
        mDirty = true;
    }
    if ((pressed & TMarioGamePad::X) &&
        mWordLength[mEditWord] < SUSAMUNE_CREATION_WORD_CHARS) {
        mWords[mEditWord][mWordLength[mEditWord]++] = ' ';
        mWords[mEditWord][mWordLength[mEditWord]] = '\0';
        mDirty = true;
    }
    if ((pressed & TMarioGamePad::A) &&
        mWordLength[mEditWord] < SUSAMUNE_CREATION_WORD_CHARS) {
        const char *page = mKeyboardPage ? kSymbols
                            : (mUppercase ? kLettersUpper : kLettersLower);
        mWords[mEditWord][mWordLength[mEditWord]++] = page[mKeyboardCursor];
        mWords[mEditWord][mWordLength[mEditWord]] = '\0';
        mDirty = true;
    }
}

void CreationExtras::drawKeyboard(Menu *menu) const {
    const int word = mEditWord;
    Creation::drawTextBox(menu, mWordStyle[word], mWordRgb[word],
                          SUSAMUNE_CREATION_WORD_CHARS, mWords[word]);
    menu->fillBox(70, 188, 500, 262, Color(8, 11, 20, 238));
    char status[64];
    snprintf(status, sizeof(status), "Custom text %d   %u/%u", word + 1,
             mWordLength[word], SUSAMUNE_CREATION_WORD_CHARS);
    menu->drawText(status, 86, 202, 17, 17, Color(255, 255, 255, 255));
    const char *page = mKeyboardPage ? kSymbols
                        : (mUppercase ? kLettersUpper : kLettersLower);
    const int count = mKeyboardPage ? (int)sizeof(kSymbols) - 1 : 32;
    const int columns = 8;
    const int cellW = 46;
    const int startX = 112;
    const int startY = 246;
    for (int i = 0; i < count; i++) {
        const int x = startX + (i % columns) * cellW;
        const int y = startY + (i / columns) * 37;
        if (i == mKeyboardCursor)
            menu->fillBox(x - 9, y - 5, 32, 29, Color(90, 170, 255, 100));
        char one[2] = {page[i], '\0'};
        menu->drawText(one, x, y, 18, 18,
                       i == mKeyboardCursor ? Color(255, 255, 255, 255)
                                            : Color(190, 200, 220, 255));
    }
    menu->drawText("D-pad Select   A Type   B Delete   X Space   Y Case   L/R Page",
                   86, 401, 10, 10, Color(150, 170, 205, 255));
    menu->drawText("START: Keep   X+START: Cancel   Z: Clear",
                   86, 421, 10, 10, Color(150, 170, 205, 255));
    if (mKeyboardConfirm) {
        menu->fillBox(128, 272, 384, 78, Color(8, 11, 20, 250));
        const char *prompt = mKeyboardConfirm == 1 ? "Keep this text?"
                             : mKeyboardConfirm == 2 ? "Discard text changes?"
                                                     : "Clear this text?";
        menu->drawText(prompt, 320 - Menu::textWidth(prompt, 15) / 2,
                       286, 15, 15, Color(255, 255, 255, 255));
        const char *answer = SUSAMUNE_GLYPH_A " Confirm    "
                             SUSAMUNE_GLYPH_B " Go Back";
        menu->drawText(answer, 320 - Menu::textWidth(answer, 12) / 2,
                       320, 12, 12, Color(190, 220, 255, 255));
    }
}

void CreationExtras::drawEditor(Menu *menu) const {
    if (mKeyboard) {
        drawKeyboard(menu);
        return;
    }
    if (!mEditor.editing()) return;
    if (mEditMode == EDIT_WORD_STYLE) {
        const int word = mEditWord;
        const u16 selected = mEditor.target() ? mEditor.target() - 1 : 0xffff;
        Creation::drawTextBox(menu, mWordStyle[word], mWordRgb[word],
                              SUSAMUNE_CREATION_WORD_CHARS, mWords[word],
                              false, selected);
        mEditor.draw(menu, mEditTitle, mWords[word]);
        return;
    }
    if (mEditMode == EDIT_TIMER) {
        mEditor.draw(menu, mEditTitle, "12:34:567");
        return;
    }
    mEditor.draw(menu, mEditTitle, mEditTitle);
}
