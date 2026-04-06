#ifndef _SUSAMUNE_SETTINGS_MENU_HXX
#define _SUSAMUNE_SETTINGS_MENU_HXX

#include "J2D/J2DOrthoGraph.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "J2D/J2DTextBox.hxx"


#define NUM_EPISODES 8
#define NUM_STAGES 7

class SettingsMenu {
private:
    static const int frameOutset = 40;
    static const int frameInset = 30;
    static const int textSizeX = 20;
    static const int textSizeY = 20;

    s32 mSelectedArea;
    s32 mSelectedEpisode;
    //enum {
    //    HIDE,
    //    SHOWN,
    //    EXIT
    //} mState;  // maybe later
    bool mShown;

    J2DTextBox* mEpisodeTexts[NUM_STAGES][NUM_EPISODES];
    J2DTextBox* mStageTexts[NUM_STAGES];
public:
    bool mChangeStageReady;
    SettingsMenu();

    void processInput(TMarioGamePad* controller);
    void draw(J2DOrthoGraph* ortho);
    void changeStageHook();
};

#endif // _SUSAMUNE_SETTINGS_MENU_HXX