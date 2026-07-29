#ifndef _SUSAMUNE_WARP_HXX
#define _SUSAMUNE_WARP_HXX

#include <Dolphin/types.h>

// =====================================================================
// warp.hxx
//
// Debug stage-warp declarations. The implementation lives in debug_warp.cpp
// and is only compiled when ENABLE_DEBUG_WARPS is enabled. The menu
// only picks a destination and calls Warp::request(); the actual flag
// manipulation and scene switch live here and run from main.cpp's
// game-mode hook via Warp::execute().
// =====================================================================

// A named preset warp: a destination plus the flag fiddling some presets
// need (e.g. warping to Delfino with a specific pipe active).
struct WarpDescriptor {
    const char *name;
    u16         area;
    u16         episode;
    s32         overrideArea;  // set the *current* scene's area first (-1 = no)
    s32         extraFlag;     // shine id / >0x10000 boolean flag to set (-1 = no)
};

#define WARP_NUM_EPISODES 8
#define WARP_NUM_STAGES   10

namespace Warp {

// The plain area picker (area 0..WARP_NUM_STAGES-1, episode 0..7).
extern const char *const kStageNames[WARP_NUM_STAGES];

// Curated preset warps.
extern const WarpDescriptor kPresets[];
extern const int            kNumPresets;

// Queue a warp. Parameters mirror WarpDescriptor; overrideArea/extraFlag
// may be -1. Executed later at the game-mode hook so the switch happens at
// a safe point in the frame.
void request(s32 area, s32 episode, s32 overrideArea, s32 extraFlag);

// True while a requested warp is waiting to be executed.
bool pending();

// Perform the queued warp: set up flags and gpApplication.mNextScene, then
// clear the pending flag. The caller (main.cpp) is responsible for driving
// director->moveStage() afterward.
void execute();

}  // namespace Warp

#endif  // _SUSAMUNE_WARP_HXX
