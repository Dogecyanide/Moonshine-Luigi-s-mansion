#ifndef LM_DIAG_STATE_HXX
#define LM_DIAG_STATE_HXX

#include "Dolphin/types.h"

namespace LMState {

enum class Status : u32 {
    Empty = 0,
    Saved,
    Loaded,
    Busy,
    BadCrc,
    BadHeap,
    Epoch,
    TooLarge,
};

// Polls port 1, updates the stability gate, and services one edge-triggered
// snapshot request. Call only after LM's complete retail presenter returns.
void tick();

// These no-op outside the first frame after a successful load. Together with
// the ARM phase journal they distinguish a restore hang in the main-loop game
// step from the first complete retail presenter after it.
void postLoadMilestone(u32 phase);
void presenterEnter();
void presenterAfterSample();
void presenterAfterDrawDone();
void presenterAfterRetail();
void presenterBeforeTick();
void presenterAfterTick();

Status status();
const char *statusText();
u32 snapshotKiB();
u32 stableFrames();
const char *gateText();
u32 gateValue();

}  // namespace LMState

#endif  // LM_DIAG_STATE_HXX
