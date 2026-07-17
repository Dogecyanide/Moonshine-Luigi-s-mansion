#ifndef _SUSAMUNE_SAVESTATE_HXX
#define _SUSAMUNE_SAVESTATE_HXX

#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DTextBox.hxx"
#include "SMS/Player/MarioGamePad.hxx"


class SavestateManager {
public:
    SavestateManager();

    // Called once per frame from main.cpp's onUpdate hook. Polls the d-pad
    // and triggers saves. Loads are queued until the post-render hook so the
    // current frame cannot consume a mixture of live and restored state.
    void updateHook(TMarioGamePad *controller);

    // Called after the game's THPPlayerDrawDone()/GXDrawDone barrier. A queued
    // load is restored here, after director, fader, audio, and rendering work
    // for the current frame has finished.
    void processPendingLoad();

    // Drawn after the scene each frame, shows "saved" / "loaded" / error.
    void draw(J2DOrthoGraph *ortho);

    // Public so callers can trigger from elsewhere (e.g. a debug menu).
    bool saveState();
    bool loadState();

private:
    void setStatus(const char *msg);

    J2DTextBox *mInfoText;
    bool mLoadPending;
};

#endif // _SUSAMUNE_SAVESTATE_HXX
