// =====================================================================
// input_display.cpp
//
// Native, live-editable controller overlay. Its default proportions and
// palette follow sup39's Controller Input Display as represented by BitPatty's
// Apache-2.0 gct-generator; the renderer itself is a Susamune implementation
// using the menu's existing J2D/GX primitives rather than a Gecko-code blob.
// =====================================================================

#include "susamune/input_display.hxx"

#include "Dolphin/PAD.h"
#include "Dolphin/printf.h"
#include "JSystem/JUtility/JUTGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "susamune/binds.hxx"
#include "susamune/menu.hxx"

namespace {

typedef JUtility::TColor Color;

const int kDesignW = 182;
const int kDesignH = 120;
const int kValueLineH = 14;
const int kSafeBottom = 456;
const u8  kOriginalLineWidth = 20;

inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline Color color(u8 r, u8 g, u8 b, u8 a) { return Color(r, g, b, a); }

struct Painter {
    Menu                          *menu;
    const SusamuneInputDisplayCfg *cfg;

    int scale(int v) const { return v * (int)cfg->scale / 100; }
    int x(int v) const { return (int)cfg->x + scale(v); }
    int y(int v) const { return (int)cfg->y + scale(v); }

    Color lit(u8 r, u8 g, u8 b, u8 a) const {
        int q = cfg->brightness;
        return color((u8)clampi((int)r * q / 100, 0, 255),
                     (u8)clampi((int)g * q / 100, 0, 255),
                     (u8)clampi((int)b * q / 100, 0, 255), a);
    }

    void box(int lx, int ly, int lw, int lh, Color c) const {
        menu->fillBox(x(lx), y(ly), scale(lw), scale(lh), c);
    }

    void circleVertices(int lx, int ly, int lr, s16 *xy) const {
        // sup39's renderer uses 32-sided n-gons. These fixed-point unit-circle
        // points reproduce that silhouette without needing a runtime trig
        // implementation in the injected mod.
        static const s16 ux[32] = {
            1000, 981, 924, 831, 707, 556, 383, 195,
            0, -195, -383, -556, -707, -831, -924, -981,
            -1000, -981, -924, -831, -707, -556, -383, -195,
            0, 195, 383, 556, 707, 831, 924, 981
        };
        static const s16 uy[32] = {
            0, 195, 383, 556, 707, 831, 924, 981,
            1000, 981, 924, 831, 707, 556, 383, 195,
            0, -195, -383, -556, -707, -831, -924, -981,
            -1000, -981, -924, -831, -707, -556, -383, -195
        };
        int cx = x(lx);
        int cy = y(ly);
        int r  = scale(lr);
        for (int i = 0; i < 32; i++) {
            xy[i * 2]     = (s16)(cx + r * ux[i] / 1000);
            xy[i * 2 + 1] = (s16)(cy + r * uy[i] / 1000);
        }
    }

    void fillCircle(int lx, int ly, int lr, Color c) const {
        s16 xy[64];
        circleVertices(lx, ly, lr, xy);
        menu->fillPoly(xy, 32, c);
    }

    void strokeCircle(int lx, int ly, int lr, Color c) const {
        s16 xy[64];
        circleVertices(lx, ly, lr, xy);
        menu->strokePoly(xy, 32, kOriginalLineWidth, c);
    }

    void button(int lx, int ly, int radius, bool down,
                u8 r, u8 g, u8 b) const {
        const Color c = lit(r, g, b, 0xbf);
        // The original display is an outline at rest and gains a solid fill
        // only for the frames where the button is held.
        if (down) fillCircle(lx, ly, radius, c);
        strokeCircle(lx, ly, radius, c);
    }
};

int valueLines(const SusamuneInputDisplayCfg &cfg) {
    if (cfg.valueMode == SUSAMUNE_INPUT_VALUES_STICKS) return 2;
    if (cfg.valueMode == SUSAMUNE_INPUT_VALUES_FULL) return 3;
    return 0;
}

const char *const kOnOff[] = { "Off", "On" };
const char *const kValueModes[] = { "Off", "Sticks", "Full" };
const char *const kSources[] = { "Raw", "Processed" };
const char *const kPlacements[] = { "Below", "Above", "Inside" };
const char *const kEditChannels[] = {
    "Background alpha", "Background red", "Background green",
    "Background blue", "Display brightness"
};

}  // namespace

InputDisplay gInputDisplay;

void InputDisplay::resetDefaults() {
    mCfg.magic          = SUSAMUNE_INPUT_CFG_MAGIC;
    mCfg.version        = SUSAMUNE_INPUT_CFG_VERSION;
    mCfg.x              = 16;
    mCfg.y              = 314;
    mCfg.startVisible   = 1;
    mCfg.scale          = 100;
    mCfg.bgR            = 0;
    mCfg.bgG            = 0;
    mCfg.bgB            = 0;
    mCfg.bgA            = 0x7f;
    mCfg.brightness     = 100;
    mCfg.valueMode      = SUSAMUNE_INPUT_VALUES_OFF;
    mCfg.valueSource    = SUSAMUNE_INPUT_SOURCE_RAW;
    mCfg.valuePlacement = SUSAMUNE_INPUT_VALUES_BELOW;
    for (u32 i = 0; i < sizeof(mCfg.reserved); i++) mCfg.reserved[i] = 0;

    mVisible          = true;
    mVisibleBeforeEdit = true;
    mDirty            = false;
    mDirtyBeforeEdit  = false;
    mEditing          = false;
    mEditChannel      = 0;
}

void InputDisplay::adopt(const volatile SusamuneInputDisplayCfg *src) {
    if (src->magic != SUSAMUNE_INPUT_CFG_MAGIC ||
        src->version != SUSAMUNE_INPUT_CFG_VERSION) {
        mVisible = mCfg.startVisible != 0;
        return;
    }

    if (src->x != SUSAMUNE_INPUT_CFG_U16_UNSET) mCfg.x = src->x;
    if (src->y != SUSAMUNE_INPUT_CFG_U16_UNSET) mCfg.y = src->y;
    if (src->startVisible != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.startVisible = src->startVisible != 0;
    if (src->scale != SUSAMUNE_INPUT_CFG_U8_UNSET) mCfg.scale = src->scale;
    if (src->bgR != SUSAMUNE_INPUT_CFG_U8_UNSET) mCfg.bgR = src->bgR;
    if (src->bgG != SUSAMUNE_INPUT_CFG_U8_UNSET) mCfg.bgG = src->bgG;
    if (src->bgB != SUSAMUNE_INPUT_CFG_U8_UNSET) mCfg.bgB = src->bgB;
    if (src->bgA != SUSAMUNE_INPUT_CFG_U8_UNSET) mCfg.bgA = src->bgA;
    if (src->brightness != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.brightness = src->brightness;
    if (src->valueMode != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.valueMode = src->valueMode;
    if (src->valueSource != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.valueSource = src->valueSource;
    if (src->valuePlacement != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.valuePlacement = src->valuePlacement;

    mCfg.startVisible   = mCfg.startVisible ? 1 : 0;
    mCfg.scale          = (u8)clampi(mCfg.scale, 50, 150);
    mCfg.brightness     = (u8)clampi(mCfg.brightness, 25, 200);
    mCfg.valueMode      = (u8)clampi(mCfg.valueMode, 0, 2);
    mCfg.valueSource    = (u8)clampi(mCfg.valueSource, 0, 1);
    mCfg.valuePlacement = (u8)clampi(mCfg.valuePlacement, 0, 2);
    clampLayout();
    mVisible = mCfg.startVisible != 0;
    mDirty   = false;
}

void InputDisplay::stageInto(volatile SusamuneInputDisplayCfg *dst) const {
    dst->magic          = SUSAMUNE_INPUT_CFG_MAGIC;
    dst->version        = SUSAMUNE_INPUT_CFG_VERSION;
    dst->x              = mCfg.x;
    dst->y              = mCfg.y;
    dst->startVisible   = mCfg.startVisible;
    dst->scale          = mCfg.scale;
    dst->bgR            = mCfg.bgR;
    dst->bgG            = mCfg.bgG;
    dst->bgB            = mCfg.bgB;
    dst->bgA            = mCfg.bgA;
    dst->brightness     = mCfg.brightness;
    dst->valueMode      = mCfg.valueMode;
    dst->valueSource    = mCfg.valueSource;
    dst->valuePlacement = mCfg.valuePlacement;
    for (u32 i = 0; i < sizeof(dst->reserved); i++) dst->reserved[i] = 0;
}

void InputDisplay::markDirty() {
    mDirty = true;
    clampLayout();
}

void InputDisplay::resetLayout() {
    mCfg.x          = 16;
    mCfg.y          = 314;
    mCfg.scale      = 100;
    mCfg.bgR        = 0;
    mCfg.bgG        = 0;
    mCfg.bgB        = 0;
    mCfg.bgA        = 0x7f;
    mCfg.brightness = 100;
    markDirty();
}

void InputDisplay::clampLayout() {
    int w = kDesignW * (int)mCfg.scale / 100;
    int h = kDesignH * (int)mCfg.scale / 100;
    int lines = valueLines(mCfg);
    int extra = lines * kValueLineH;
    int minY = (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_ABOVE) ? extra : 0;
    int maxY = kSafeBottom - h;
    if (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_BELOW) maxY -= extra;
    if (maxY < minY) maxY = minY;
    mCfg.x = (u16)clampi(mCfg.x, 0, 640 - w);
    mCfg.y = (u16)clampi(mCfg.y, minY, maxY);
}

void InputDisplay::update() {
    if (!mEditing && (!gMenu || !gMenu->shown()) &&
        gBinds.wasPressed(BIND_TOGGLE_INPUT_DISPLAY)) {
        mVisible = !mVisible;
    }
}

const char *InputDisplay::menuRowName(int row) {
    static const char *const names[] = {
        "Input display", "Value readout", "Value source", "Value position",
        "Edit layout", "Reset layout"
    };
    return (row >= 0 && row < menuRowCount()) ? names[row] : "";
}

const char *InputDisplay::menuRowValue(int row) const {
    switch (row) {
    case 0: return kOnOff[mCfg.startVisible ? 1 : 0];
    case 1: return kValueModes[mCfg.valueMode];
    case 2: return kSources[mCfg.valueSource];
    case 3: return kPlacements[mCfg.valuePlacement];
    case 4: return "Open";
    case 5: return "Default";
    default: return "";
    }
}

void InputDisplay::adjustMenuRow(int row, int dir) {
    if (dir == 0) dir = 1;
    switch (row) {
    case 0:
        mCfg.startVisible = !mCfg.startVisible;
        mVisible = mCfg.startVisible != 0;
        markDirty();
        break;
    case 1:
        mCfg.valueMode = (u8)((mCfg.valueMode + (dir > 0 ? 1 : 2)) % 3);
        markDirty();
        break;
    case 2:
        mCfg.valueSource = !mCfg.valueSource;
        markDirty();
        break;
    case 3:
        mCfg.valuePlacement = (u8)((mCfg.valuePlacement + (dir > 0 ? 1 : 2)) % 3);
        markDirty();
        break;
    case 4:
        beginEditor();
        break;
    case 5:
        resetLayout();
        break;
    }
}

void InputDisplay::beginEditor() {
    if (mEditing) return;
    mEditBackup       = mCfg;
    mDirtyBeforeEdit  = mDirty;
    mVisibleBeforeEdit = mVisible;
    mVisible          = true;
    mEditing          = true;
    mEditChannel      = 0;
}

void InputDisplay::finishEditor(bool keep) {
    if (!keep) {
        mCfg   = mEditBackup;
        mDirty = mDirtyBeforeEdit;
    }
    mVisible = mVisibleBeforeEdit;
    mEditing = false;
}

void InputDisplay::updateEditor(TMarioGamePad *pad) {
    u32 rapid = pad->mButtons.mRapidInput;

    if (rapid & TMarioGamePad::A) {
        finishEditor(true);
        return;
    }
    if (rapid & TMarioGamePad::B) {
        finishEditor(false);
        return;
    }
    if (rapid & TMarioGamePad::Z) {
        resetLayout();
        return;
    }

    bool changed = false;
    if (rapid & TMarioGamePad::DPAD_LEFT)  {
        mCfg.x = (u16)clampi((int)mCfg.x - 2, 0, 640); changed = true;
    }
    if (rapid & TMarioGamePad::DPAD_RIGHT) {
        mCfg.x = (u16)clampi((int)mCfg.x + 2, 0, 640); changed = true;
    }
    if (rapid & TMarioGamePad::DPAD_UP)    {
        mCfg.y = (u16)clampi((int)mCfg.y - 2, 0, 480); changed = true;
    }
    if (rapid & TMarioGamePad::DPAD_DOWN)  {
        mCfg.y = (u16)clampi((int)mCfg.y + 2, 0, 480); changed = true;
    }

    if (rapid & TMarioGamePad::L) {
        mCfg.scale = (u8)clampi((int)mCfg.scale - 2, 50, 150);
        changed = true;
    }
    if (rapid & TMarioGamePad::R) {
        mCfg.scale = (u8)clampi((int)mCfg.scale + 2, 50, 150);
        changed = true;
    }
    if (rapid & TMarioGamePad::START) {
        mEditChannel = (u8)((mEditChannel + 1) % 5);
    }
    int delta = 0;
    if (rapid & TMarioGamePad::X) delta = -8;
    if (rapid & TMarioGamePad::Y) delta = 8;
    if (delta) {
        u8 *field = nullptr;
        if (mEditChannel == 0) field = &mCfg.bgA;
        if (mEditChannel == 1) field = &mCfg.bgR;
        if (mEditChannel == 2) field = &mCfg.bgG;
        if (mEditChannel == 3) field = &mCfg.bgB;
        if (field) {
            *field = (u8)clampi((int)*field + delta, 0, 254);
        } else {
            mCfg.brightness = (u8)clampi((int)mCfg.brightness + delta, 25, 200);
        }
        changed = true;
    }

    if (changed) markDirty();
}

void InputDisplay::draw(Menu *menu, bool force) const {
    if ((!mVisible && !force) || !menu) return;

    const PADStatus &raw = JUTGamePad::mPadStatus[0];
    const u16 buttons = raw.mButton;
    Painter p = { menu, &mCfg };

    p.box(0, 0, kDesignW, kDesignH, color(mCfg.bgR, mCfg.bgG, mCfg.bgB, mCfg.bgA));

    // Analog trigger bars, with the digital click guaranteeing a full bar.
    int l = raw.mTriggerLeft;
    int r = raw.mTriggerRight;
    if (buttons & JUTGamePad::L) l = 255;
    if (buttons & JUTGamePad::R) r = 255;
    // Analog travel fills 56 of the 64 design units; a digital click fills
    // the complete outlined trigger, exactly like the Gecko original.
    const int lFill = (buttons & JUTGamePad::L) ? 64 : 56 * l / 255;
    const int rFill = (buttons & JUTGamePad::R) ? 64 : 56 * r / 255;
    p.box(12, 10, lFill, 8, p.lit(223, 223, 223, 0xbf));
    p.box(170 - rFill, 10, rFill, 8, p.lit(223, 223, 223, 0xbf));
    const Color triggerStroke = p.lit(238, 238, 238, 0xbf);
    s16 trigger[8] = {
        (s16)p.x(12), (s16)p.y(10), (s16)p.x(76), (s16)p.y(10),
        (s16)p.x(76), (s16)p.y(18), (s16)p.x(12), (s16)p.y(18)
    };
    menu->strokePoly(trigger, 4, kOriginalLineWidth, triggerStroke);
    trigger[0] = trigger[6] = (s16)p.x(106);
    trigger[2] = trigger[4] = (s16)p.x(170);
    menu->strokePoly(trigger, 4, kOriginalLineWidth, triggerStroke);

    // Main and C sticks: a fixed gate ring plus a knob moved by the raw sample.
    const int mx = clampi((s8)raw.mStickX, -100, 100) * 14 / 100;
    const int my = clampi((s8)raw.mStickY, -100, 100) * 14 / 100;
    const int cx = clampi((s8)raw.mSubStickX, -100, 100) * 14 / 100;
    const int cy = clampi((s8)raw.mSubStickY, -100, 100) * 14 / 100;
    p.fillCircle(32 + mx, 52 - my, 12, p.lit(238, 238, 238, 0xef));
    p.strokeCircle(32, 52, 19, p.lit(238, 238, 238, 0xef));
    p.fillCircle(64 + cx, 92 - cy, 12, p.lit(255, 211, 0, 0xef));
    p.strokeCircle(64, 92, 19, p.lit(255, 211, 0, 0xef));

    p.button(138, 66, 18, buttons & JUTGamePad::A, 46, 229, 184);
    p.button(113, 89, 9, buttons & JUTGamePad::B, 255, 26, 26);
    p.button(164, 50, 8, buttons & JUTGamePad::X, 238, 238, 238);
    p.button(119, 41, 8, buttons & JUTGamePad::Y, 238, 238, 238);
    p.button(144, 34, 6, buttons & JUTGamePad::Z, 148, 148, 255);
    p.button(91, 64, 5, buttons & JUTGamePad::START, 238, 238, 238);

    int lines = valueLines(mCfg);
    if (lines == 0) return;

    int textSize = clampi(10 * (int)mCfg.scale / 100, 8, 14);
    int graphicH = p.scale(kDesignH);
    int textY;
    if (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_ABOVE) {
        textY = (int)mCfg.y - lines * kValueLineH;
    } else if (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_INSIDE) {
        textY = (int)mCfg.y + graphicH - lines * kValueLineH - 2;
    } else {
        textY = (int)mCfg.y + graphicH;
    }
    menu->fillBox(mCfg.x, textY, p.scale(kDesignW), lines * kValueLineH,
                  color(mCfg.bgR, mCfg.bgG, mCfg.bgB, mCfg.bgA));

    int mainX, mainY, cX, cY, triggerL, triggerR;
    TMarioGamePad *pad = gpApplication.mGamePads[0];
    if (mCfg.valueSource == SUSAMUNE_INPUT_SOURCE_PROCESSED && pad) {
        mainX = clampi((int)(pad->mControlStick.mStickX * 100.0f), -100, 100);
        mainY = clampi((int)(pad->mControlStick.mStickY * 100.0f), -100, 100);
        cX = clampi((int)(pad->mCStick.mStickX * 100.0f), -100, 100);
        cY = clampi((int)(pad->mCStick.mStickY * 100.0f), -100, 100);
        triggerL = clampi((int)(pad->mButtons.mAnalogL * 100.0f), 0, 100);
        triggerR = clampi((int)(pad->mButtons.mAnalogR * 100.0f), 0, 100);
    } else {
        mainX = (s8)raw.mStickX;
        mainY = (s8)raw.mStickY;
        cX = (s8)raw.mSubStickX;
        cY = (s8)raw.mSubStickY;
        triggerL = raw.mTriggerLeft;
        triggerR = raw.mTriggerRight;
    }

    char text[48];
    snprintf(text, sizeof(text), "M  X:%+04d  Y:%+04d", mainX, mainY);
    menu->drawText(text, mCfg.x + 4, textY + 2, textSize, textSize,
                   p.lit(255, 255, 255, 255));
    snprintf(text, sizeof(text), "C  X:%+04d  Y:%+04d", cX, cY);
    menu->drawText(text, mCfg.x + 4, textY + kValueLineH + 1,
                   textSize, textSize, p.lit(255, 255, 255, 255));
    if (lines == 3) {
        snprintf(text, sizeof(text), "L:%03d  R:%03d", triggerL, triggerR);
        menu->drawText(text, mCfg.x + 4, textY + kValueLineH * 2 + 1,
                       textSize, textSize, p.lit(255, 255, 255, 255));
    }
}

void InputDisplay::drawEditor(Menu *menu) const {
    draw(menu, true);

    char status[112];
    // Keep every editor instruction in the title-safe upper area. The old
    // footer reached y=472 and was clipped by real PAL output/capture paths.
    menu->fillBox(8, 8, 624, 88, color(0, 0, 0, 205));
    snprintf(status, sizeof(status),
             "X:%u Y:%u  Size:%u%%  BG:%03u,%03u,%03u  A:%03u  Bright:%u%%",
             mCfg.x, mCfg.y, mCfg.scale, mCfg.bgR, mCfg.bgG, mCfg.bgB,
             mCfg.bgA, mCfg.brightness);
    menu->drawText("Input Display editor", 18, 15, 16, 16, color(255, 255, 255, 255));
    menu->drawText(status, 18, 38, 11, 11, color(190, 220, 255, 255));
    snprintf(status, sizeof(status), "START Field: %s   X -   Y +",
             kEditChannels[mEditChannel]);
    menu->drawText(status, 18, 57, 11, 11, color(255, 255, 255, 255));
    menu->drawText("D-pad Move   L/R Size   A Save   B Cancel   Z Reset",
                   18, 76, 11, 11, color(255, 255, 255, 255));
}
