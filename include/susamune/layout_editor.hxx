#ifndef _SUSAMUNE_LAYOUT_EDITOR_HXX
#define _SUSAMUNE_LAYOUT_EDITOR_HXX

#include <Dolphin/types.h>

class Menu;

namespace LayoutEditor {

bool updatePositionScale(u32 rapid, u16 &x, u16 &y, u8 &scale);
void drawHeader(Menu *menu, int boxHeight, const char *title, const char *status);

}  // namespace LayoutEditor

#endif  // _SUSAMUNE_LAYOUT_EDITOR_HXX
