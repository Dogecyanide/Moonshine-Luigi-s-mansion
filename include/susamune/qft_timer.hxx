#ifndef _SUSAMUNE_QFT_TIMER_HXX
#define _SUSAMUNE_QFT_TIMER_HXX

#include <Dolphin/types.h>

class Menu;
class TMarDirector;

// Native Quarterframe Timer backend plus its two presentations: Sunshine's
// HUD timer (rounded to centiseconds) and the compact three-decimal readout.
// Timing state lives in fixed mod scratch because the small asm event hooks
// must be able to reach it without a linker relocation.
class QFTTimer {
  public:
  // Install the regional timing/freeze hooks. Called once after settings are
  // loaded and before the application state machine starts.
  void init();

  // The temporary-freeze countdown advances at the beginning of a frame, so
  // a freeze raised during director->direct() still renders for its complete
  // configured duration on that same frame.
  void beginFrame();

  // Stage lifecycle and post-direct display update.
  void onStageSetup(TMarDirector *director);
  void update();

  // Compact QFT overlay. Menu::draw supplies the already configured 2D
  // renderer; this draws only while the menu itself is closed.
  void draw(Menu *menu) const;

  // Susamune warps are new attempts, not an in-level transition.
  void requestReset();

  // Keep the native timer in the same one-slot savestate as the director.
  void onSavestateSaved();
  void onSavestateLoaded();
};

extern QFTTimer gQFTTimer;

#endif  // _SUSAMUNE_QFT_TIMER_HXX
