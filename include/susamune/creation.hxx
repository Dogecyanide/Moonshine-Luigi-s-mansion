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
    u8  textR;
    u8  textG;
    u8  textB;
    u8  textA;
    u8  bgR;
    u8  bgG;
    u8  bgB;
    u8  bgA;
    u8  brightness;
    u8  padding;
};
static_assert(sizeof(CreationStyle) == 16, "creation style layout changed");

class CreationEditor {
public:
    enum UpdateResult {
        UPDATE_NONE      = 0,
        UPDATE_CHANGED   = 1,
        UPDATE_FINISHED  = 2,
        UPDATE_CANCELLED = 4,
    };

    void reset();
    void begin(CreationStyle *style);
    u8   update(TMarioGamePad *pad, const CreationStyle &defaults);
    void draw(Menu *menu, const char *title) const;
    bool editing() const { return mEditing; }

private:
    CreationStyle *mStyle;
    CreationStyle  mBackup;
    u8             mField;
    bool           mEditing;
};

namespace Creation {

void drawTextBox(Menu *menu, const CreationStyle &style, const char *text);

}  // namespace Creation

#endif  // _SUSAMUNE_CREATION_HXX
