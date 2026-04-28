#ifndef _SUSAMUNE_SETTINGS_MENU_HXX
#define _SUSAMUNE_SETTINGS_MENU_HXX

#include "J2D/J2DOrthoGraph.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "J2D/J2DTextBox.hxx"

struct WarpDescriptor {
    const char* name;
    u16 area;
    u16 episode;
    s32 overrideArea;
    s32 extraFlag;
};

#define NUM_EPISODES 8
#define NUM_STAGES 10
#define NUM_PRESETS 13

class SettingsMenu {
private:
    static const int frameOutset = 40;
    static const int frameInset = 30;
    static const int textSizeX = 20;
    static const int textSizeY = 20;
    static const int tabWidth = 100;
    static const int tabGap = textSizeY + 10;

    s32 mSelectedArea;
    s32 mSelectedEpisode;
    enum {
        HIDE,
        SHOWN,
        EXIT
    } mState;  // maybe later
    bool mShown;
    enum SettingsTab {
        STAGES,
        PRESETS,
        NUM_TABS
    } mTab;

    J2DTextBox *mInfoText;    
    J2DTextBox* mTabTexts[NUM_TABS];

    J2DTextBox* mEpisodeTexts[NUM_STAGES];
    J2DTextBox* mStageTexts[NUM_STAGES];

    J2DTextBox* mPresetWarps[NUM_PRESETS];
    s32 mSelectedPreset;
public:
    bool mChangeStageReady;
    SettingsMenu();

    void processInput(TMarioGamePad* controller);
    void draw(J2DOrthoGraph* ortho);
    void changeStageHook();
    void setInfo(u32 x);
};

#endif // _SUSAMUNE_SETTINGS_MENU_HXX