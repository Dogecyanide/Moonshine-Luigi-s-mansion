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

const u32 kHudPaneTags[] = {
    'w_tx', 't_ba', 'c_ba', 'r_ba', 'b_ba', 'm_ba', 's_ba', 'm_tx',
    '\0m_x', 'm_n1', 'm_n2', 'm_n3', 't_n1', 't_n2', 't_n3', 't_n4',
    't_n5', 't_n6', 't_n7', 't_n8', 't_n9', 't_n0', 't_c1', 't_c2',
    't_c3',
};
const u8 kHudPaneColors[] = {
    SUSAMUNE_CREATION_WATER_TEXT, SUSAMUNE_CREATION_TIMER_BG,
    SUSAMUNE_CREATION_COIN_BG, SUSAMUNE_CREATION_RED_BG,
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
};
static_assert(sizeof(kHudPaneTags) / sizeof(kHudPaneTags[0]) ==
                  CreationExtras::HUD_PANE_COUNT,
              "HUD pane cache changed");
static_assert(sizeof(kHudPaneColors) / sizeof(kHudPaneColors[0]) ==
                  CreationExtras::HUD_PANE_COUNT,
              "HUD colour table changed");

constexpr char kTimerNames[] =
    "Normal digit 1\0Normal digit 2\0Normal digit 3\0Normal digit 4\0"
    "Normal digit 5\0Normal digit 6\0Rush digit 1\0Rush digit 2\0"
    "Rush digit 3\0Rush digit 4\0Separator 1\0Separator 2\0Separator 3";

constexpr char kMenuText[] =
    "HUD\0WATER / ACQUA text\0FLUDD water\0Sunshine timer streak\0"
    "Sunshine timer characters\0Sunshine timer size\0Coin streak\0Red coin streak\0"
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
    // count/table. The colour block pointer is at material + 0x20.
    const u16 count = *reinterpret_cast<u16 *>(
        reinterpret_cast<u8 *>(data) + 0x24);
    if (index >= count) return nullptr;
    J3DMaterial **materials = *reinterpret_cast<J3DMaterial ***>(
        reinterpret_cast<u8 *>(data) + 0x28);
    if (!materials || !materials[index]) return nullptr;
    u8 *block = *reinterpret_cast<u8 **>(
        reinterpret_cast<u8 *>(materials[index]) + 0x20);
    return block ? reinterpret_cast<J3DGXColor *>(block + 4) : nullptr;
}

void tintMaterial(J3DModelData *data, u16 index, const u8 rgb[3]) {
    J3DGXColor *color = materialColor(data, index);
    if (!color) return;
    color->rgba.r = rgb[0];
    color->rgba.g = rgb[1];
    color->rgba.b = rgb[2];
}

bool sameRgb(const u8 a[3], const u8 b[3]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

bool materialMatches(J3DModelData *data, u16 index, const u8 rgb[3]) {
    J3DGXColor *color = materialColor(data, index);
    return color && color->rgba.r == rgb[0] && color->rgba.g == rgb[1] &&
           color->rgba.b == rgb[2];
}

void copyColor(u8 dst[4], const J3DGXColor *src) {
    if (!src) return;
    dst[0] = src->rgba.r;
    dst[1] = src->rgba.g;
    dst[2] = src->rgba.b;
    dst[3] = src->rgba.a;
}

void restoreMaterial(J3DModelData *data, u16 index, const u8 rgba[4]) {
    J3DGXColor *color = materialColor(data, index);
    if (!color) return;
    color->rgba.r = rgba[0];
    color->rgba.g = rgba[1];
    color->rgba.b = rgba[2];
    color->rgba.a = rgba[3];
}

void rebuildModel(J3DModel *model) {
    if (!model) return;
    model->unlock();
    model->makeDL();
    model->lock();
}

J2DPane *timerPane(J2DPicture *const pictures[], J2DPane *text, u32 index) {
    if (index == 0) return pictures[1];
    if (index == 1) return text;
    return pictures[index + 10];
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
    mTimerText = nullptr;
    mHudScreen = nullptr;
    mColorStyle = CreationStyle{0, 0, 100, 255, 0, 0, 0, 0xff, 100, 0xff};
    mMarioBody = nullptr;
    mMarioCaps[0] = mMarioCaps[1] = nullptr;
    mEditor.reset();
    mEditTitle = nullptr;
    mEditMode = EDIT_NONE;
    mTimerAppliedScale = 100;
    mMarioTouched = 0;
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
    mColorStyle.scale = src->timerScale >= 50 && src->timerScale <= 200
                            ? src->timerScale : 100;
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
    dst->timerScale = mColorStyle.scale;
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
    mMarioBody = nullptr;
    mMarioCaps[0] = mMarioCaps[1] = nullptr;
    mMarioTouched =
        (mColorPresent & SUSAMUNE_CREATION_COLOR(
             SUSAMUNE_CREATION_SHINE_OUTFIT) ? 1 : 0) |
        (mColorPresent & SUSAMUNE_CREATION_COLOR(
             SUSAMUNE_CREATION_MARIO_HAT) ? 2 : 0);
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

    mTimerText = screen->search('t_tx');
    for (u32 i = 0; i < TIMER_PANE_COUNT; i++) {
        J2DPane *pane = timerPane(mHudPictures, mTimerText, i);
        if (pane) mTimerPaneBase[i] = pane->mRect;
    }

    J2DPicture *waterText = mHudPictures[0];
    if (waterText) {
        mWaterTextOverlay[0] = waterText->mColorOverlay.r;
        mWaterTextOverlay[1] = waterText->mColorOverlay.g;
        mWaterTextOverlay[2] = waterText->mColorOverlay.b;
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

    if (gpMarioOriginal && gpMarioOriginal->mModelData &&
        gpMarioOriginal->mModelData->mModel) {
        mMarioBody = gpMarioOriginal->mModelData->mModel;
        copyColor(mMarioDefault[0], materialColor(mMarioBody->mModelData, 2));
        copyColor(mMarioDefault[1], materialColor(mMarioBody->mModelData, 4));
        copyColor(mMarioDefault[2], materialColor(mMarioBody->mModelData, 9));
        if (!(mColorPresent & SUSAMUNE_CREATION_COLOR(
                  SUSAMUNE_CREATION_SHINE_OUTFIT))) {
            copyRgb(mDefaultColors[SUSAMUNE_CREATION_SHINE_OUTFIT],
                    mMarioDefault[0]);
            copyRgb(mColors[SUSAMUNE_CREATION_SHINE_OUTFIT],
                    mMarioDefault[0]);
        }
        if (!(mColorPresent & SUSAMUNE_CREATION_COLOR(
                  SUSAMUNE_CREATION_MARIO_HAT))) {
            copyRgb(mDefaultColors[SUSAMUNE_CREATION_MARIO_HAT],
                    mMarioDefault[2]);
            copyRgb(mColors[SUSAMUNE_CREATION_MARIO_HAT],
                    mMarioDefault[2]);
        }
    }
    if (gpMarioOriginal && gpMarioOriginal->mCap) {
        mMarioCaps[0] = gpMarioOriginal->mCap->mCap1;
        mMarioCaps[1] = gpMarioOriginal->mCap->mCap3;
        for (int i = 0; i < 2; i++)
            copyColor(mMarioDefault[3 + i],
                      mMarioCaps[i]
                          ? materialColor(mMarioCaps[i]->mModelData, 0)
                          : nullptr);
    }
    applyHud();
    applyMario();
    applyTimerScale();
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
    }

    J2DPicture *waterText = mHudPictures[0];
    if (waterText && (mColorPresent & SUSAMUNE_CREATION_COLOR(
                         SUSAMUNE_CREATION_WATER_TEXT))) {
        const u8 *rgb = mColors[SUSAMUNE_CREATION_WATER_TEXT];
        if (sameRgb(rgb, mDefaultColors[SUSAMUNE_CREATION_WATER_TEXT])) {
            waterText->mColorOverlay.r = mWaterTextOverlay[0];
            waterText->mColorOverlay.g = mWaterTextOverlay[1];
            waterText->mColorOverlay.b = mWaterTextOverlay[2];
        } else {
            waterText->mColorOverlay.r = rgb[0];
            waterText->mColorOverlay.g = rgb[1];
            waterText->mColorOverlay.b = rgb[2];
        }
    }

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

void CreationExtras::applyMario() {
    if (!gpMarioOriginal || !mMarioBody) return;
    const bool shine = gpMarioOriginal->mAttributes.mIsShineShirt;
    const bool outfitPresent = mColorPresent & SUSAMUNE_CREATION_COLOR(
        SUSAMUNE_CREATION_SHINE_OUTFIT);
    const bool hatPresent = mColorPresent & SUSAMUNE_CREATION_COLOR(
        SUSAMUNE_CREATION_MARIO_HAT);
    const bool outfitActive = outfitPresent || (mMarioTouched & 1);
    const bool hatActive = hatPresent || (mMarioTouched & 2);
    if (!outfitActive && !hatActive) return;
    const u8 *outfit = mColors[SUSAMUNE_CREATION_SHINE_OUTFIT];
    const u8 *hat = mColors[SUSAMUNE_CREATION_MARIO_HAT];
    const bool customOutfit = outfitPresent && shine && !sameRgb(
        outfit, mDefaultColors[SUSAMUNE_CREATION_SHINE_OUTFIT]);
    const bool customHat = hatPresent && !sameRgb(
        hat, mDefaultColors[SUSAMUNE_CREATION_MARIO_HAT]);
    bool bodyChanged = false;
    if (outfitActive) {
        const u8 *first = customOutfit ? outfit : mMarioDefault[0];
        const u8 *second = customOutfit ? outfit : mMarioDefault[1];
        if (!materialMatches(mMarioBody->mModelData, 2, first) ||
            !materialMatches(mMarioBody->mModelData, 4, second)) {
            if (customOutfit) {
                tintMaterial(mMarioBody->mModelData, 2, outfit);
                tintMaterial(mMarioBody->mModelData, 4, outfit);
            } else {
                restoreMaterial(mMarioBody->mModelData, 2, mMarioDefault[0]);
                restoreMaterial(mMarioBody->mModelData, 4, mMarioDefault[1]);
            }
            bodyChanged = true;
        }
    }
    if (hatActive) {
        const u8 *bodyHat = customHat ? hat : mMarioDefault[2];
        if (!materialMatches(mMarioBody->mModelData, 9, bodyHat)) {
            if (customHat)
                tintMaterial(mMarioBody->mModelData, 9, hat);
            else
                restoreMaterial(mMarioBody->mModelData, 9, mMarioDefault[2]);
            bodyChanged = true;
        }
    }
    if (bodyChanged) rebuildModel(mMarioBody);

    for (int i = 0; i < 2; i++) {
        if (!mMarioCaps[i] || !hatActive) continue;
        const u8 *cap = customHat ? hat : mMarioDefault[3 + i];
        if (materialMatches(mMarioCaps[i]->mModelData, 0, cap)) continue;
        if (customHat)
            tintMaterial(mMarioCaps[i]->mModelData, 0, hat);
        else
            restoreMaterial(mMarioCaps[i]->mModelData, 0,
                            mMarioDefault[3 + i]);
        rebuildModel(mMarioCaps[i]);
    }
    if (!outfitPresent) mMarioTouched &= ~1;
    if (!hatPresent) mMarioTouched &= ~2;
}

void CreationExtras::prepareUpdate() {
    if (!gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL ||
        !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != mHudScreen) return;
    // Rebuild locked model display lists before this frame can submit them.
    applyMario();
    J2DPane *anchorPane = timerPane(mHudPictures, mTimerText, 0);
    if (!anchorPane) return;
    const int anchorX = anchorPane->mRect.mX1;
    const int anchorY = anchorPane->mRect.mY1;
    for (u32 i = 0; i < TIMER_PANE_COUNT; i++)
        if (J2DPane *pane = timerPane(mHudPictures, mTimerText, i)) {
            JUTRect &r = pane->mRect;
            JUTRect &base = mTimerPaneBase[i];
            const int scale = mTimerAppliedScale;
            if (r.mX1 != anchorX + (base.mX1 - anchorX) * scale / 100 ||
                r.mY1 != anchorY + (base.mY1 - anchorY) * scale / 100 ||
                r.mX2 != anchorX + (base.mX2 - anchorX) * scale / 100 ||
                r.mY2 != anchorY + (base.mY2 - anchorY) * scale / 100) {
                base.mX1 = anchorX + (r.mX1 - anchorX) * 100 / scale;
                base.mY1 = anchorY + (r.mY1 - anchorY) * 100 / scale;
                base.mX2 = anchorX + (r.mX2 - anchorX) * 100 / scale;
                base.mY2 = anchorY + (r.mY2 - anchorY) * 100 / scale;
            }
            r = base;
        }
}

void CreationExtras::applyTimerScale() {
    const int scale = mColorStyle.scale;
    if (!timerPane(mHudPictures, mTimerText, 0)) return;
    for (u32 i = 0; i < TIMER_PANE_COUNT; i++) {
        J2DPane *pane = timerPane(mHudPictures, mTimerText, i);
        if (!pane) continue;
        mTimerPaneBase[i] = pane->mRect;
    }
    const int anchorX = mTimerPaneBase[0].mX1;
    const int anchorY = mTimerPaneBase[0].mY1;
    for (u32 i = 0; i < TIMER_PANE_COUNT; i++) {
        J2DPane *pane = timerPane(mHudPictures, mTimerText, i);
        if (!pane || scale == 100) continue;
        JUTRect &r = pane->mRect;
        r.mX1 = anchorX + (mTimerPaneBase[i].mX1 - anchorX) * scale / 100;
        r.mY1 = anchorY + (mTimerPaneBase[i].mY1 - anchorY) * scale / 100;
        r.mX2 = anchorX + (mTimerPaneBase[i].mX2 - anchorX) * scale / 100;
        r.mY2 = anchorY + (mTimerPaneBase[i].mY2 - anchorY) * scale / 100;
    }
    mTimerAppliedScale = (u8)scale;
}

void CreationExtras::update() {
    // The cached panes and Mario live in the stage heap. Do not follow them
    // while that heap is being torn down or rebuilt by the setup thread.
    if (!gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL ||
        !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != mHudScreen) return;
    applyHud();
    applyTimerScale();
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
    return row == 0 || row == 12 || row == 15 || row == 25;
}

const char *CreationExtras::menuRowName(int row) {
    return row >= 0 && row < MENU_ROW_COUNT
               ? PackedText::at(kMenuText, row) : "";
}

const char *CreationExtras::menuRowValue(int row) const {
    static char timerScale[5];
    if (row == 5) {
        snprintf(timerScale, sizeof(timerScale), "%u%%", mColorStyle.scale);
        return timerScale;
    }
    if (row >= 16 && row <= 24) {
        const int local = row - 16;
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
    static const u8 rowToColor[] = {
        SUSAMUNE_CREATION_WATER_TEXT, SUSAMUNE_CREATION_FLUDD_WATER,
        SUSAMUNE_CREATION_TIMER_BG, SUSAMUNE_CREATION_TIMER_CHAR_FIRST,
        SUSAMUNE_CREATION_COIN_BG, SUSAMUNE_CREATION_RED_BG,
        SUSAMUNE_CREATION_BLUE_BG, SUSAMUNE_CREATION_LIVES_BG,
        SUSAMUNE_CREATION_SHINES_BG, SUSAMUNE_CREATION_LIFE_TEXT,
    };
    if (row >= 1 && row <= 4) {
        const int local = row - 1;
        const int count = local == 3 ? SUSAMUNE_CREATION_TIMER_CHAR_COUNT : 1;
        beginColorEditor(rowToColor[local], count, menuRowName(row),
                         local == 3 ? kTimerNames : nullptr);
    } else if (row == 5) {
        mColorStyle.scale = (u8)clampi(
            (int)mColorStyle.scale + direction * 5, 50, 200);
        mDirty = true;
    } else if (row >= 6 && row <= 11) {
        beginColorEditor(rowToColor[row - 2], 1, menuRowName(row));
    } else if (row == 13) {
        beginColorEditor(SUSAMUNE_CREATION_SHINE_OUTFIT, 1, menuRowName(row));
    } else if (row == 14) {
        beginColorEditor(SUSAMUNE_CREATION_MARIO_HAT, 1, menuRowName(row));
    } else if (row >= 16 && row <= 24) {
        const int local = row - 16;
        const int word = local / 3;
        if (local % 3 == 0) beginKeyboard(word);
        else if (local % 3 == 1) beginWordEditor(word);
        else {
            mWordVisible[word] = !mWordVisible[word];
            mDirty = true;
        }
    } else if (row == 26) {
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
                if (slot == SUSAMUNE_CREATION_SHINE_OUTFIT)
                    mMarioTouched |= 1;
                else if (slot == SUSAMUNE_CREATION_MARIO_HAT)
                    mMarioTouched |= 2;
            }
        }
        mDirty = true;
        applyHud();
    }
    if (result & CreationEditor::UPDATE_CANCELLED) {
        mDirty = mDirtyBeforeEdit;
        if (mEditMode == EDIT_COLOR) {
            mColorPresent = mColorPresentBeforeEdit;
            const u32 savedPresent = mColorPresent;
            for (int i = 0; i < mEditCount; i++)
                mColorPresent |= SUSAMUNE_CREATION_COLOR(mEditFirst + i);
            applyHud();
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
    CreationStyle preview = mColorStyle;
    preview.x = 170;
    preview.y = 96;
    preview.scale = 120;
    preview.textA = 255;
    preview.textBrightness = 100;
    preview.padding = 0xff;
    const u16 selected = mEditor.target() ? mEditor.target() - 1 : 0xffff;
    if (mEditFirst >= SUSAMUNE_CREATION_TIMER_BG &&
        mEditFirst <= SUSAMUNE_CREATION_SHINES_BG) {
        for (u32 i = 0; i < HUD_PANE_COUNT; i++) {
            if (kHudPaneColors[i] != mEditFirst || !mHudPictures[i]) continue;
            mHudPictures[i]->draw(140, 78, 360, 58, false, false, false);
            break;
        }
    }
    const char *sample = mEditCount == SUSAMUNE_CREATION_TIMER_CHAR_COUNT
                             ? "0123456789:::" : mEditTitle;
    Creation::drawTextBox(menu, preview, mColors + mEditFirst, mEditCount,
                          sample, false, selected);
    mEditor.draw(menu, mEditTitle, sample);
}
