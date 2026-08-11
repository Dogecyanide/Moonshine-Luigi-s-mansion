#ifndef _SUSAMUNE_QFT_DISPLAY_HXX
#define _SUSAMUNE_QFT_DISPLAY_HXX

#include "susamune/creation.hxx"
#include "susamune/susamune_cfg.h"

class Menu;
class TMarioGamePad;

class QftDisplay {
public:
    void resetDefaults();
    void adopt(const volatile SusamuneQftDisplayCfg *src);
    void stageInto(volatile SusamuneQftDisplayCfg *dst) const;

    void draw(Menu *menu, const char *text) const;

    void beginEditor();
    void updateEditor(TMarioGamePad *pad);
    void drawEditor(Menu *menu) const;
    bool editing() const { return mEditor.editing(); }

    bool dirty() const { return mDirty; }
    void clearDirty() { mDirty = false; }

private:
    static CreationStyle defaults();
    void clamp();

    CreationStyle  mStyle;
    CreationEditor mEditor;
    bool           mDirty;
    bool           mDirtyBeforeEdit;
};

extern QftDisplay gQftDisplay;

#endif  // _SUSAMUNE_QFT_DISPLAY_HXX
