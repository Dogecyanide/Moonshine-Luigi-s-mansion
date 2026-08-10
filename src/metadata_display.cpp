// Native metadata overlay based on sup39's Customized Display data set.
// The in-game editor stays deliberately small; arbitrary labels and layout
// are available through one optional template in susamune.ini.

#include "susamune/metadata_display.hxx"

#include "Dolphin/printf.h"
#include "SMS/Camera/PolarSubCamera.hxx"
#include "SMS/Manager/PollutionManager.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/menu.hxx"

namespace {

typedef JUtility::TColor Color;

const int kBaseTextSize = 14;
const int kSafeBottom   = 456;

const char *const kOnOff[]      = { "Off", "On" };
const char *const kLabelModes[] = { "Short", "Long", "Custom" };

const char *const kShortLabels[MetadataDisplay::FIELD_COUNT] = {
    "X", "Y", "Z", "A", "H", "V", "QF", "CA", "IF", "G", "SP"
};

const char *const kLongLabels[MetadataDisplay::FIELD_COUNT] = {
    "X Position", "Y Position", "Z Position", "Mario Angle",
    "Horizontal Speed", "Vertical Speed", "QF Offset", "Camera Angle",
    "Invincibility Frames", "Pollution Degree", "Spin Condition"
};

const u16 kFieldBits[MetadataDisplay::FIELD_COUNT] = {
    SUSAMUNE_METADATA_FIELD_X,
    SUSAMUNE_METADATA_FIELD_Y,
    SUSAMUNE_METADATA_FIELD_Z,
    SUSAMUNE_METADATA_FIELD_ANGLE,
    SUSAMUNE_METADATA_FIELD_HSPD,
    SUSAMUNE_METADATA_FIELD_VSPD,
    SUSAMUNE_METADATA_FIELD_QF,
    SUSAMUNE_METADATA_FIELD_CANGLE,
    SUSAMUNE_METADATA_FIELD_INVINC,
    SUSAMUNE_METADATA_FIELD_GOOP,
    SUSAMUNE_METADATA_FIELD_SPIN
};

const char kDefaultFormat[] =
    "X <x|.0>\\nY <y|.0>\\nZ <z|.0>\\nA <angle>\\n"
    "H <HSpd|.2>\\nV <VSpd|.2>\\nQF <QF>\\nCA <CAngle>\\n"
    "IF <invinc>\\nG <goop>\\nSP <spin>";

inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int textSize(const SusamuneMetadataDisplayCfg &cfg) {
    return clampi(kBaseTextSize * (int)cfg.scale / 100, 8, 22);
}

int enabledLineCount(const SusamuneMetadataDisplayCfg &cfg) {
    if (cfg.labelMode == SUSAMUNE_METADATA_LABEL_CUSTOM) {
        int lines = 1;
        for (u32 i = 0; i < SUSAMUNE_METADATA_FORMAT_SIZE && cfg.format[i]; i++) {
            if (cfg.format[i] == '\\' && i + 1 < SUSAMUNE_METADATA_FORMAT_SIZE &&
                cfg.format[i + 1] == '\\') {
                i++;
            } else if (cfg.format[i] == '\n' ||
                       (cfg.format[i] == '\\' && i + 1 < SUSAMUNE_METADATA_FORMAT_SIZE &&
                        cfg.format[i + 1] == 'n')) {
                lines++;
                if (cfg.format[i] == '\\') i++;
            }
        }
        return lines;
    }

    int lines = 0;
    for (int i = 0; i < MetadataDisplay::FIELD_COUNT; i++) {
        if (cfg.fieldMask & kFieldBits[i]) lines++;
    }
    return lines;
}

struct Values {
    f32 x;
    f32 y;
    f32 z;
    f32 hSpd;
    f32 vSpd;
    u16 angle;
    u16 cameraAngle;
    u8  qf;
    s16 invinc;
    u32 goop;
    bool spin;
};

Values readValues() {
    Values v = {};
    TMario *mario = gpMarioOriginal;
    if (!mario) return v;

    v.x      = mario->mTranslation.x;
    v.y      = mario->mTranslation.y;
    v.z      = mario->mTranslation.z;
    v.angle  = (u16)mario->mAngle.y;
    v.hSpd   = mario->mForwardSpeed;
    v.vSpd   = mario->mSpeed.y;
    v.qf     = gpMarDirector ? (u8)(gpMarDirector->unk58 & 3) : 0;
    v.cameraAngle = gpCamera ? (u16)(gpCamera->mHorizontalAngle - 0x8000) : 0;
    v.invinc = (s16)(mario->mInvincibilityFrames >> 2);
    // EX maps do not have a meaningful pollution count. Some retain a global
    // manager pointer while their scene resources are being exchanged, so do
    // not traverse its stage-owned layer array there.
    const u8 area = gpMarDirector ? gpMarDirector->mAreaID : 0xFF;
    const bool exMap = area > 0x14 && area < 0x35;
    v.goop = (gpPollution && !exMap) ? gpPollution->getPollutionDegree() : 0;

    int spinDirection = 0;
    v.spin = mario->checkStickRotate(&spinDirection);
    return v;
}

void drawLine(Menu *menu, const char *text, int x, int y, int size) {
    menu->drawText(text, x + 1, y + 1, size, size, Color(0, 0, 0, 220));
    menu->drawText(text, x, y, size, size, Color(255, 255, 255, 255));
}

void formatField(char *out, u32 cap, int field, const char *label, const Values &v) {
    switch (field) {
    case MetadataDisplay::FIELD_X:      snprintf(out, cap, "%s: %.0f", label, v.x); break;
    case MetadataDisplay::FIELD_Y:      snprintf(out, cap, "%s: %.0f", label, v.y); break;
    case MetadataDisplay::FIELD_Z:      snprintf(out, cap, "%s: %.0f", label, v.z); break;
    case MetadataDisplay::FIELD_ANGLE:  snprintf(out, cap, "%s: %u", label, v.angle); break;
    case MetadataDisplay::FIELD_HSPD:   snprintf(out, cap, "%s: %.2f", label, v.hSpd); break;
    case MetadataDisplay::FIELD_VSPD:   snprintf(out, cap, "%s: %.2f", label, v.vSpd); break;
    case MetadataDisplay::FIELD_QF:     snprintf(out, cap, "%s: %u", label, v.qf); break;
    case MetadataDisplay::FIELD_CANGLE: snprintf(out, cap, "%s: %u", label, v.cameraAngle); break;
    case MetadataDisplay::FIELD_INVINC: snprintf(out, cap, "%s: %d", label, (int)v.invinc); break;
    case MetadataDisplay::FIELD_GOOP:   snprintf(out, cap, "%s: %lu", label, v.goop); break;
    case MetadataDisplay::FIELD_SPIN:
        snprintf(out, cap, "%s: %s", label, v.spin ? SUSAMUNE_GLYPH_A : "-");
        break;
    default:
        out[0] = '\0';
        break;
    }
}

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool tokenEquals(const char *s, int len, const char *id) {
    int i = 0;
    for (; i < len && id[i]; i++) {
        if (lower(s[i]) != lower(id[i])) return false;
    }
    return i == len && id[i] == '\0';
}

int tokenField(const char *s, int len) {
    static const char *const ids[MetadataDisplay::FIELD_COUNT] = {
        "x", "y", "z", "angle", "HSpd", "VSpd", "QF", "CAngle",
        "invinc", "goop", "spin"
    };
    while (len > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        len--;
    }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
    for (int i = 0; i < MetadataDisplay::FIELD_COUNT; i++) {
        if (tokenEquals(s, len, ids[i])) return i;
    }
    return -1;
}

int tokenPrecision(const char *s, int len, int fallback) {
    int i = 0;
    if (i < len && s[i] == '%') i++;
    while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    if (i >= len || s[i] != '.') return fallback;
    i++;
    if (i >= len || s[i] < '0' || s[i] > '9') return fallback;
    return clampi(s[i] - '0', 0, 4);
}

void formatValue(char *out, u32 cap, int field, int precision, const Values &v) {
    switch (field) {
    case MetadataDisplay::FIELD_X:      snprintf(out, cap, "%.*f", precision, v.x); break;
    case MetadataDisplay::FIELD_Y:      snprintf(out, cap, "%.*f", precision, v.y); break;
    case MetadataDisplay::FIELD_Z:      snprintf(out, cap, "%.*f", precision, v.z); break;
    case MetadataDisplay::FIELD_ANGLE:  snprintf(out, cap, "%u", v.angle); break;
    case MetadataDisplay::FIELD_HSPD:   snprintf(out, cap, "%.*f", precision, v.hSpd); break;
    case MetadataDisplay::FIELD_VSPD:   snprintf(out, cap, "%.*f", precision, v.vSpd); break;
    case MetadataDisplay::FIELD_QF:     snprintf(out, cap, "%u", v.qf); break;
    case MetadataDisplay::FIELD_CANGLE: snprintf(out, cap, "%u", v.cameraAngle); break;
    case MetadataDisplay::FIELD_INVINC: snprintf(out, cap, "%d", (int)v.invinc); break;
    case MetadataDisplay::FIELD_GOOP:   snprintf(out, cap, "%lu", v.goop); break;
    case MetadataDisplay::FIELD_SPIN:
        snprintf(out, cap, "%s", v.spin ? SUSAMUNE_GLYPH_A : "-");
        break;
    default:
        out[0] = '\0';
        break;
    }
}

void append(char *dst, int &len, int cap, const char *src) {
    while (*src && len + 1 < cap) dst[len++] = *src++;
    dst[len] = '\0';
}

void appendRange(char *dst, int &len, int cap, const char *src, int n) {
    for (int i = 0; i < n && len + 1 < cap; i++) dst[len++] = src[i];
    dst[len] = '\0';
}

bool formatCustomLine(const SusamuneMetadataDisplayCfg &cfg, const Values &v,
                      u32 &i, char *line, int cap) {
    int len = 0;
    line[0] = '\0';

    while (i < SUSAMUNE_METADATA_FORMAT_SIZE && cfg.format[i]) {
        char c = cfg.format[i];
        if (c == '\n' ||
            (c == '\\' && i + 1 < SUSAMUNE_METADATA_FORMAT_SIZE &&
             cfg.format[i + 1] == 'n')) {
            i += (c == '\\') ? 2 : 1;
            return true;
        }
        if (c == '\\' && i + 1 < SUSAMUNE_METADATA_FORMAT_SIZE &&
            cfg.format[i + 1] == '\\') {
            appendRange(line, len, cap, "\\", 1);
            i += 2;
            continue;
        }
        if (c == '<') {
            u32 close = i + 1;
            while (close < SUSAMUNE_METADATA_FORMAT_SIZE && cfg.format[close] &&
                   cfg.format[close] != '>') {
                close++;
            }
            if (close < SUSAMUNE_METADATA_FORMAT_SIZE && cfg.format[close] == '>') {
                const char *body = &cfg.format[i + 1];
                int bodyLen = (int)(close - i - 1);
                int split = 0;
                while (split < bodyLen && body[split] != '|') split++;
                int field = tokenField(body, split);
                if (field >= 0) {
                    int fallback = (field == MetadataDisplay::FIELD_HSPD ||
                                    field == MetadataDisplay::FIELD_VSPD) ? 2 : 0;
                    int precision = fallback;
                    if (split < bodyLen) {
                        int fmtLen = 0;
                        while (split + 1 + fmtLen < bodyLen &&
                               body[split + 1 + fmtLen] != '|') fmtLen++;
                        precision = tokenPrecision(body + split + 1, fmtLen, fallback);
                    }
                    char value[48];
                    formatValue(value, sizeof(value), field, precision, v);
                    append(line, len, cap, value);
                } else {
                    appendRange(line, len, cap, &cfg.format[i],
                                (int)(close - i + 1));
                }
                i = close + 1;
                continue;
            }
        }

        appendRange(line, len, cap, &cfg.format[i], 1);
        i++;
    }
    return false;
}

void drawBackground(Menu *menu, const SusamuneMetadataDisplayCfg &cfg,
                    int width, int height) {
    if (cfg.backgroundAlpha == 0 || width <= 0 || height <= 0) return;
    menu->fillBox(cfg.x - 3, cfg.y - 3, width + 6, height + 6,
                  Color(0, 0, 0, cfg.backgroundAlpha));
}

void drawCustom(Menu *menu, const SusamuneMetadataDisplayCfg &cfg, const Values &v,
                int size, int lineH) {
    char line[192];
    u32 pos = 0;
    int maxWidth = 0;
    int lines = 0;
    bool more;

    do {
        more = formatCustomLine(cfg, v, pos, line, sizeof(line));
        int width = Menu::textWidth(line, size);
        if (width > maxWidth) maxWidth = width;
        lines++;
    } while (more);

    drawBackground(menu, cfg, maxWidth, lines * lineH);

    pos = 0;
    int y = cfg.y;
    do {
        more = formatCustomLine(cfg, v, pos, line, sizeof(line));
        drawLine(menu, line, cfg.x, y, size);
        y += lineH;
    } while (more);
}

}  // namespace

MetadataDisplay gMetadataDisplay;

void MetadataDisplay::resetDefaults() {
    mCfg.magic        = SUSAMUNE_METADATA_CFG_MAGIC;
    mCfg.version      = SUSAMUNE_METADATA_CFG_VERSION;
    mCfg.x            = 16;
    mCfg.y            = 112;
    mCfg.fieldMask    = SUSAMUNE_METADATA_FIELD_ALL;
    mCfg.startVisible = 0;
    mCfg.scale        = 100;
    mCfg.labelMode    = SUSAMUNE_METADATA_LABEL_SHORT;
    mCfg.backgroundAlpha = 0;

    u32 i = 0;
    for (; i + 1 < SUSAMUNE_METADATA_FORMAT_SIZE && kDefaultFormat[i]; i++)
        mCfg.format[i] = kDefaultFormat[i];
    mCfg.format[i] = '\0';
    for (i++; i < SUSAMUNE_METADATA_FORMAT_SIZE; i++) mCfg.format[i] = '\0';

    mDirty          = false;
    mDirtyBeforeEdit = false;
    mEditing        = false;
}

void MetadataDisplay::adopt(const volatile SusamuneMetadataDisplayCfg *src) {
    if (src->magic != SUSAMUNE_METADATA_CFG_MAGIC ||
        src->version != SUSAMUNE_METADATA_CFG_VERSION) {
        return;
    }

    if (src->x != SUSAMUNE_INPUT_CFG_U16_UNSET) mCfg.x = src->x;
    if (src->y != SUSAMUNE_INPUT_CFG_U16_UNSET) mCfg.y = src->y;
    if (src->fieldMask != SUSAMUNE_INPUT_CFG_U16_UNSET) mCfg.fieldMask = src->fieldMask;
    if (src->startVisible != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.startVisible = src->startVisible != 0;
    if (src->scale != SUSAMUNE_INPUT_CFG_U8_UNSET) mCfg.scale = src->scale;
    if (src->labelMode != SUSAMUNE_INPUT_CFG_U8_UNSET) mCfg.labelMode = src->labelMode;
    if (src->backgroundAlpha != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.backgroundAlpha = src->backgroundAlpha;

    if ((u8)src->format[0] != SUSAMUNE_METADATA_FORMAT_UNSET) {
        u32 i = 0;
        for (; i + 1 < SUSAMUNE_METADATA_FORMAT_SIZE && src->format[i]; i++)
            mCfg.format[i] = src->format[i];
        mCfg.format[i] = '\0';
        for (i++; i < SUSAMUNE_METADATA_FORMAT_SIZE; i++) mCfg.format[i] = '\0';
    }

    mCfg.fieldMask &= SUSAMUNE_METADATA_FIELD_ALL;
    mCfg.startVisible = mCfg.startVisible ? 1 : 0;
    mCfg.scale         = (u8)clampi(mCfg.scale, 50, 150);
    mCfg.labelMode     = (u8)clampi(mCfg.labelMode, 0, 2);
    clampLayout();
    mDirty = false;
}

void MetadataDisplay::stageInto(volatile SusamuneMetadataDisplayCfg *dst) const {
    dst->magic        = SUSAMUNE_METADATA_CFG_MAGIC;
    dst->version      = SUSAMUNE_METADATA_CFG_VERSION;
    dst->x            = mCfg.x;
    dst->y            = mCfg.y;
    dst->fieldMask    = mCfg.fieldMask;
    dst->startVisible = mCfg.startVisible;
    dst->scale        = mCfg.scale;
    dst->labelMode    = mCfg.labelMode;
    dst->backgroundAlpha = mCfg.backgroundAlpha;
    for (u32 i = 0; i < SUSAMUNE_METADATA_FORMAT_SIZE; i++)
        dst->format[i] = mCfg.format[i];
}

void MetadataDisplay::markDirty() {
    mDirty = true;
    clampLayout();
}

void MetadataDisplay::resetLayout() {
    mCfg.x     = 16;
    mCfg.y     = 112;
    mCfg.scale = 100;
    mCfg.backgroundAlpha = 0;
    markDirty();
}

void MetadataDisplay::clampLayout() {
    int size  = textSize(mCfg);
    int lines = enabledLineCount(mCfg);
    int h     = lines * (size + 3);
    int maxY  = kSafeBottom - h;
    if (maxY < 0) maxY = 0;
    mCfg.x = (u16)clampi(mCfg.x, 0, 620);
    mCfg.y = (u16)clampi(mCfg.y, 0, maxY);
}

const char *MetadataDisplay::menuRowName(int row) {
    static const char *const names[] = {
        "Metadata display", "Labels", "X Position", "Y Position", "Z Position",
        "Mario Angle", "Horizontal Speed", "Vertical Speed", "QF Offset",
        "Camera Angle", "Invincibility Frames", "Pollution Degree", "Spin Condition",
        "Edit layout", "Reset layout"
    };
    return (row >= 0 && row < menuRowCount()) ? names[row] : "";
}

const char *MetadataDisplay::menuRowValue(int row) const {
    if (row == 0) return kOnOff[mCfg.startVisible ? 1 : 0];
    if (row == 1) return kLabelModes[mCfg.labelMode];
    if (row >= 2 && row < 2 + FIELD_COUNT)
        return kOnOff[(mCfg.fieldMask & kFieldBits[row - 2]) ? 1 : 0];
    if (row == 2 + FIELD_COUNT) return "Open";
    if (row == 3 + FIELD_COUNT) return "Default";
    return "";
}

void MetadataDisplay::adjustMenuRow(int row, int dir) {
    if (dir == 0) dir = 1;
    if (row == 0) {
        mCfg.startVisible = !mCfg.startVisible;
        markDirty();
    } else if (row == 1) {
        mCfg.labelMode = (u8)((mCfg.labelMode + (dir > 0 ? 1 : 2)) % 3);
        markDirty();
    } else if (row >= 2 && row < 2 + FIELD_COUNT) {
        mCfg.fieldMask ^= kFieldBits[row - 2];
        markDirty();
    } else if (row == 2 + FIELD_COUNT) {
        beginEditor();
    } else if (row == 3 + FIELD_COUNT) {
        resetLayout();
    }
}

void MetadataDisplay::beginEditor() {
    if (mEditing) return;
    mEditBackup      = mCfg;
    mDirtyBeforeEdit = mDirty;
    mEditing         = true;
}

void MetadataDisplay::finishEditor(bool keep) {
    if (!keep) {
        mCfg   = mEditBackup;
        mDirty = mDirtyBeforeEdit;
    }
    mEditing = false;
}

void MetadataDisplay::updateEditor(TMarioGamePad *pad) {
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
    if (rapid & TMarioGamePad::DPAD_LEFT) {
        mCfg.x = (u16)clampi((int)mCfg.x - 2, 0, 640);
        changed = true;
    }
    if (rapid & TMarioGamePad::DPAD_RIGHT) {
        mCfg.x = (u16)clampi((int)mCfg.x + 2, 0, 640);
        changed = true;
    }
    if (rapid & TMarioGamePad::DPAD_UP) {
        mCfg.y = (u16)clampi((int)mCfg.y - 2, 0, 480);
        changed = true;
    }
    if (rapid & TMarioGamePad::DPAD_DOWN) {
        mCfg.y = (u16)clampi((int)mCfg.y + 2, 0, 480);
        changed = true;
    }
    if (rapid & TMarioGamePad::L) {
        mCfg.scale = (u8)clampi((int)mCfg.scale - 2, 50, 150);
        changed = true;
    }
    if (rapid & TMarioGamePad::R) {
        mCfg.scale = (u8)clampi((int)mCfg.scale + 2, 50, 150);
        changed = true;
    }
    if (rapid & TMarioGamePad::X) {
        mCfg.backgroundAlpha = (u8)clampi((int)mCfg.backgroundAlpha - 8, 0, 255);
        changed = true;
    }
    if (rapid & TMarioGamePad::Y) {
        mCfg.backgroundAlpha = (u8)clampi((int)mCfg.backgroundAlpha + 8, 0, 255);
        changed = true;
    }
    if (changed) markDirty();
}

void MetadataDisplay::draw(Menu *menu, bool force) const {
    if (!menu || (!mCfg.startVisible && !force) || !gpMarioOriginal || !gpMarDirector)
        return;

    // Only sample stage-owned objects during ordinary gameplay with no wipe
    // in progress. A departure can begin its fade before the director reaches
    // its final stage-exit state.
    if (gpMarDirector->mCurState != TMarDirector::STATE_NORMAL ||
        !gpApplication.mFader ||
        gpApplication.mFader->mFadeStatus != TSMSFader::FADE_OFF) {
        return;
    }

    Values v = readValues();
    int size  = textSize(mCfg);
    int lineH = size + 3;

    if (mCfg.labelMode == SUSAMUNE_METADATA_LABEL_CUSTOM) {
        drawCustom(menu, mCfg, v, size, lineH);
        return;
    }

    const char *const *labels = mCfg.labelMode == SUSAMUNE_METADATA_LABEL_LONG
                                    ? kLongLabels : kShortLabels;
    int maxWidth = 0;
    int lines = 0;
    char text[96];
    for (int field = 0; field < FIELD_COUNT; field++) {
        if (!(mCfg.fieldMask & kFieldBits[field])) continue;
        formatField(text, sizeof(text), field, labels[field], v);
        int width = Menu::textWidth(text, size);
        if (width > maxWidth) maxWidth = width;
        lines++;
    }
    drawBackground(menu, mCfg, maxWidth, lines * lineH);

    int y = mCfg.y;
    for (int field = 0; field < FIELD_COUNT; field++) {
        if (!(mCfg.fieldMask & kFieldBits[field])) continue;
        formatField(text, sizeof(text), field, labels[field], v);
        drawLine(menu, text, mCfg.x, y, size);
        y += lineH;
    }
}

void MetadataDisplay::drawEditor(Menu *menu) const {
    draw(menu, true);

    char status[96];
    menu->fillBox(8, 8, 624, 70, Color(0, 0, 0, 205));
    snprintf(status, sizeof(status), "X:%u Y:%u Size:%u%% BG:%u%% Labels:%s",
             mCfg.x, mCfg.y, mCfg.scale,
             (unsigned)mCfg.backgroundAlpha * 100u / 255u,
             kLabelModes[mCfg.labelMode]);
    menu->drawText("Metadata Display editor", 18, 15, 16, 16,
                   Color(255, 255, 255, 255));
    menu->drawText(status, 18, 38, 11, 11, Color(190, 220, 255, 255));
    menu->drawText("D-pad Move  L/R Size  X/Y BG  A Save  B Cancel  Z Reset",
                   18, 57, 11, 11, Color(255, 255, 255, 255));
}
