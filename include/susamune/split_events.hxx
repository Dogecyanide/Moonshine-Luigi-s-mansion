#ifndef _SUSAMUNE_SPLIT_EVENTS_HXX
#define _SUSAMUNE_SPLIT_EVENTS_HXX

#include <Dolphin/types.h>

class TMarDirector;

namespace SplitEvents {

// Install the displaced-prologue trampolines before gameplay begins.
void init();

// Stage-heap actor identities are valid only inside one setup generation.
void beforeStageSetup();
void onStageSetup(TMarDirector *director);

// Call immediately around director->direct(): beginFrame opens the retail
// event window; update closes it and converts local event times to absolute QFT.
void beginFrame();
void update();

// Preserve Pinna 1 across its two intentional retail Exit Area movie skips.
void armPinnaOneRetailExit();

// Exact director events shared with the existing gameplay-polish wrappers.
void onYoshiMounted();
void onNozzleCollected();

// A restored actor graph must never inherit pre-load split identity.
void onSavestateLoaded();

}  // namespace SplitEvents

extern "C" void susamuneSplitCoinRedTaken(void *coin, void *collector);
extern "C" void *susamuneSplitPiantaRecoverNerve();
extern "C" void susamuneSplitEmitHappyEffect(void *npc);
extern "C" int susamuneSplitChangePlayerStatus(void *mario, u32 status,
                                                u32 arg, bool force);

#endif  // _SUSAMUNE_SPLIT_EVENTS_HXX
