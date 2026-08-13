#ifndef _SUSAMUNE_CREATION_HXX
#define _SUSAMUNE_CREATION_HXX

#include <Dolphin/types.h>

class Menu;
class TMarioGamePad;

// Common live presentation state used by Creation-capable overlays. It stays
// independent of any launcher's wire struct so future targets can reuse the
// editor without inheriting QFT persistence details.
struct CreationStyle {
    u16 x;
    u16 y;
    u8  scale;
    u8  textA;
    u8  bgR;
    u8  bgG;
    u8  bgB;
    u8  bgA;
    u8  textBrightness;
    u8  padding;
};
static_assert(sizeof(CreationStyle) == 12, "creation style layout changed");

class CreationEditor {
public:
    enum Capability {
        CAP_POSITION   = 1 << 0,
        CAP_SCALE      = 1 << 1,
        CAP_TEXT_ALPHA = 1 << 2,
        CAP_BRIGHTNESS = 1 << 3,
        CAP_BACKGROUND = 1 << 4,
        CAP_PADDING    = 1 << 5,
        CAP_TEXT_COLOR = 1 << 6,
        CAP_ALL        = 0x7f,
    };

    enum UpdateResult {
        UPDATE_NONE      = 0,
        UPDATE_CHANGED   = 1,
        UPDATE_FINISHED  = 2,
        UPDATE_CANCELLED = 4,
    };

    void reset();
    void begin(CreationStyle *style, u8 (*textRgb)[3], u8 (*backupRgb)[3],
               u16 textSlots, u16 targetSlots = 0,
               const char *targetNames = nullptr,
               u8 capabilities = CAP_ALL);
    u8   update(TMarioGamePad *pad, const CreationStyle &defaults,
                const u8 (*defaultRgb)[3], u16 defaultRgbSlots = 1);
    void draw(Menu *menu, const char *title, const char *preview) const;
    bool editing() const { return mEditing; }
    u16  target() const { return mTextTarget; }

private:
    u32 repeatInput(TMarioGamePad *pad);
    bool optionEnabled(u8 option) const;
    void moveOption(int direction);

    CreationStyle *mStyle;
    CreationStyle  mBackup;
    u8            (*mTextRgb)[3];
    u8            (*mBackupRgb)[3];
    const char     *mTargetNames;
    u32            mRepeatMask;
    u16            mTextSlots;
    u16            mTargetSlots;
    u16            mTextTarget;
    u8             mOption;
    u8             mCapabilities;
    u8             mRepeatFrames;
    u8             mConfirm;
    bool           mEditing;
};

namespace Creation {

void fillWhite(u8 (*out)[3], u16 slots);
int glyphCount(const char *text);
void drawTextBox(Menu *menu, const CreationStyle &style,
                 const u8 (*textRgb)[3], u16 textSlots, const char *text,
                 bool rightAlignSlots = false, u16 selectedSlot = 0xffff);
void drawTextLine(Menu *menu, const CreationStyle &style,
                  const u8 (*textRgb)[3], u16 textSlots, const char *text,
                  int x, int y, int size, u16 firstSlot, bool shadow,
                  u16 selectedSlot = 0xffff);

}  // namespace Creation

#endif  // _SUSAMUNE_CREATION_HXX
