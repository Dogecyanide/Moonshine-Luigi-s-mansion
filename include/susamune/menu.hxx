#ifndef _SUSAMUNE_MENU_HXX
#define _SUSAMUNE_MENU_HXX

#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DTextBox.hxx"
#include "SMS/Player/MarioGamePad.hxx"

// =====================================================================
// menu.hxx
//
// The on-screen menu: a tabbed overlay that renders and edits mod state.
// It ONLY handles presentation and navigation -- warping lives in warp.*,
// settings in settings.*. Each tab is a small object implementing the
// internal MenuTab interface (defined in menu.cpp); Menu is a thin frame
// around a list of them, so a new tab is a new class plus one line in the
// constructor.
//
// Menu allocates nothing per item: it owns a single reusable J2DTextBox
// (drawText below) that it re-points at const strings each draw, so the
// pressured system heap is untouched no matter how many tabs/rows exist.
// =====================================================================

class MenuTab;  // internal to menu.cpp

class Menu {
public:
    Menu();

    // Per-frame input (from onUpdate) and render (from afterDraw).
    void update(TMarioGamePad *pad);
    void draw(J2DOrthoGraph *ortho);

    bool shown() const { return mShown; }
    void hide() { mShown = false; }

    // --- helpers a tab uses to render itself (implemented in menu.cpp) ---
    // Draw one line of text with the shared textbox. No allocation: `s` is
    // borrowed (a const literal or a caller-owned scratch buffer).
    void drawText(const char *s, int x, int y, int sizeX, int sizeY, JUtility::TColor color);
    // Draw a filled rectangle. Re-asserts the flat 2D GX vertex state first:
    // J2DFillBox relies on the current vertex descriptor, which text drawing
    // (JUTResFont) reconfigures, so a bare J2DFillBox after any text renders as
    // skewed garbage.
    void fillBox(int x, int y, int w, int h, JUtility::TColor color);
    // Estimated pixel width of `s` at the given cell size (for right-align /
    // tab layout). Font is roughly fixed-advance, so this is len * factor.
    static int textWidth(const char *s, int sizeX);

    static const int kMaxTabs = 12;

private:
    void switchTab(int dir);
    void drawTabStrip(int x, int y, int w);

    J2DTextBox      mText;   // the one reusable textbox; see note above
    J2DOrthoGraph  *mOrtho;  // borrowed from draw(); used to re-enter 2D state
    int             mFontAscent;  // font metrics, cached: baseline<->top offset
    int             mFontHeight;
    bool            mShown;
    int             mCurTab;
    int             mNumTabs;
    int             mTabScrollPx;   // horizontal scroll of the tab strip
    MenuTab        *mTabs[kMaxTabs];
};

// One instance, constructed once at boot (placement-new into BSS -- no heap).
extern Menu *gMenu;
void menuInit();

#endif  // _SUSAMUNE_MENU_HXX
