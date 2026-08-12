#ifndef _SUSAMUNE_WARP_HXX
#define _SUSAMUNE_WARP_HXX

#include <Dolphin/types.h>

// A named debug warp and its optional current-area/flag setup.
struct WarpDescriptor {
    const char *name;
    u16         area;
    u16         episode;
    s32         overrideArea;
    s32         extraFlag;
};

#define WARP_NUM_EPISODES 8
#define WARP_NUM_STAGES   10

namespace Warp {

extern const char *const kStageNames[WARP_NUM_STAGES];
extern const WarpDescriptor kPresets[];
extern const int            kNumPresets;

void request(s32 area, s32 episode, s32 overrideArea, s32 extraFlag);
bool pending();
void execute();

}  // namespace Warp

#endif  // _SUSAMUNE_WARP_HXX
