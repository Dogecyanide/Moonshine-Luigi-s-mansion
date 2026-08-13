#ifndef _SUSAMUNE_WALLKICK_DISPLAY_HXX
#define _SUSAMUNE_WALLKICK_DISPLAY_HXX

class Menu;

namespace WallkickDisplay {

void onStageSetup();
void beforeDirect(bool active);
void afterDirect(bool active);
void draw(Menu *menu);

}  // namespace WallkickDisplay

#endif  // _SUSAMUNE_WALLKICK_DISPLAY_HXX
