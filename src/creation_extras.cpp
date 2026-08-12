#include "susamune/creation_extras.hxx"

#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "JSystem/J2D/J2DPicture.hxx"
#include "JSystem/J3D/J3DColor.hxx"
#include "JSystem/J3D/J3DMaterial.hxx"
#include "JSystem/J3D/J3DModel.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/MarioCap.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"

namespace {

typedef JUtility::TColor Color;

struct HudPaneDef {
    u32 tag;
    u8 color;
};

const HudPaneDef kHudPanes[] = {
    {'w_tx', SUSAMUNE_CREATION_WATER_TEXT},
    {'w_t1', SUSAMUNE_CREATION_FLUDD_TANK},
    {'w_t2', SUSAMUNE_CREATION_FLUDD_TANK},
    {'w_t3', SUSAMUNE_CREATION_FLUDD_TANK},
    {'t_ba', SUSAMUNE_CREATION_TIMER_BG},
    {'c_ba', SUSAMUNE_CREATION_COIN_BG},
    {'r_ba', SUSAMUNE_CREATION_RED_BG},
    {'b_ba', SUSAMUNE_CREATION_BLUE_BG},
    {'m_ba', SUSAMUNE_CREATION_LIVES_BG},
    {'s_ba', SUSAMUNE_CREATION_SHINES_BG},
    {'m_tx', SUSAMUNE_CREATION_LIFE_TEXT},
    {'\0m_x', SUSAMUNE_CREATION_LIFE_TEXT},
    {'m_n1', SUSAMUNE_CREATION_LIFE_TEXT},
    {'m_n2', SUSAMUNE_CREATION_LIFE_TEXT},
    {'m_n3', SUSAMUNE_CREATION_LIFE_TEXT},
    {'t_n1', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 0},
    {'t_n2', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 1},
    {'t_n3', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 2},
    {'t_n4', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 3},
    {'t_n5', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 4},
    {'t_n6', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 5},
    {'t_n7', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 6},
    {'t_n8', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 7},
    {'t_n9', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 8},
    {'t_n0', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 9},
    {'t_c1', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 10},
    {'t_c2', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 11},
    {'t_c3', SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 12},
};
static_assert(sizeof(kHudPanes) / sizeof(kHudPanes[0]) == 28,
              "HUD pane cache changed");

constexpr char kTimerNames[] =
    "Normal digit 1\0Normal digit 2\0Normal digit 3\0Normal digit 4\0"
    "Normal digit 5\0Normal digit 6\0Rush digit 1\0Rush digit 2\0"
    "Rush digit 3\0Rush digit 4\0Separator 1\0Separator 2\0Separator 3";

constexpr char kMenuText[] =
    "HUD\0WATER / ACQUA text\0FLUDD tank\0Sunshine timer streak\0"
    "Sunshine timer characters\0Coin streak\0Red coin streak\0"
    "Blue coin streak\0Lives streak\0Shines streak\0Life counter\0"
    "MARIO\0Shine outfit\0Mario hat\0CUSTOM TEXT\0"
    "Word 1 text\0Word 1 style\0Word 1 visible\0"
    "Word 2 text\0Word 2 style\0Word 2 visible\0"
    "Word 3 text\0Word 3 style\0Word 3 visible\0"
    "MOD MENU\0Menu background";

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

const u8 kDefaultColors[SUSAMUNE_CREATION_COLOR_COUNT][3] = {
    {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
    {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
    {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
    {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
    {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
    {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
    {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
    {255, 255, 255}, {255, 255, 255}, {255, 255, 255},
    { 24,  28,  40},
};

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

J3DGXColor *materialColor(J3DModelData *data, u16 index) {
    if (!data) return nullptr;
    // The local J3D headers misname these retail slots: they are the material
    // count/table, followed by the material's TEV block at +0x28.
    const u16 count = *reinterpret_cast<u16 *>(
        reinterpret_cast<u8 *>(data) + 0x24);
    if (index >= count) return nullptr;
    J3DMaterial **materials = *reinterpret_cast<J3DMaterial ***>(
        reinterpret_cast<u8 *>(data) + 0x28);
    if (!materials || !materials[index]) return nullptr;
    J3DTevBlock *tev = *reinterpret_cast<J3DTevBlock **>(
        reinterpret_cast<u8 *>(materials[index]) + 0x28);
    if (!tev) return nullptr;
    return reinterpret_cast<J3DGXColor *>((u32)tev->getTevKColor(0));
}

void tintMaterial(J3DModelData *data, u16 index, const u8 rgb[3]) {
    J3DGXColor *color = materialColor(data, index);
    if (!color) return;
    color->rgba.r = rgb[0];
    color->rgba.g = rgb[1];
    color->rgba.b = rgb[2];
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
    for (int i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++) {
        copyRgb(mColors[i], kDefaultColors[i]);
        copyRgb(mDefaultColors[i], kDefaultColors[i]);
    }
    for (int word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++) {
        mWordStyle[word] = defaultWordStyle(word);
        for (int i = 0; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
            copyRgb(mWordRgb[word][i], kDefaultColors[0]);
        snprintf(mWords[word], SUSAMUNE_CREATION_WORD_TEXT_SIZE,
                 "Custom Text %d", word + 1);
        mWordLength[word] = (u8)strlen(mWords[word]);
        mWordVisible[word] = 0;
    }
    for (u32 i = 0; i < sizeof(mHudPictures) / sizeof(mHudPictures[0]); i++)
        mHudPictures[i] = nullptr;
    mHudScreen = nullptr;
    mColorStyle = CreationStyle{0, 0, 100, 255, 0, 0, 0, 0xff, 100, 0xff};
    mEditor.reset();
    mEditTitle = nullptr;
    mEditMode = EDIT_NONE;
    mKeyboard = false;
    mUppercase = false;
    mKeyboardConfirm = 0;
    mColorPresent = 0;
    mColorPresentBeforeEdit = 0;
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
    for (int i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++)
        for (int c = 0; c < 3; c++) dst->rgb[i][c] = mColors[i][c];
    for (u32 i = 0; i < sizeof(dst->reserved1); i++) dst->reserved1[i] = 0;
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
    u32 captured = 0;
    for (u32 i = 0; i < sizeof(kHudPanes) / sizeof(kHudPanes[0]); i++) {
        J2DPane *pane = screen->search(kHudPanes[i].tag);
        if (pane && pane->mTypeMagic == 'PIC1') {
            mHudPictures[i] = static_cast<J2DPicture *>(pane);
            const u32 bit = SUSAMUNE_CREATION_COLOR(kHudPanes[i].color);
            if (!(captured & bit)) {
                const JUtility::TColor &original = mHudPictures[i]->mColorMask;
                mDefaultColors[kHudPanes[i].color][0] = original.r;
                mDefaultColors[kHudPanes[i].color][1] = original.g;
                mDefaultColors[kHudPanes[i].color][2] = original.b;
                if (!(mColorPresent & bit))
                    copyRgb(mColors[kHudPanes[i].color],
                            mDefaultColors[kHudPanes[i].color]);
                captured |= bit;
            }
        }
    }
    applyHud();
}

void CreationExtras::applyHud() {
    for (u32 i = 0; i < sizeof(kHudPanes) / sizeof(kHudPanes[0]); i++) {
        J2DPicture *picture = mHudPictures[i];
        if (!picture) continue;
        if (!(mColorPresent &
              SUSAMUNE_CREATION_COLOR(kHudPanes[i].color))) continue;
        const u8 *rgb = mColors[kHudPanes[i].color];
        picture->mColorMask.r = rgb[0];
        picture->mColorMask.g = rgb[1];
        picture->mColorMask.b = rgb[2];
    }
}

void CreationExtras::applyMario() {
    if (!gpMarioOriginal || !gpMarioOriginal->mBodyModelData) return;
    if (!(mColorPresent & SUSAMUNE_CREATION_COLOR(
              SUSAMUNE_CREATION_SHINE_OUTFIT)) &&
        !(mColorPresent & SUSAMUNE_CREATION_COLOR(
              SUSAMUNE_CREATION_MARIO_HAT))) return;
    const u8 white[3] = {255, 255, 255};
    const u8 *outfit = gpMarioOriginal->mAttributes.mIsShineShirt
                           ? mColors[SUSAMUNE_CREATION_SHINE_OUTFIT] : white;
    if (mColorPresent & SUSAMUNE_CREATION_COLOR(
            SUSAMUNE_CREATION_SHINE_OUTFIT)) {
        tintMaterial(gpMarioOriginal->mBodyModelData, 2, outfit);
        tintMaterial(gpMarioOriginal->mBodyModelData, 4, outfit);
    }
    if (!(mColorPresent & SUSAMUNE_CREATION_COLOR(
              SUSAMUNE_CREATION_MARIO_HAT))) return;
    tintMaterial(gpMarioOriginal->mBodyModelData, 9,
                 mColors[SUSAMUNE_CREATION_MARIO_HAT]);

    if (!gpMarioOriginal->mCap) return;
    J3DModel *cap = gpMarioOriginal->mCap->mCap1;
    if (cap) tintMaterial(cap->mModelData, 0,
                          mColors[SUSAMUNE_CREATION_MARIO_HAT]);
}

void CreationExtras::update() {
    // The cached panes and Mario live in the stage heap. Do not follow them
    // while that heap is being torn down or rebuilt by the setup thread.
    if (!gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL ||
        !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != mHudScreen) return;
    applyHud();
    applyMario();
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
    return row == 0 || row == 11 || row == 14 || row == 24;
}

const char *CreationExtras::menuRowName(int row) {
    return row >= 0 && row < MENU_ROW_COUNT
               ? PackedText::at(kMenuText, row) : "";
}

const char *CreationExtras::menuRowValue(int row) const {
    if (row >= 15 && row <= 23) {
        const int local = row - 15;
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
    static const u8 rowToColor[] = {
        SUSAMUNE_CREATION_WATER_TEXT, SUSAMUNE_CREATION_FLUDD_TANK,
        SUSAMUNE_CREATION_TIMER_BG, SUSAMUNE_CREATION_TIMER_CHAR_FIRST,
        SUSAMUNE_CREATION_COIN_BG, SUSAMUNE_CREATION_RED_BG,
        SUSAMUNE_CREATION_BLUE_BG, SUSAMUNE_CREATION_LIVES_BG,
        SUSAMUNE_CREATION_SHINES_BG, SUSAMUNE_CREATION_LIFE_TEXT,
    };
    if (row >= 1 && row <= 10) {
        const int local = row - 1;
        const int count = local == 3 ? SUSAMUNE_CREATION_TIMER_CHAR_COUNT : 1;
        beginColorEditor(rowToColor[local], count, menuRowName(row),
                         local == 3 ? kTimerNames : nullptr);
    } else if (row == 12) {
        beginColorEditor(SUSAMUNE_CREATION_SHINE_OUTFIT, 1, menuRowName(row));
    } else if (row == 13) {
        beginColorEditor(SUSAMUNE_CREATION_MARIO_HAT, 1, menuRowName(row));
    } else if (row >= 15 && row <= 23) {
        const int local = row - 15;
        const int word = local / 3;
        if (local % 3 == 0) beginKeyboard(word);
        else if (local % 3 == 1) beginWordEditor(word);
        else {
            mWordVisible[word] = !mWordVisible[word];
            mDirty = true;
        }
    } else if (row == 25) {
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
                                  ? kDefaultColors
                                  : mDefaultColors + mEditFirst;
    const CreationStyle defaultStyle = mEditMode == EDIT_WORD_STYLE
                                           ? defaultWordStyle(mEditWord)
                                           : mColorStyle;
    const u8 result = mEditor.update(
        pad, defaultStyle, defaults,
        mEditMode == EDIT_COLOR ? mEditCount : 1);
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
        applyMario();
    }
    if (result & CreationEditor::UPDATE_CANCELLED) {
        mDirty = mDirtyBeforeEdit;
        if (mEditMode == EDIT_COLOR) {
            mColorPresent = mColorPresentBeforeEdit;
            const u32 savedPresent = mColorPresent;
            for (int i = 0; i < mEditCount; i++)
                mColorPresent |= SUSAMUNE_CREATION_COLOR(mEditFirst + i);
            applyHud();
            applyMario();
            mColorPresent = savedPresent;
        }
    }
    if (!mEditor.editing()) mEditMode = EDIT_NONE;
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
    const int columns = mKeyboardPage ? 9 : 8;
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
    const int columns = mKeyboardPage ? 9 : 8;
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
    CreationStyle preview = mColorStyle;
    preview.x = 170;
    preview.y = 96;
    preview.scale = 120;
    preview.textA = 255;
    preview.textBrightness = 100;
    preview.padding = 0xff;
    const u16 selected = mEditor.target() ? mEditor.target() - 1 : 0xffff;
    const char *sample = mEditCount == SUSAMUNE_CREATION_TIMER_CHAR_COUNT
                             ? "0123456789:::" : mEditTitle;
    Creation::drawTextBox(menu, preview, mColors + mEditFirst, mEditCount,
                          sample, false, selected);
    mEditor.draw(menu, mEditTitle, sample);
}
