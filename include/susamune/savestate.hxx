#ifndef _SUSAMUNE_SAVESTATE_HXX
#define _SUSAMUNE_SAVESTATE_HXX

#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DTextBox.hxx"
#include "SMS/Player/MarioGamePad.hxx"


class SavestateManager {
public:
    SavestateManager();

    // Called once per frame from main.cpp's onUpdate hook. Polls the d-pad
    // and triggers save/load.
    void updateHook(TMarioGamePad *controller);

    // Drawn after the scene each frame, shows "saved" / "loaded" / error.
    void draw(J2DOrthoGraph *ortho);

    // Public so callers can trigger from elsewhere (e.g. a debug menu).
    bool saveState();
    bool loadState();

private:
    void setStatus(const char *msg);

    J2DTextBox *mInfoText;
};

#endif // _SUSAMUNE_SAVESTATE_HXX
