#ifndef _SUSAMUNE_STAGE_LOADER_HXX
#define _SUSAMUNE_STAGE_LOADER_HXX

#include <Dolphin/types.h>

class Menu;

namespace StageLoader {

void init();

// Start one exact IL route repeatedly. A negative target accepts any eligible
// finish; otherwise the result must be at or below targetQf.
bool start(int entry, u16 finishes, s32 targetQf);
void cancel();
bool active();

void update();
void draw(Menu *menu);

// ILing owns attempt identity and reports only exact catalogue routes here.
void onILAttemptStarted(int entry);
void onILAttemptEnded();
void onILResult(int entry, s32 qf, bool eligible);
void onILWarpCancelled();

}  // namespace StageLoader

#endif  // _SUSAMUNE_STAGE_LOADER_HXX
