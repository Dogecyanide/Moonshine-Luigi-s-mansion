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
// snapshot request. Call only after the retail frame has reached GXDrawDone.
void tick();

Status status();
const char *statusText();
u32 snapshotKiB();
u32 stableFrames();
const char *gateText();
u32 gateValue();

}  // namespace LMState

#endif  // LM_DIAG_STATE_HXX
