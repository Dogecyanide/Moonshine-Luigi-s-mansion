// =====================================================================
// menu.cpp
//
// Rendering + navigation for the tabbed mod menu. Everything feature-
// specific is delegated: the warp tabs call into warp.*, the savestate
// tab into settings.*. See menu.hxx for the design.
//
// Memory: the menu owns exactly one J2DTextBox and re-points its mStrPtr
// at borrowed const strings each frame, so it never touches the (nearly
// full) system heap per item. The Menu object itself is placement-new'd
// once into a static BSS buffer -- no persistent heap allocation at all.
// =====================================================================

#include "susamune/menu.hxx"
#include "susamune/settings.hxx"
#include "susamune/warp.hxx"

#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DPane.hxx"  // J2DFillBox
#include "J2D/J2DTextBox.hxx"
#include "SMS/System/Application.hxx"
#include "JKernel/JKRHeap.hxx"  // placement new (operator new(size_t, void*))

// Objects are placement-new'd into BSS buffers so the menu needs no heap.

// Controller-button icon glyphs in the system font (standard_fontEx.bfn). Each
// is the glyph's 2-byte Shift-JIS code: J2DPrint sees the 0x81 lead byte, pulls
// the trail byte, and maps the pair to the icon. Kept as individual string
// literals so the \x escapes never run together (\x is greedy within a literal)
// and so callers can concatenate them with text: BTN_A " Select".
#define BTN_A "\x81\x97"
#define BTN_B "\x81\x94"
#define BTN_X "\x81\x7b"
#define BTN_Y "\x81\x8f"
#define BTN_L "\x81\x83"
#define BTN_R "\x81\x84"
#define BTN_C "\x81\x93"  // C-stick
#define BTN_Z "\x81\x90"

// Full-width symbol glyphs. This font's ASCII->full-width remap is disabled
// (its min map startCode is 0x20, not >=0x8000), so punctuation like '&' must
// be given as its 2-byte code directly -- a plain ASCII '&' resolves to the
// blank default glyph.
#define SYM_AMP "\x81\x95"  // full-width ampersand

namespace {

typedef JUtility::TColor Color;

inline Color col(u8 r, u8 g, u8 b, u8 a) { return Color(r, g, b, a); }

int strLen(const char *s) {
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

// -------- palette (built inline so nothing lives in static storage) -------
inline Color cBackdrop()   { return col(6, 8, 14, 150); }    // full-screen dim
inline Color cPanel()      { return col(24, 28, 40, 240); }  // menu body
inline Color cAccent()     { return col(90, 170, 255, 255); }// primary accent
inline Color cTitle()      { return col(238, 242, 252, 255); }
inline Color cTabIdle()    { return col(150, 160, 182, 255); }
inline Color cTabOnText()  { return col(14, 18, 28, 255); }  // text on accent
inline Color cRowSelBg()   { return col(90, 170, 255, 60); } // selected row bar
inline Color cRowSel()     { return col(255, 255, 255, 255); }
inline Color cRow()        { return col(200, 206, 220, 255); }
inline Color cRowDim()     { return col(120, 130, 150, 255); }
inline Color cValue()      { return col(120, 220, 150, 255); }// setting value
inline Color cFooter()     { return col(104, 114, 136, 255); }

// -------- layout ----------------------------------------------------------
const int PANEL_X = 40;
const int PANEL_Y = 20;
const int PANEL_W = 560;
const int PANEL_H = 400;

const int PAD       = 18;
const int TITLE_SZ  = 20;
const int TAB_SZ    = 18;
const int ROW_SZ    = 16;
const int FOOT_SZ   = 12;

const int TAB_STRIP_Y = PANEL_Y + 46;
const int TAB_STRIP_H = 30;
const int CONTENT_Y   = PANEL_Y + 92;
const int FOOTER_Y    = PANEL_Y + PANEL_H - 26;

}  // namespace

// =====================================================================
// Tab interface + concrete tabs
// =====================================================================

class MenuTab {
public:
    virtual const char *title() const              = 0;
    virtual void update(Menu *menu, TMarioGamePad *pad) = 0;
    // Render into the content rect [x, x+w) x [y, y+h).
    virtual void draw(Menu *menu, int x, int y, int w, int h) = 0;
};

namespace {

// Shared vertical-list helpers so every list-style tab scrolls the same way.
// Returns the index of the first row to draw so that `sel` stays visible.
int listScrollStart(int sel, int count, int maxRows) {
    if (count <= maxRows) {
        return 0;
    }
    int start = sel - maxRows / 2;
    if (start < 0) {
        start = 0;
    }
    if (start > count - maxRows) {
        start = count - maxRows;
    }
    return start;
}

// Draw the selection highlight bar behind a row.
void drawRowHighlight(Menu *menu, int x, int y, int w, int rowH) {
    menu->fillBox(x - 6, y - (rowH - 12) / 2, w + 12, rowH, cRowSelBg());
}

const int ROW_H = ROW_SZ + 8;

// Wrap helper for a cursor over [0, n).
int wrap(int v, int n) {
    if (v < 0) {
        v += n;
    } else if (v >= n) {
        v -= n;
    }
    return v;
}

}  // namespace

// ---------------------------------------------------------------------
// Warp presets tab
// ---------------------------------------------------------------------
class WarpPresetsTab : public MenuTab {
public:
    WarpPresetsTab() : mSel(0) {}

    const char *title() const override { return "Warps"; }

    void update(Menu *menu, TMarioGamePad *pad) override {
        u32 rapid = pad->mButtons.mRapidInput;
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, Warp::kNumPresets);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, Warp::kNumPresets);
        }
        if (rapid & TMarioGamePad::A) {
            const WarpDescriptor &d = Warp::kPresets[mSel];
            Warp::request(d.area, d.episode, d.overrideArea, d.extraFlag);
            menu->hide();
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        int maxRows = h / ROW_H;
        int start   = listScrollStart(mSel, Warp::kNumPresets, maxRows);
        int end     = start + maxRows;
        if (end > Warp::kNumPresets) {
            end = Warp::kNumPresets;
        }
        int ry = y;
        for (int i = start; i < end; i++) {
            bool selected = (i == mSel);
            if (selected) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
                menu->drawText(">", x - 2, ry, ROW_SZ, ROW_SZ, cAccent());
            }
            menu->drawText(Warp::kPresets[i].name, x + 22, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());
            ry += ROW_H;
        }
        drawScrollHints(menu, x, y, w, h, start, end, Warp::kNumPresets);
    }

private:
    void drawScrollHints(Menu *menu, int x, int y, int w, int h, int start, int end,
                         int count) {
        if (start > 0) {
            menu->drawText("^", x + w - 12, y, ROW_SZ, ROW_SZ, cRowDim());
        }
        if (end < count) {
            menu->drawText("v", x + w - 12, y + h - ROW_H, ROW_SZ, ROW_SZ, cRowDim());
        }
    }
    int mSel;
};

// ---------------------------------------------------------------------
// Stage / episode picker tab
// ---------------------------------------------------------------------
class WarpStagesTab : public MenuTab {
public:
    WarpStagesTab() : mArea(0), mEpisode(0) {}

    const char *title() const override { return "Stages"; }

    void update(Menu *menu, TMarioGamePad *pad) override {
        u32 rapid = pad->mButtons.mRapidInput;
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mArea = wrap(mArea - 1, WARP_NUM_STAGES);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mArea = wrap(mArea + 1, WARP_NUM_STAGES);
        }
        if (rapid & TMarioGamePad::CSTICK_LEFT) {
            mEpisode = wrap(mEpisode - 1, WARP_NUM_EPISODES);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            mEpisode = wrap(mEpisode + 1, WARP_NUM_EPISODES);
        }
        if (rapid & TMarioGamePad::A) {
            Warp::request(mArea, mEpisode, -1, -1);
            menu->hide();
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        int maxRows = h / ROW_H;
        int start   = listScrollStart(mArea, WARP_NUM_STAGES, maxRows);
        int end     = start + maxRows;
        if (end > WARP_NUM_STAGES) {
            end = WARP_NUM_STAGES;
        }
        const int epX    = x + 230;
        const int epStep = 26;
        char      digit[2];
        digit[1] = '\0';

        int ry = y;
        for (int i = start; i < end; i++) {
            bool selArea = (i == mArea);
            if (selArea) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
            }
            menu->drawText(Warp::kStageNames[i], x + 4, ry, ROW_SZ, ROW_SZ,
                           selArea ? cRowSel() : cRow());
            for (int e = 0; e < WARP_NUM_EPISODES; e++) {
                bool here = selArea && (e == mEpisode);
                digit[0]  = (char)('1' + e);
                menu->drawText(digit, epX + e * epStep, ry, ROW_SZ, ROW_SZ,
                               here ? cAccent() : (selArea ? cRow() : cRowDim()));
            }
            ry += ROW_H;
        }
    }

private:
    int mArea;
    int mEpisode;
};

// ---------------------------------------------------------------------
// Category settings tab (generic settings renderer)
//
// Renders every setting tagged with `mCat` -- one tab per SettingCategory.
// The setting/value store lives in settings.*; this only navigates and
// draws. The filtered id list is rebuilt each frame off kSettingDescs
// (SETTING_COUNT is tiny), so adding a setting needs no change here.
// ---------------------------------------------------------------------
class CategorySettingsTab : public MenuTab {
public:
    CategorySettingsTab(const char *title, SettingCategory cat)
        : mTitle(title), mCat(cat), mSel(0) {}

    const char *title() const override { return mTitle; }

    void update(Menu *menu, TMarioGamePad *pad) override {
        SettingId ids[SETTING_COUNT];
        int       n = buildList(ids);
        if (n == 0) {
            return;
        }
        u32 rapid = pad->mButtons.mRapidInput;
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, n);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, n);
        }
        if (mSel >= n) {
            mSel = n - 1;
        }
        SettingId id = ids[mSel];
        if ((rapid & TMarioGamePad::A) || (rapid & TMarioGamePad::CSTICK_RIGHT)) {
            gSettings.cycle(id, +1);
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            gSettings.cycle(id, -1);
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        SettingId ids[SETTING_COUNT];
        int       n = buildList(ids);
        if (n == 0) {
            menu->drawText("(none)", x + 4, y, ROW_SZ, ROW_SZ, cRowDim());
            return;
        }
        int maxRows = h / ROW_H;
        int start   = listScrollStart(mSel, n, maxRows);
        int end     = start + maxRows;
        if (end > n) {
            end = n;
        }
        int ry = y;
        for (int i = start; i < end; i++) {
            SettingId          id       = ids[i];
            const SettingDesc &d        = Settings::desc(id);
            bool               selected = (i == mSel);
            if (selected) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
            }
            menu->drawText(d.name, x + 4, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());
            const char *val = gSettings.valueLabel(id);
            int         vx  = x + w - Menu::textWidth(val, ROW_SZ) - 8;
            menu->drawText(val, vx, ry, ROW_SZ, ROW_SZ, cValue());
            ry += ROW_H;
        }
        if (start > 0) {
            menu->drawText("^", x + w - 12, y, ROW_SZ, ROW_SZ, cRowDim());
        }
        if (end < n) {
            menu->drawText("v", x + w - 12, y + h - ROW_H, ROW_SZ, ROW_SZ, cRowDim());
        }
    }

private:
    int buildList(SettingId *out) const {
        int n = 0;
        for (int i = 0; i < SETTING_COUNT; i++) {
            if (Settings::desc((SettingId)i).category == mCat) {
                out[n++] = (SettingId)i;
            }
        }
        return n;
    }

    const char     *mTitle;
    SettingCategory mCat;
    int             mSel;
};

// =====================================================================
// Menu
// =====================================================================

// Static tab instances (constructed via placement new in Menu::Menu so their
// vtables are set without relying on C++ static-init, which the injected mod
// does not run).
namespace {
u8 sPresetsBuf[sizeof(WarpPresetsTab)]         __attribute__((aligned(8)));
u8 sStagesBuf[sizeof(WarpStagesTab)]           __attribute__((aligned(8)));
u8 sQolBuf[sizeof(CategorySettingsTab)]        __attribute__((aligned(8)));
u8 sCosmeticBuf[sizeof(CategorySettingsTab)]   __attribute__((aligned(8)));
u8 sMiscBuf[sizeof(CategorySettingsTab)]       __attribute__((aligned(8)));
u8 sSavestateBuf[sizeof(CategorySettingsTab)]  __attribute__((aligned(8)));
}  // namespace

Menu::Menu() : mText(gpSystemFont->mFont, " ") {
    mOrtho       = nullptr;
    mShown       = false;
    mCurTab      = 0;
    mNumTabs     = 0;
    mTabScrollPx = 0;

    // Cache font metrics. J2DTextBox::draw(x, y) places the text *baseline* at
    // y (glyphs render upward from it: top = y - ascent*size/height). drawText
    // converts a top-anchored y into that baseline so text lines up with the
    // fills drawn behind it.
    mFontAscent = mText.mFont->getAscent();
    mFontHeight = mText.mFont->getHeight();
    if (mFontHeight <= 0) {
        mFontHeight = 1;
    }

    // The ctor above allocated a small string buffer on the current heap via
    // setString(); free it and never let mText reallocate again. From here on
    // drawText() only assigns mStrPtr to borrowed const strings.
    if (mText.mStrPtr) {
        delete[] mText.mStrPtr;
        mText.mStrPtr = nullptr;
    }

    mTabs[mNumTabs++] = new (sPresetsBuf) WarpPresetsTab();
    mTabs[mNumTabs++] = new (sStagesBuf) WarpStagesTab();
    mTabs[mNumTabs++] = new (sQolBuf) CategorySettingsTab("QoL", SETTING_CAT_QOL);
    mTabs[mNumTabs++] = new (sCosmeticBuf) CategorySettingsTab("Cosmetic", SETTING_CAT_COSMETIC);
    mTabs[mNumTabs++] = new (sMiscBuf) CategorySettingsTab("Misc", SETTING_CAT_MISC);
    mTabs[mNumTabs++] = new (sSavestateBuf) CategorySettingsTab("Savestate", SETTING_CAT_SAVESTATE);
}

int Menu::textWidth(const char *s, int sizeX) {
    // Estimate advance width. Half-width ASCII glyphs run ~0.75 cell; a 2-byte
    // Shift-JIS glyph (e.g. a button icon) is full-width, ~1 cell. Detect SJIS
    // lead bytes (0x81-0x9F / 0xE0-0xFC) so a button counts as one glyph, not
    // two, and gets its wider advance -- keeps right-align / tab layout honest.
    int w = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p;) {
        unsigned char c = *p;
        bool lead = (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
        if (lead && p[1]) {
            w += sizeX;      // full-width glyph
            p += 2;
        } else {
            w += sizeX * 3 / 4;  // half-width ASCII
            p += 1;
        }
    }
    return w;
}

void Menu::drawText(const char *s, int x, int y, int sizeX, int sizeY, Color color) {
    mText.mCharSizeX      = sizeX;
    mText.mCharSizeY      = sizeY;
    mText.mGradientTop    = color;
    mText.mGradientBottom = color;
    mText.mStrPtr         = const_cast<char *>(s);
    // `y` is the cell top; convert to the baseline J2DTextBox::draw expects.
    int baseline = y + mFontAscent * sizeY / mFontHeight;
    mText.draw(x, baseline);
}

void Menu::fillBox(int x, int y, int w, int h, Color color) {
    // Restore the flat pos+color vertex state before filling. J2DFillBox draws
    // with whatever GX vertex descriptor is current, and J2DTextBox/JUTResFont
    // switch it to a textured layout; without this, any fill after a text draw
    // renders as skewed garbage. setup2D() leaves the projection alone.
    if (mOrtho) {
        mOrtho->setup2D();
    }
    J2DFillBox(x, y, w, h, color);
}

void Menu::switchTab(int dir) {
    mCurTab = wrap(mCurTab + dir, mNumTabs);
}

void Menu::drawTabStrip(int x, int y, int w) {
    const int gap   = 20;    // space between tabs
    const int inner = 12;    // highlight padding around active tab text
    const int chevW = 16;    // reserved for scroll chevrons

    // First pass: locate the selected tab's logical x-range and total width.
    int cursor = 0;
    int selX0 = 0, selX1 = 0;
    for (int i = 0; i < mNumTabs; i++) {
        int tw = textWidth(mTabs[i]->title(), TAB_SZ) + inner * 2;
        if (i == mCurTab) {
            selX0 = cursor;
            selX1 = cursor + tw;
        }
        cursor += tw + gap;
    }
    int totalW = cursor - gap;
    int visW   = w - chevW * 2;

    // Keep the selected tab fully inside the visible window.
    if (selX0 - mTabScrollPx < 0) {
        mTabScrollPx = selX0;
    }
    if (selX1 - mTabScrollPx > visW) {
        mTabScrollPx = selX1 - visW;
    }
    if (mTabScrollPx < 0 || totalW <= visW) {
        mTabScrollPx = 0;
    }

    int stripX = x + chevW;

    // Second pass: draw the tabs that fit fully in the window.
    cursor = 0;
    for (int i = 0; i < mNumTabs; i++) {
        int tw    = textWidth(mTabs[i]->title(), TAB_SZ) + inner * 2;
        int drawX = stripX + (cursor - mTabScrollPx);
        if (drawX >= stripX && drawX + tw <= stripX + visW) {
            bool active = (i == mCurTab);
            if (active) {
                fillBox(drawX, y, tw, TAB_STRIP_H, cAccent());
            }
            drawText(mTabs[i]->title(), drawX + inner, y + 6, TAB_SZ, TAB_SZ,
                     active ? cTabOnText() : cTabIdle());
        }
        cursor += tw + gap;
    }

    // Scroll chevrons when tabs overflow either edge.
    if (mTabScrollPx > 0) {
        drawText("<", x, y + 6, TAB_SZ, TAB_SZ, cAccent());
    }
    if (totalW - mTabScrollPx > visW) {
        drawText(">", x + w - chevW + 2, y + 6, TAB_SZ, TAB_SZ, cAccent());
    }
}

void Menu::update(TMarioGamePad *pad) {
    u32 input = pad->mButtons.mInput;
    u32 rapid = pad->mButtons.mRapidInput;

    if ((input & TMarioGamePad::Y) && (rapid & TMarioGamePad::START)) {
        mShown = !mShown;
        return;
    }
    if (!mShown) {
        return;
    }

    if (rapid & TMarioGamePad::L) {
        switchTab(-1);
    }
    if (rapid & TMarioGamePad::R) {
        switchTab(+1);
    }

    mTabs[mCurTab]->update(this, pad);
}

void Menu::draw(J2DOrthoGraph *ortho) {
    mOrtho = ortho;  // used by fillBox() to re-enter 2D state
    if (!mShown) {
        return;
    }

    // Dim the whole frame, then the panel on top.
    fillBox(0, 0, 640, 480, cBackdrop());
    fillBox(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, cPanel());
    // Accent rule at the top edge of the panel.
    fillBox(PANEL_X, PANEL_Y, PANEL_W, 3, cAccent());

    // Title + accent underline.
    drawText("susamune", PANEL_X + PAD - 2, PANEL_Y + 12, TITLE_SZ, TITLE_SZ, cTitle());
    fillBox(PANEL_X + PAD, PANEL_Y + 12 + TITLE_SZ + 1, 150, 2, cAccent());

    drawTabStrip(PANEL_X + PAD, TAB_STRIP_Y, PANEL_W - PAD * 2);

    int cx = PANEL_X + PAD;
    int cy = CONTENT_Y;
    int cw = PANEL_W - PAD * 2;
    int ch = FOOTER_Y - CONTENT_Y - 8;
    mTabs[mCurTab]->draw(this, cx, cy, cw, ch);

    drawText(BTN_L "/" BTN_R " Tabs    " BTN_C " Move    " BTN_A " Select    "
             BTN_Y SYM_AMP "Start Close",
             PANEL_X + PAD, FOOTER_Y, FOOT_SZ, FOOT_SZ, cFooter());
}

// =====================================================================
// Global instance
// =====================================================================

Menu *gMenu = nullptr;

namespace {
u8 sMenuBuf[sizeof(Menu)] __attribute__((aligned(8)));
}  // namespace

void menuInit() { gMenu = new (sMenuBuf) Menu(); }
