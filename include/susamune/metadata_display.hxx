#ifndef _SUSAMUNE_METADATA_DISPLAY_HXX
#define _SUSAMUNE_METADATA_DISPLAY_HXX

#include <Dolphin/types.h>

#include "susamune/susamune_cfg.h"

class Menu;
class TMarioGamePad;

class MetadataDisplay {
public:
    enum Field {
        FIELD_X,
        FIELD_Y,
        FIELD_Z,
        FIELD_ANGLE,
        FIELD_HSPD,
        FIELD_VSPD,
        FIELD_QF,
        FIELD_CANGLE,
        FIELD_INVINC,
        FIELD_GOOP,
        FIELD_SPIN,
        FIELD_COUNT
    };

    void resetDefaults();
    void adopt(const volatile SusamuneMetadataDisplayCfg *src);
    void stageInto(volatile SusamuneMetadataDisplayCfg *dst) const;

    void draw(Menu *menu, bool force = false) const;

    bool dirty() const { return mDirty; }
    void clearDirty() { mDirty = false; }

    static int         menuRowCount() { return FIELD_COUNT + 4; }
    static const char *menuRowName(int row);
    const char        *menuRowValue(int row) const;
    void               adjustMenuRow(int row, int dir);

    void beginEditor();
    void updateEditor(TMarioGamePad *pad);
    void drawEditor(Menu *menu) const;
    bool editing() const { return mEditing; }

private:
    void resetLayout();
    void clampLayout();
    void markDirty();
    void finishEditor(bool keep);

    SusamuneMetadataDisplayCfg mCfg;
    SusamuneMetadataDisplayCfg mEditBackup;
    bool                       mDirty;
    bool                       mDirtyBeforeEdit;
    bool                       mEditing;
};

extern MetadataDisplay gMetadataDisplay;

#endif  // _SUSAMUNE_METADATA_DISPLAY_HXX
