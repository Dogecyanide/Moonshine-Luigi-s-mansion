#ifndef _SUSAMUNE_ILING_HXX
#define _SUSAMUNE_ILING_HXX

#include <Dolphin/types.h>

class Menu;

namespace ILing {

void init();
int count();
const char *label(int entry);
s32 pbQf(int entry);
int jumpGroup(int entry, int direction);
bool beginsGroup(int entry);
const char *groupName(int entry);

bool start(int entry);
void clearPB(int entry);
void update();
// Called at LevelWarp's transition tail, after the old director is finished
// but before the destination director is constructed.
void onWarpTail();
void beforeStageSetup();
void onStageSetup();
void onSavestateSaved();
void onSavestateLoaded();

// PB result banner, drawn through Menu's shared no-allocation renderer.
void draw(Menu *menu);

}  // namespace ILing

#endif  // _SUSAMUNE_ILING_HXX
