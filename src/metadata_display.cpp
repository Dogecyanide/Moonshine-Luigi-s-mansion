// Native metadata overlay based on sup39's Customized Display data set.
// Arbitrary labels remain available through one optional ini template.

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

constexpr u8 kTextOffsets[] = {
    // Short field labels.
    205, 207, 209, 211, 213, 215, 217, 220, 223, 226, 228,
    // Menu rows. Rows 2..12 are also the long field labels.
    0, 17, 24, 35, 46, 57, 69, 86, 101, 111, 124, 145, 162, 177, 192,
};
constexpr char kMetadataText[] =
    "Metadata display\0Labels\0X Position\0Y Position\0Z Position\0"
    "Mario Angle\0Horizontal Speed\0Vertical Speed\0QF Offset\0Camera Angle\0"
    "Invincibility Frames\0Pollution Degree\0Spin Condition\0Metadata style\0"
    "Reset layout\0"
    "X\0Y\0Z\0A\0H\0V\0QF\0CA\0IF\0G\0SP\0"
    "Off\0On\0Short\0Long\0Custom";

const int kTextOnOff      = 231;
const int kTextLabelModes = 238;

constexpr char kTokenIds[] =
    "x\0y\0z\0angle\0HSpd\0VSpd\0QF\0CAngle\0invinc\0goop\0spin";
constexpr u8 kMaximumValueSlots[] = {7, 7, 7, 5, 9, 9, 1, 5, 5, 10, 1};

template <unsigned N>
constexpr int stringCount(const char (&pool)[N]) {
    int count = 0;
    for (unsigned i = 0; i < N; i++) {
        if (pool[i] == '\0') count++;
    }
    return count;
}

static_assert(MetadataDisplay::FIELD_COUNT == 11, "metadata field order changed");
static_assert(SUSAMUNE_METADATA_FIELD_X == (1u << MetadataDisplay::FIELD_X) &&
              SUSAMUNE_METADATA_FIELD_SPIN == (1u << MetadataDisplay::FIELD_SPIN) &&
              SUSAMUNE_METADATA_FIELD_ALL == (1u << MetadataDisplay::FIELD_COUNT) - 1,
              "metadata wire bits no longer match field order");
static_assert(SUSAMUNE_METADATA_LABEL_SHORT == 0 &&
              SUSAMUNE_METADATA_LABEL_LONG == 1 &&
              SUSAMUNE_METADATA_LABEL_CUSTOM == 2,
              "metadata label mode order changed");
static_assert(sizeof(kTextOffsets) == MetadataDisplay::FIELD_COUNT + 15,
              "metadata text offsets changed");
static_assert(sizeof(kMetadataText) == 256, "metadata text offsets need updating");
static_assert(stringCount(kTokenIds) == MetadataDisplay::FIELD_COUNT,
              "metadata token ids changed");
static_assert(sizeof(kMaximumValueSlots) == MetadataDisplay::FIELD_COUNT,
              "metadata maximum widths changed");

inline u16 fieldBit(int field) { return (u16)(1u << field); }

inline const char *onOffText(bool on) {
    return kMetadataText + kTextOnOff + (on ? 4 : 0);
}

inline const char *labelModeText(int mode) {
    return kMetadataText + kTextLabelModes + mode * 6 - (mode >> 1);
}

const char kDefaultFormat[] =
    "X <x|.0>\\nY <y|.0>\\nZ <z|.0>\\nA <angle>\\n"
    "H <HSpd|.2>\\nV <VSpd|.2>\\nQF <QF>\\nCA <CAngle>\\n"
    "IF <invinc>\\nG <goop>\\nSP <spin>";
static_assert(sizeof(kDefaultFormat) <= SUSAMUNE_METADATA_FORMAT_SIZE,
              "default metadata format no longer fits the wire config");

inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int textGlyphBytes(const char *text) {
    const u8 c = (u8)text[0];
    return ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) && text[1]
               ? 2 : 1;
}

int textSize(const CreationStyle &style) {
    return clampi(kBaseTextSize * (int)style.scale / 100, 7, 28);
}

int enabledLineCount(const MetadataDisplayLiveCfg &cfg, const char *format,
                     u32 formatLength) {
    if (cfg.labelMode == SUSAMUNE_METADATA_LABEL_CUSTOM) {
        int lines = 1;
        for (u32 i = 0; i < formatLength; i++) {
            if (format[i] == '\\' && i + 1 < formatLength && format[i + 1] == '\\') {
                i++;
            } else if (format[i] == '\n' ||
                       (format[i] == '\\' && i + 1 < formatLength &&
                        format[i + 1] == 'n')) {
                lines++;
                if (format[i] == '\\') i++;
            }
        }
        return lines;
    }

    int lines = 0;
    for (int i = 0; i < MetadataDisplay::FIELD_COUNT; i++) {
        if (cfg.fieldMask & fieldBit(i)) lines++;
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

Values editorValues() {
    Values v = {};
    v.x = v.y = v.z = 9999999.0f;
    v.hSpd = v.vSpd = 999999.0f;
    v.angle = v.cameraAngle = 0xffff;
    v.qf = 3;
    v.invinc = 0x7fff;
    v.goop = 0xffffffff;
    v.spin = true;
    return v;
}

__attribute__((noinline))
void formatValue(char *out, u32 cap, int field, int precision, const Values &v);

void formatField(char *out, u32 cap, int field, const char *label, const Values &v) {
    char value[48];
    int precision = (field == MetadataDisplay::FIELD_HSPD ||
                     field == MetadataDisplay::FIELD_VSPD) ? 2 : 0;
    formatValue(value, sizeof(value), field, precision, v);
    snprintf(out, cap, "%s: %s", label, value);
}

int fieldPrecision(int field) {
    return (field == MetadataDisplay::FIELD_HSPD ||
            field == MetadataDisplay::FIELD_VSPD) ? 2 : 0;
}

const u8 *fieldLabelOffsets(const MetadataDisplayLiveCfg &cfg) {
    return cfg.labelMode == SUSAMUNE_METADATA_LABEL_LONG
               ? kTextOffsets + MetadataDisplay::FIELD_COUNT + 2
               : kTextOffsets;
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
    while (len > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        len--;
    }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
    const char *id = kTokenIds;
    for (int i = 0; i < MetadataDisplay::FIELD_COUNT; i++) {
        if (tokenEquals(s, len, id)) return i;
        while (*id++) {}
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

bool formatCustomLine(const char *format, u32 formatLength, const Values &v,
                      u32 &i, char *line, int cap) {
    int len = 0;
    line[0] = '\0';

    while (i < formatLength) {
        char c = format[i];
        if (c == '\n' ||
            (c == '\\' && i + 1 < formatLength && format[i + 1] == 'n')) {
            i += (c == '\\') ? 2 : 1;
            return true;
        }
        if (c == '\\' && i + 1 < formatLength && format[i + 1] == '\\') {
            appendRange(line, len, cap, "\\", 1);
            i += 2;
            continue;
        }
        if (c == '<') {
            u32 close = i + 1;
            while (close < formatLength && format[close] != '>') {
                close++;
            }
            if (close < formatLength && format[close] == '>') {
                const char *body = &format[i + 1];
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
                    appendRange(line, len, cap, &format[i],
                                (int)(close - i + 1));
                }
                i = close + 1;
                continue;
            }
        }

        appendRange(line, len, cap, &format[i], 1);
        i++;
    }
    return false;
}

void drawBackground(Menu *menu, const CreationStyle &style,
                    int width, int height) {
    if (style.padding == 0xff || style.bgA == 0 || width <= 0 || height <= 0)
        return;
    const int pad = style.padding;
    menu->fillBox(style.x - pad, style.y - pad,
                  width + pad * 2, height + pad * 2,
                  Color(style.bgR, style.bgG, style.bgB, style.bgA));
}

void drawCustom(Menu *menu, const CreationStyle &style, const u8 (*textRgb)[3],
                const char *format, u32 formatLength, const Values &v,
                const Values &maximum, int size, int lineH, u16 selectedSlot) {
    char line[192], maximumLine[192];
    u32 pos = 0;
    int maxWidth = 0;
    int lines = 0;
    bool more;

    do {
        more = formatCustomLine(format, formatLength, v, pos, line, sizeof(line));
        int width = Menu::textWidth(line, size);
        if (width > maxWidth) maxWidth = width;
        lines++;
    } while (more);

    drawBackground(menu, style, maxWidth, lines * lineH);

    pos = 0;
    u32 maximumPos = 0;
    int y = style.y;
    u16 slot = 0;
    do {
        more = formatCustomLine(format, formatLength, v, pos, line, sizeof(line));
        formatCustomLine(format, formatLength, maximum, maximumPos,
                         maximumLine, sizeof(maximumLine));
        Creation::drawTextLine(menu, style, textRgb,
                               SUSAMUNE_METADATA_STYLE_TEXT_SLOTS,
                               line, style.x, y, size, slot, true,
                               selectedSlot);
        slot = (u16)(slot + Creation::glyphCount(maximumLine));
        y += lineH;
    } while (more);
}

void drawStandard(Menu *menu, const MetadataDisplayLiveCfg &cfg,
                  const CreationStyle &style, const u8 (*textRgb)[3],
                  const Values &values, bool editing, int size, int lineH,
                  u16 selectedSlot) {
    const u8 *labelOffsets = fieldLabelOffsets(cfg);
    int maxWidth = 0;
    int lines = 0;
    char prefix[48], value[48];

    for (int field = 0; field < MetadataDisplay::FIELD_COUNT; field++) {
        if (!editing && !(cfg.fieldMask & fieldBit(field))) continue;
        const char *label = kMetadataText + labelOffsets[field];
        snprintf(prefix, sizeof(prefix), "%s: ", label);
        formatValue(value, sizeof(value), field, fieldPrecision(field), values);
        const int width = Menu::textWidth(prefix, size) +
                          Menu::textWidth(value, size);
        if (width > maxWidth) maxWidth = width;
        lines++;
    }
    drawBackground(menu, style, maxWidth, lines * lineH);

    int y = style.y;
    u16 slot = 0;
    for (int field = 0; field < MetadataDisplay::FIELD_COUNT; field++) {
        const char *label = kMetadataText + labelOffsets[field];
        snprintf(prefix, sizeof(prefix), "%s: ", label);
        const u16 prefixSlots = (u16)Creation::glyphCount(prefix);
        const u16 valueSlots = kMaximumValueSlots[field];

        if (editing || (cfg.fieldMask & fieldBit(field))) {
            formatValue(value, sizeof(value), field, fieldPrecision(field), values);
            const u16 valueCount = (u16)Creation::glyphCount(value);
            const u16 valueSlot = valueCount < valueSlots
                                      ? slot + prefixSlots + valueSlots - valueCount
                                      : slot + prefixSlots;
            Creation::drawTextLine(menu, style, textRgb,
                                   SUSAMUNE_METADATA_STYLE_TEXT_SLOTS,
                                   prefix, style.x, y, size, slot, true,
                                   selectedSlot);
            Creation::drawTextLine(
                menu, style, textRgb, SUSAMUNE_METADATA_STYLE_TEXT_SLOTS,
                value, style.x + Menu::textWidth(prefix, size), y, size,
                valueSlot, true, selectedSlot);
            y += lineH;
        }
        slot = (u16)(slot + prefixSlots + valueSlots);
    }
}

}  // namespace

MetadataDisplay gMetadataDisplay;

CreationStyle MetadataDisplay::defaultStyle() {
    return CreationStyle{16, 112, 100, 255, 0, 0, 0, 0, 100, 3};
}

void MetadataDisplay::resetDefaults() {
    mCfg.x            = 16;
    mCfg.y            = 112;
    mCfg.fieldMask    = SUSAMUNE_METADATA_FIELD_ALL;
    mCfg.startVisible = 0;
    mCfg.scale        = 100;
    mCfg.labelMode    = SUSAMUNE_METADATA_LABEL_SHORT;
    mCfg.backgroundAlpha = 0;
    mStyle             = defaultStyle();
    Creation::fillWhite(mTextRgb, SUSAMUNE_METADATA_STYLE_TEXT_SLOTS);
    mFormat            = kDefaultFormat;
    mFormatLength      = sizeof(kDefaultFormat) - 1;

    mEditor.reset();
    mEditorPreview[0] = '\0';
    mEditorPreviewSlots = 0;
    mDirty          = false;
    mDirtyBeforeEdit = false;
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
        u32 length = 0;
        while (length + 1 < SUSAMUNE_METADATA_FORMAT_SIZE && src->format[length]) {
            length++;
        }
        // The launcher publishes this fixed block before releasing the PPC and
        // never rewrites its payload, so the custom template can stay borrowed.
        mFormat       = const_cast<const char *>(&src->format[0]);
        mFormatLength = (u8)length;
    }

    mCfg.fieldMask &= SUSAMUNE_METADATA_FIELD_ALL;
    mCfg.startVisible = mCfg.startVisible ? 1 : 0;
    mCfg.scale         = (u8)clampi(mCfg.scale, 50, 200);
    mCfg.labelMode     = (u8)clampi(mCfg.labelMode, 0, 2);
    mStyle.x           = mCfg.x;
    mStyle.y           = mCfg.y;
    mStyle.scale       = mCfg.scale;
    mStyle.bgA         = mCfg.backgroundAlpha;
    clampLayout();
    mDirty = false;
}

void MetadataDisplay::adoptStyle(const volatile SusamuneMetadataStyleCfg *src) {
    if (!src || src->magic != SUSAMUNE_METADATA_STYLE_MAGIC ||
        src->version == 0 || src->version > SUSAMUNE_METADATA_STYLE_VERSION) {
        return;
    }

    const u16 p = src->present;
    if (p & SUSAMUNE_METADATA_STYLE_TEXT_A) mStyle.textA = src->textA;
    if (p & SUSAMUNE_METADATA_STYLE_BG_R) mStyle.bgR = src->bgR;
    if (p & SUSAMUNE_METADATA_STYLE_BG_G) mStyle.bgG = src->bgG;
    if (p & SUSAMUNE_METADATA_STYLE_BG_B) mStyle.bgB = src->bgB;
    if (p & SUSAMUNE_METADATA_STYLE_BG_A) mStyle.bgA = src->bgA;
    if (p & SUSAMUNE_METADATA_STYLE_BRIGHTNESS)
        mStyle.textBrightness = src->textBrightness;
    if (p & SUSAMUNE_METADATA_STYLE_PADDING) mStyle.padding = src->padding;

    for (int i = 0; i < SUSAMUNE_METADATA_STYLE_TEXT_SLOTS; i++) {
        if (p & SUSAMUNE_METADATA_STYLE_TEXT_R) mTextRgb[i][0] = src->textR;
        if (p & SUSAMUNE_METADATA_STYLE_TEXT_G) mTextRgb[i][1] = src->textG;
        if (p & SUSAMUNE_METADATA_STYLE_TEXT_B) mTextRgb[i][2] = src->textB;
        if (src->version >= 2 &&
            (src->slotPresent[i >> 3] & (1u << (i & 7)))) {
            for (int c = 0; c < 3; c++) mTextRgb[i][c] = src->textRgb[i][c];
        }
    }
    clampLayout();
    mDirty = false;
}

void MetadataDisplay::stageInto(volatile SusamuneMetadataDisplayCfg *dst) const {
    dst->magic        = SUSAMUNE_METADATA_CFG_MAGIC;
    dst->version      = SUSAMUNE_METADATA_CFG_VERSION;
    dst->x            = mStyle.x;
    dst->y            = mStyle.y;
    dst->fieldMask    = mCfg.fieldMask;
    dst->startVisible = mCfg.startVisible;
    dst->scale        = mStyle.scale;
    dst->labelMode    = mCfg.labelMode;
    dst->backgroundAlpha = mStyle.bgA;
    const char *dstFormat = const_cast<const char *>(&dst->format[0]);
    if (mFormat != dstFormat) {
        for (u32 i = 0; i < mFormatLength; i++) {
            dst->format[i] = mFormat[i];
        }
    }
    for (u32 i = mFormatLength; i < SUSAMUNE_METADATA_FORMAT_SIZE; i++) {
        dst->format[i] = '\0';
    }
}

void MetadataDisplay::stageStyleInto(volatile SusamuneMetadataStyleCfg *dst) const {
    // Normal layouts never select the final slot, while All still updates it.
    const int base = SUSAMUNE_METADATA_STYLE_TEXT_SLOTS - 1;
    dst->magic          = SUSAMUNE_METADATA_STYLE_MAGIC;
    dst->version        = SUSAMUNE_METADATA_STYLE_VERSION;
    dst->present        = SUSAMUNE_METADATA_STYLE_ALL;
    dst->textR          = mTextRgb[base][0];
    dst->textG          = mTextRgb[base][1];
    dst->textB          = mTextRgb[base][2];
    dst->textA          = mStyle.textA;
    dst->bgR            = mStyle.bgR;
    dst->bgG            = mStyle.bgG;
    dst->bgB            = mStyle.bgB;
    dst->bgA            = mStyle.bgA;
    dst->textBrightness = mStyle.textBrightness;
    dst->padding        = mStyle.padding;
    for (u32 i = 0; i < sizeof(dst->reserved0); i++) dst->reserved0[i] = 0;
    for (u32 i = 0; i < sizeof(dst->slotPresent); i++) dst->slotPresent[i] = 0;
    for (int i = 0; i < SUSAMUNE_METADATA_STYLE_TEXT_SLOTS; i++) {
        const bool override = mTextRgb[i][0] != mTextRgb[base][0] ||
                              mTextRgb[i][1] != mTextRgb[base][1] ||
                              mTextRgb[i][2] != mTextRgb[base][2];
        if (override) {
            dst->slotPresent[i >> 3] |= (u8)(1u << (i & 7));
            for (int c = 0; c < 3; c++) dst->textRgb[i][c] = mTextRgb[i][c];
        }
    }
}

void MetadataDisplay::markDirty() {
    mDirty = true;
    clampLayout();
}

void MetadataDisplay::resetLayout() {
    const CreationStyle defaults = defaultStyle();
    mStyle.x     = defaults.x;
    mStyle.y     = defaults.y;
    mStyle.scale = defaults.scale;
    mStyle.bgA   = defaults.bgA;
    markDirty();
}

void MetadataDisplay::clampLayout() {
    mStyle.scale          = (u8)clampi(mStyle.scale, 50, 200);
    mStyle.textBrightness = (u8)clampi(mStyle.textBrightness, 25, 200);
    if (mStyle.padding != 0xff)
        mStyle.padding = (u8)clampi(mStyle.padding, 0, 16);
    int size  = textSize(mStyle);
    int lines = enabledLineCount(mCfg, mFormat, mFormatLength);
    int h     = lines * (size + 3);
    int maxY  = kSafeBottom - h;
    if (maxY < 0) maxY = 0;
    mStyle.x = (u16)clampi(mStyle.x, 0, 620);
    mStyle.y = (u16)clampi(mStyle.y, 0, maxY);
    syncLegacyStyle();
}

void MetadataDisplay::syncLegacyStyle() {
    mCfg.x               = mStyle.x;
    mCfg.y               = mStyle.y;
    mCfg.scale           = mStyle.scale;
    mCfg.backgroundAlpha = mStyle.bgA;
}

const char *MetadataDisplay::menuRowName(int row) {
    return (row >= 0 && row < menuRowCount())
               ? kMetadataText + kTextOffsets[FIELD_COUNT + row] : "";
}

const char *MetadataDisplay::menuRowValue(int row) const {
    if (row == 0) return onOffText(mCfg.startVisible != 0);
    if (row == 1) return labelModeText(mCfg.labelMode);
    if (row >= 2 && row < 2 + FIELD_COUNT)
        return onOffText((mCfg.fieldMask & fieldBit(row - 2)) != 0);
    if (row == 2 + FIELD_COUNT) return "Edit";
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
        mCfg.fieldMask ^= fieldBit(row - 2);
        markDirty();
    } else if (row == 2 + FIELD_COUNT) {
        beginEditor();
    } else if (row == 3 + FIELD_COUNT) {
        resetLayout();
    }
}

void MetadataDisplay::buildEditorPreview() {
    const Values v = editorValues();
    int out = 0;
    auto appendLine = [&](const char *line) {
        while (*line && out < SUSAMUNE_METADATA_STYLE_TEXT_SLOTS) {
            const int bytes = textGlyphBytes(line);
            if (out + bytes > SUSAMUNE_METADATA_STYLE_TEXT_SLOTS) break;
            for (int i = 0; i < bytes; i++) mEditorPreview[out++] = line[i];
            line += bytes;
        }
    };

    char line[192];
    if (mCfg.labelMode == SUSAMUNE_METADATA_LABEL_CUSTOM) {
        u32 pos = 0;
        bool more;
        do {
            more = formatCustomLine(mFormat, mFormatLength, v, pos,
                                    line, sizeof(line));
            appendLine(line);
        } while (more && out < SUSAMUNE_METADATA_STYLE_TEXT_SLOTS);
    } else {
        const u8 *labelOffsets = fieldLabelOffsets(mCfg);
        for (int field = 0; field < FIELD_COUNT; field++) {
            formatField(line, sizeof(line), field,
                        kMetadataText + labelOffsets[field], v);
            appendLine(line);
        }
    }

    if (out == 0) {
        const char fallback[] = "Metadata";
        for (u32 i = 0; i < sizeof(fallback); i++) mEditorPreview[i] = fallback[i];
        out = sizeof(fallback) - 1;
    } else {
        mEditorPreview[out] = '\0';
    }
    mEditorPreviewSlots = (u16)Creation::glyphCount(mEditorPreview);
}

void MetadataDisplay::beginEditor() {
    if (editing()) return;
    buildEditorPreview();
    mDirtyBeforeEdit = mDirty;
    mEditor.begin(&mStyle, mTextRgb, mBackupRgb,
                  SUSAMUNE_METADATA_STYLE_TEXT_SLOTS, mEditorPreviewSlots);
}

void MetadataDisplay::updateEditor(TMarioGamePad *pad) {
    const u8 defaultsRgb[3] = {255, 255, 255};
    const u8 result = mEditor.update(pad, defaultStyle(), defaultsRgb);
    if (result & CreationEditor::UPDATE_CHANGED) markDirty();
    if (result & CreationEditor::UPDATE_CANCELLED) {
        syncLegacyStyle();
        mDirty = mDirtyBeforeEdit;
    }
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

    const bool editor = editing();
    const Values maximum = editorValues();
    const Values v = editor ? maximum : readValues();
    int size  = textSize(mStyle);
    int lineH = size + 3;
    const u16 selected = editor && mEditor.target()
                             ? mEditor.target() - 1 : 0xffff;

    if (mCfg.labelMode == SUSAMUNE_METADATA_LABEL_CUSTOM) {
        drawCustom(menu, mStyle, mTextRgb, mFormat, mFormatLength,
                   v, maximum, size, lineH, selected);
        return;
    }
    drawStandard(menu, mCfg, mStyle, mTextRgb, v, editor, size, lineH, selected);
}

void MetadataDisplay::drawEditor(Menu *menu) const {
    draw(menu, true);
    mEditor.draw(menu, "Metadata editor", mEditorPreview);
}
