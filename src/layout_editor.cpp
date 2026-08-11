#include "susamune/layout_editor.hxx"

#include "SMS/Player/MarioGamePad.hxx"
#include "susamune/menu.hxx"

namespace {

int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

}  // namespace

namespace LayoutEditor {

bool updatePositionScale(u32 rapid, u16 &x, u16 &y, u8 &scale, int maxScale) {
    if (rapid & TMarioGamePad::DPAD_LEFT) {
        x = (u16)clampi((int)x - 2, 0, 640);
    }
    if (rapid & TMarioGamePad::DPAD_RIGHT) {
        x = (u16)clampi((int)x + 2, 0, 640);
    }
    if (rapid & TMarioGamePad::DPAD_UP) {
        y = (u16)clampi((int)y - 2, 0, 480);
    }
    if (rapid & TMarioGamePad::DPAD_DOWN) {
        y = (u16)clampi((int)y + 2, 0, 480);
    }
    if (rapid & TMarioGamePad::L) {
        scale = (u8)clampi((int)scale - 2, 50, maxScale);
    }
    if (rapid & TMarioGamePad::R) {
        scale = (u8)clampi((int)scale + 2, 50, maxScale);
    }
    const u32 controls = TMarioGamePad::DPAD_LEFT | TMarioGamePad::DPAD_RIGHT |
                         TMarioGamePad::DPAD_UP | TMarioGamePad::DPAD_DOWN |
                         TMarioGamePad::L | TMarioGamePad::R;
    return (rapid & controls) != 0;
}

void drawHeader(Menu *menu, int boxHeight, const char *title, const char *status) {
    menu->fillBox(8, 8, 624, boxHeight, JUtility::TColor(0, 0, 0, 205));
    menu->drawText(title, 18, 15, 16, 16, JUtility::TColor(255, 255, 255, 255));
    menu->drawText(status, 18, 38, 11, 11, JUtility::TColor(190, 220, 255, 255));
}

}  // namespace LayoutEditor
