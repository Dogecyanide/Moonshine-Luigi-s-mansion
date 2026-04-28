#ifndef _SUSAMUNE_SAVESTATE_HXX
#define _SUSAMUNE_SAVESTATE_HXX

#include "J2D/J2DOrthoGraph.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "J2D/J2DTextBox.hxx"


class SavestateManager {
    J2DTextBox *mChksumText;    
public:
    SavestateManager();

    void updateHook(TMarioGamePad* controller);
    void draw(J2DOrthoGraph* ortho);
};

#endif // _SUSAMUNE_SAVESTATE_HXX