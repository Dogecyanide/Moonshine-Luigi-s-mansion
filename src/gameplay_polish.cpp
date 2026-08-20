#include "susamune/gameplay_polish.hxx"

#include "JSystem/J2D/J2DSetScreen.hxx"
#include "JSystem/J2D/J2DScreen.hxx"
#include "JSystem/JUtility/JUTGamePad.hxx"
#include "SMS/Player/Yoshi.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/binds.hxx"
#include "susamune/menu.hxx"
#include "susamune/records.hxx"
#include "susamune/settings.hxx"
#include "susamune/split_events.hxx"

namespace {

bool pauseOpen() {
    return gpMarDirector && gpMarDirector->mPauseMenu &&
        gpMarDirector->mCurState == TMarDirector::STATE_PAUSE_MENU &&
        gpMarDirector->mPauseMenu->mState == TPauseMenu2::MENU_OPEN;
}

J2DScreen *sHiddenHud;
J2DSetScreen *sHiddenPause;
bool sHudWasVisible;
bool sPauseWasVisible;
bool sCleanPause;
bool sZHeld;

void restoreScreens() {
    if (sHiddenHud && gpMarDirector && gpMarDirector->mGCConsole &&
        gpMarDirector->mGCConsole->mMainScreen == sHiddenHud) {
        sHiddenHud->mIsVisible = sHudWasVisible;
    }
    if (sHiddenPause && gpMarDirector && gpMarDirector->mPauseMenu &&
        gpMarDirector->mPauseMenu->mScreen == sHiddenPause) {
        sHiddenPause->mIsVisible = sPauseWasVisible;
    }
    sHiddenHud = nullptr;
    sHiddenPause = nullptr;
}

void hideScreens() {
    if (!gpMarDirector || !gpMarDirector->mGCConsole ||
        !gpMarDirector->mPauseMenu) return;

    J2DScreen *hud = gpMarDirector->mGCConsole->mMainScreen;
    J2DSetScreen *pause = gpMarDirector->mPauseMenu->mScreen;
    if (hud) {
        if (sHiddenHud != hud) {
            sHiddenHud = hud;
            sHudWasVisible = hud->mIsVisible;
        }
        hud->mIsVisible = false;
    }
    if (pause) {
        if (sHiddenPause != pause) {
            sHiddenPause = pause;
            sPauseWasVisible = pause->mIsVisible;
        }
        pause->mIsVisible = false;
    }
}

void requestSavePrompt(TMarDirector *director) {
    if (!director || (director->mGameState & 0x200)) return;
    director->mGameState |= 0x200;
    director->mSavePromptType = 0;
}

}  // namespace

extern "C" void susamuneFireRideYoshi(TMarDirector *director, TYoshi *yoshi) {
    director->fireRideYoshi(yoshi);
    Records::onYoshiMounted();
    SplitEvents::onYoshiMounted();
    if (gSettings.getBool(SETTING_YOSHI_NOZZLE_SAVE_PROMPT))
        requestSavePrompt(director);
}

extern "C" void susamuneFireGetNozzle(TMarDirector *director,
                                      TItemNozzle *nozzle) {
    director->fireGetNozzle(nozzle);
    SplitEvents::onNozzleCollected();
    if (gSettings.getBool(SETTING_YOSHI_NOZZLE_SAVE_PROMPT))
        requestSavePrompt(director);
}

namespace GameplayPolish {

void beforeDirect() {
    const bool zHeld =
        (JUTGamePad::mPadStatus[0].mButton & JUTGamePad::Z) != 0;
    if (pauseOpen() && zHeld && !sZHeld) {
        sCleanPause = !sCleanPause;
    }
    sZHeld = zHeld;
    if (!pauseOpen()) {
        sCleanPause = false;
        restoreScreens();
    } else if (sCleanPause) {
        // The pause screen draws inside direct(), so it must already be hidden.
        hideScreens();
    } else {
        restoreScreens();
    }
}

void afterDirect() {
    if (!pauseOpen()) {
        sCleanPause = false;
        restoreScreens();
    } else if (sCleanPause) {
        hideScreens();
    }
}

void draw(Menu *menu) {
    if (!menu || !pauseOpen()) return;
    const char *text = sCleanPause ? "Z: Show everything"
                                   : "Z: Hide everything";
    const int size = 11;
    const int pad = 6;
    const int width = Menu::textWidth(text, size) + pad * 2;
    const int x = 632 - width;
    const int y = 18;
    menu->fillBox(x, y, width, size + pad * 2,
                  JUtility::TColor(0, 0, 0, 165));
    menu->drawText(text, x + pad, y + pad, size, size,
                   JUtility::TColor(245, 248, 255, 255));
}

}  // namespace GameplayPolish
