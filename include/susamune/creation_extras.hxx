#ifndef _SUSAMUNE_CREATION_EXTRAS_HXX
#define _SUSAMUNE_CREATION_EXTRAS_HXX

#include "susamune/creation.hxx"
#include "susamune/susamune_cfg.h"

class J2DPicture;
class J2DPane;
class J2DScreen;
class Menu;
class TMarioGamePad;

class CreationExtras {
public:
    enum {
        MENU_ROW_COUNT = 20,
        HUD_PANE_COUNT = 25,
        PREVIEW_PANE_COUNT = 2,
    };

    void resetDefaults();
    void adopt(const volatile SusamuneCreationCfg *src);
    void stageInto(volatile SusamuneCreationCfg *dst) const;

    void onStageSetup();
    void update();
    void draw(Menu *menu) const;
    void beginTimerCharacterEditor();
    void beginColorEditor(int first, int count, const char *title,
                          const char *names = nullptr);
    void toggleTimerLabel();
    bool timerLabelVisible() const { return mTimerLabelVisible != 0; }

    static int menuRowCount() { return MENU_ROW_COUNT; }
    static bool menuRowSeparator(int row);
    static const char *menuRowName(int row);
    const char *menuRowValue(int row) const;
    void adjustMenuRow(int row, int direction);

    void updateEditor(TMarioGamePad *pad);
    void drawEditor(Menu *menu) const;
    bool editing() const { return mEditor.editing() || mKeyboard; }

    bool dirty() const { return mDirty; }
    void clearDirty() { mDirty = false; }
    const u8 *menuBackground() const {
        return mColors[SUSAMUNE_CREATION_MENU_BG];
    }
private:
    enum EditMode { EDIT_NONE, EDIT_COLOR, EDIT_TIMER, EDIT_WORD_STYLE };

    static CreationStyle defaultWordStyle(int index);
    void beginWordEditor(int index);
    void beginKeyboard(int index);
    void updateKeyboard(TMarioGamePad *pad);
    void drawKeyboard(Menu *menu) const;
    void applyHud();
    void beginHudPreview(int color);
    void endHudPreview();
    void addPreviewPane(J2DPane *pane);
    void clampWord(int index);

    CreationStyle mWordStyle[SUSAMUNE_CREATION_WORD_COUNT];
    CreationStyle mColorStyle;
    u8 mColors[SUSAMUNE_CREATION_COLOR_COUNT][3];
    u8 mDefaultColors[SUSAMUNE_CREATION_COLOR_COUNT][3];
    u8 mColorBackup[SUSAMUNE_CREATION_COLOR_COUNT][3];
    u8 mWordRgb[SUSAMUNE_CREATION_WORD_COUNT]
               [SUSAMUNE_CREATION_WORD_CHARS][3];
    u8 mWordBackup[SUSAMUNE_CREATION_WORD_CHARS][3];
    char mWords[SUSAMUNE_CREATION_WORD_COUNT]
               [SUSAMUNE_CREATION_WORD_TEXT_SIZE];
    char mTextBackup[SUSAMUNE_CREATION_WORD_TEXT_SIZE];
    J2DPicture *mHudPictures[HUD_PANE_COUNT];
    J2DScreen *mHudScreen;
    J2DPane *mPreviewPanes[PREVIEW_PANE_COUNT];
    CreationEditor mEditor;
    const char *mEditTitle;
    u8 mWordLength[SUSAMUNE_CREATION_WORD_COUNT];
    u8 mWordVisible[SUSAMUNE_CREATION_WORD_COUNT];
    u8 mEditMode;
    u8 mEditFirst;
    u8 mEditCount;
    u8 mEditWord;
    u8 mPreviewPaneCount;
    u8 mKeyboardCursor;
    u8 mKeyboardPage;
    u8 mKeyboardConfirm;
    u32 mColorPresent;
    u32 mColorPresentBeforeEdit;
    u32 mPreviewVisible;
    u8 mWaterFillDefault[2][3];
    u8 mTimerLabelVisible;
    bool mKeyboard;
    bool mUppercase;
    bool mDirty;
    bool mDirtyBeforeEdit;
};

extern CreationExtras gCreationExtras;

#endif  // _SUSAMUNE_CREATION_EXTRAS_HXX
