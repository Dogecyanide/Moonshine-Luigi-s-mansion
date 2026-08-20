#ifndef _SUSAMUNE_STAGE_TARGETS_HXX
#define _SUSAMUNE_STAGE_TARGETS_HXX

#include <Dolphin/types.h>

class Menu;

namespace StageTargets {

void init();
void service(Menu *menu);
s32 get(int entry);
void set(int entry, s32 targetQf);
bool available();

}  // namespace StageTargets

#endif  // _SUSAMUNE_STAGE_TARGETS_HXX
