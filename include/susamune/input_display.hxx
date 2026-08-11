#ifndef _SUSAMUNE_INPUT_DISPLAY_HXX
#define _SUSAMUNE_INPUT_DISPLAY_HXX

#include <Dolphin/types.h>

#include "susamune/susamune_cfg.h"

class Menu;
class TMarioGamePad;

// MEM1 keeps only fields the renderer edits. stageInto() reconstructs the
// versioned 32-byte wire payload used by the launcher.
struct InputDisplayLiveCfg {
    u16 x;
    u16 y;
    u8  startVisible;
    u8  scale;
    u8  bgR;
    u8  bgG;
    u8  bgB;
    u8  bgA;
    u8  brightness;
    u8  valueMode;
    u8  valueSource;
    u8  valuePlacement;
};
static_assert(sizeof(InputDisplayLiveCfg) == 14,
              "input live config layout changed");

// Live controller overlay and its dedicated editor. The visual configuration
// is wider than Settings' byte-sized values, so it persists through the
// versioned SusamuneInputDisplayCfg payload in susamune_cfg.h.
class InputDisplay {
public:
    void resetDefaults();
    void adopt(const volatile SusamuneInputDisplayCfg *src);
    void stageInto(volatile SusamuneInputDisplayCfg *dst) const;

    void update();
    void draw(Menu *menu, bool force = false) const;

    bool dirty() const { return mDirty; }
    void clearDirty() { mDirty = false; }

    // Dedicated Input menu tab.
    static int         menuRowCount() { return 6; }
    static const char *menuRowName(int row);
    const char        *menuRowValue(int row) const;
    void               adjustMenuRow(int row, int dir);

    // Full-screen live layout editor, entered from the Input tab.
    void beginEditor();
    void updateEditor(TMarioGamePad *pad);
    void drawEditor(Menu *menu) const;
    bool editing() const { return mEditing; }

private:
    struct EditBackup {
        u16 x;
        u16 y;
        u8  scale;
        u8  bgR;
        u8  bgG;
        u8  bgB;
        u8  bgA;
        u8  brightness;
    };

    void resetLayout();
    void clampLayout();
    void markDirty();
    void finishEditor(bool keep);

    InputDisplayLiveCfg mCfg;
    EditBackup          mEditBackup;
    bool mVisible;
    bool mVisibleBeforeEdit;
    bool mDirty;
    bool mDirtyBeforeEdit;
    bool mEditing;
    u8   mEditChannel;
};
static_assert(sizeof(InputDisplay) == 30, "input display state layout changed");

extern InputDisplay gInputDisplay;

#endif  // _SUSAMUNE_INPUT_DISPLAY_HXX
