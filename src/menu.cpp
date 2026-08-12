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
#include "susamune/binds.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/input_display.hxx"
#include "susamune/iling.hxx"
#include "susamune/metadata_display.hxx"
#include "susamune/attempt_counter.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/qft_display.hxx"
#include "susamune/settings.hxx"
#if ENABLE_DEBUG_WARPS
#include "susamune/debug_warp.hxx"
#endif

#include "Dolphin/string.h"
#include "Dolphin/printf.h"
#include "Dolphin/GX.h"
#include "JSystem/JAudio/JASystem/JASTrackMgr.hxx"
#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DPane.hxx"  // J2DFillBox
#include "J2D/J2DTextBox.hxx"
#include "SMS/System/Application.hxx"
#include "JKernel/JKRHeap.hxx"  // placement new (operator new(size_t, void*))

// Objects are placement-new'd into BSS buffers so the menu needs no heap.

namespace {

typedef JUtility::TColor Color;

inline Color col(u8 r, u8 g, u8 b, u8 a) { return Color(r, g, b, a); }

// -------- palette (built inline so nothing lives in static storage) -------
inline Color cBackdrop()   { return col(6, 8, 14, 150); }    // full-screen dim
inline Color cPanel()      {
    const u8 *rgb = gCreationExtras.menuBackground();
    return col(rgb[0], rgb[1], rgb[2], 240);
}  // menu body
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

const char *settingsStorageError(u32 error) {
    switch (error) {
#if IS_EMULATOR
    case 2:   return "wrong device in slot B";
    case 3:   return "no card in slot B";
    case 4:   return "settings file missing";
    case 5:   return "slot B card I/O error";
    case 6:   return "slot B card unformatted";
    case 7:   return "settings file already exists";
    case 8:   return "card directory is full";
    case 9:   return "slot B card is full";
    case 10:  return "settings file not writable";
    case 11:  return "slot B card limit reached";
    case 12:  return "settings filename too long";
    case 13:  return "slot B encoding mismatch";
    case 14:  return "card operation canceled";
    case 128: return "fatal slot B card error";
    case 256: return "not enough memory";
#else
    case 1:  return "storage I/O error";
    case 2:  return "storage internal error";
    case 3:  return "storage not ready";
    case 4:  return "settings file missing";
    case 5:  return "settings path missing";
    case 6:  return "invalid settings filename";
    case 7:  return "storage access denied";
    case 8:  return "settings file already exists";
    case 9:  return "invalid settings file";
    case 10: return "storage is write-protected";
    case 11: return "invalid storage device";
    case 12: return "storage not mounted";
    case 13: return "storage has no FAT filesystem";
    case 14: return "format aborted";
    case 15: return "storage timed out";
    case 16: return "settings file locked";
    case 17: return "not enough memory";
    case 18: return "too many open files";
    case 19: return "invalid storage parameter";
#endif
    default:  return nullptr;
    }
}

// -------- font metrics ----------------------------------------------------
// Cached in Menu::Menu so the static textWidth() can reach them. They describe
// the same font drawText() renders with (gpSystemFont), so measuring matches
// what J2DPrint (via J2DTextBox::draw) actually lays out.
JUTFont *sFont        = nullptr;
int      sFontWidth   = 1;      // design cell width; every advance scales by size/this
int      sCharSpacing = 0;      // J2DTextBox's spacing, handed to J2DPrint
bool     sFontFixed   = false;  // font advances every glyph by sFontFixedW
int      sFontFixedW  = 0;

// -------- layout ----------------------------------------------------------
const int PANEL_X = 40;
const int PANEL_Y = 20;
const int PANEL_W = 560;
const int PANEL_H = 400;

const int PAD       = 18;
const int TITLE_SZ  = 20;
const int TAB_SZ    = 18;
const int ROW_SZ    = 16;
const int ROW_H = ROW_SZ + 8;
const int FOOT_SZ   = 12;

const int TAB_GAP   = 20;  // space between tabs
const int TAB_INNER = 12;  // highlight padding around a tab's text
const int TAB_CHEV  = 16;  // strip margin reserved for the scroll chevrons

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
    // While true, Menu::update hands the pad to this tab alone: no tab
    // switching, no close combo. The binds tab needs it, since every button
    // it might record is also a menu control.
    virtual bool grabsInput() const { return false; }
    // A live editor can temporarily replace the normal panel while retaining
    // the menu's input grab and stage-freeze behaviour.
    virtual bool fullScreen() const { return false; }

protected:
    void drawScrollHints(Menu *menu, int x, int y, int w, int h, int start, int end,
                         int count) {
        const int cx = x + w - 7;
        if (start > 0) {
            const int top = y - ROW_H + 5;
            const s16 up[6] = {
                (s16)cx,       (s16)top,
                (s16)(cx - 6), (s16)(top + 9),
                (s16)(cx + 6), (s16)(top + 9)
            };
            menu->fillPoly(up, 3, cRowDim());
        }
        if (end < count) {
            const int top = y + h - ROW_SZ + 2;
            const s16 down[6] = {
                (s16)(cx - 6), (s16)top,
                (s16)(cx + 6), (s16)top,
                (s16)cx,       (s16)(top + 9)
            };
            menu->fillPoly(down, 3, cRowDim());
        }
    }
};

namespace {

// Shared vertical-list helpers so every list-style tab scrolls the same way.
// Keep multi-call helpers out of line; duplicating them costs hundreds of bytes.
// Returns the index of the first row to draw so that `sel` stays visible.
__attribute__((noinline)) int listScrollStart(int sel, int count, int maxRows) {
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
__attribute__((noinline)) void drawRowHighlight(Menu *menu, int x, int y, int w, int rowH) {
    menu->fillBox(x - 6, y - (rowH - 12) / 2, w + 12, rowH, cRowSelBg());
}

__attribute__((noinline)) void drawSectionHeader(Menu *menu, int x, int y, int w,
                                                 const char *label) {
    menu->drawText(label, x + 4, y + 3, 13, 13, cAccent());
    const int lineX = x + 14 + Menu::textWidth(label, 13);
    menu->fillBox(lineX, y + 11, x + w - lineX - 8, 1, cRowDim());
}

// Wrap helper for a cursor over [0, n).
__attribute__((noinline)) int wrap(int v, int n) {
    if (v < 0) {
        v += n;
    } else if (v >= n) {
        v -= n;
    }
    return v;
}

void bgmStatsDraw(Menu *menu) {
    if (!gSettings.getBool(SETTING_SHOW_BGM_SLOTS) || !JASystem::TrackMgr::sRootTrack) {
        return;
    }

    u32 freeRoots = 0;
    for (int i = 0; i < 8; i++) {
        if (!JASystem::TrackMgr::sRootTrack[i]) {
            freeRoots++;
        }
    }

    char text[24];
    snprintf(text, sizeof(text), "RT:%lu S:%lu", freeRoots,
             JASystem::TrackMgr::seqRemain);

    const int size = 16;
    const int tw = Menu::textWidth(text, size);
    const int x    = 640 - 20 - tw;
    const int y    = 480 - 80 - size;
    menu->fillBox(x, y, tw + 1, size + 1, col(0, 0, 0, 180));
    menu->drawText(text, x, y, size, size, col(255, 255, 255, 255));
}

}  // namespace

// ---------------------------------------------------------------------
// IL and travel practice
// ---------------------------------------------------------------------
class ILingTab : public MenuTab {
public:
    ILingTab() : mSel(0), mConfirmDelete(false) {}

    const char *title() const override { return "ILs"; }
    bool grabsInput() const override { return mConfirmDelete; }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (mConfirmDelete) {
            const u32 rapid = pad->mButtons.mRapidInput;
            if (rapid & TMarioGamePad::A) {
                ILing::clearPB(mSel);
                mConfirmDelete = false;
                menu->toast("PB deleted");
            } else if (rapid & TMarioGamePad::B) {
                mConfirmDelete = false;
            }
            return;
        }

        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, ILing::count());
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, ILing::count());
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            mSel = ILing::jumpGroup(mSel, -1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            mSel = ILing::jumpGroup(mSel, 1);
        }
        if (rapid & TMarioGamePad::A) {
            if (ILing::start(mSel)) {
                menu->hide();
            } else {
                menu->toast("Warps disabled");
            }
        } else if ((rapid & TMarioGamePad::X) && ILing::pbQf(mSel) >= 0) {
            mConfirmDelete = true;
        } else if (rapid & TMarioGamePad::Z) {
            gSettings.cycle(SETTING_ILING_FANFARE, +1);
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (mConfirmDelete) {
            const char *question = "Delete PB?";
            const char *entry = ILing::label(mSel);
            const char *hint = SUSAMUNE_GLYPH_A " Yes    " SUSAMUNE_GLYPH_B " No";
            menu->fillBox(x, y + 34, w, 104, JUtility::TColor(36, 30, 20, 245));
            menu->fillBox(x, y + 34, w, 3, cAccent());
            menu->drawText(question,
                           x + (w - Menu::textWidth(question, ROW_SZ)) / 2,
                           y + 52, ROW_SZ, ROW_SZ, cRowSel());
            menu->drawText(entry,
                           x + (w - Menu::textWidth(entry, ROW_SZ)) / 2,
                           y + 78, ROW_SZ, ROW_SZ, cValue());
            menu->drawText(hint,
                           x + (w - Menu::textWidth(hint, FOOT_SZ)) / 2,
                           y + 112, FOOT_SZ, FOOT_SZ, cFooter());
            return;
        }

        const int entries = ILing::count();
        const int listH = h - ROW_H;
        const int maxRows = listH / ROW_H;
        const int rows = menuRowCount(entries);
        const int start = listScrollStart(menuRowForEntry(mSel), rows, maxRows);
        int end = start + maxRows;
        if (end > rows) {
            end = rows;
        }

        int ry = y;
        int row = 0;
        for (int i = 0; i < entries && row < end; i++) {
            if (ILing::beginsGroup(i)) {
                if (row >= start) {
                    drawSectionHeader(menu, x, ry, w, ILing::groupName(i));
                    ry += ROW_H;
                }
                row++;
                if (row >= end) break;
            }
            if (row < start) {
                row++;
                continue;
            }

            const bool selected = i == mSel;
            if (selected) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
                menu->drawText(">", x - 2, ry, ROW_SZ, ROW_SZ, cAccent());
            }
            menu->drawText(ILing::label(i), x + 12, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());

            char pb[24];
            const char *value = "[PB: --]";
            const s32 qf = ILing::pbQf(i);
            if (qf >= 0) {
                const s32 millis = (qf * 1001) / 120;
                snprintf(pb, sizeof(pb), "[PB: %d:%02d.%03d]",
                         (int)(millis / 60000),
                         (int)((millis / 1000) % 60),
                         (int)(millis % 1000));
                value = pb;
            }
            menu->drawText(value, x + w - Menu::textWidth(value, ROW_SZ) - 8,
                           ry, ROW_SZ, ROW_SZ, cValue());
            ry += ROW_H;
            row++;
        }

        drawScrollHints(menu, x, y, w, listH, start, end, rows);
        menu->drawText(SUSAMUNE_GLYPH_A " Start  " SUSAMUNE_GLYPH_X
                       " Delete  " SUSAMUNE_GLYPH_C " U/D Scroll L/R Group",
                       x + 4, y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
        const char *fanfare = gSettings.getBool(SETTING_ILING_FANFARE)
                                   ? SUSAMUNE_GLYPH_Z " Fanfare: On"
                                   : SUSAMUNE_GLYPH_Z " Fanfare: Off";
        menu->drawText(fanfare,
                       x + w - Menu::textWidth(fanfare, FOOT_SZ) - 8,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    static int menuRowForEntry(int entry) {
        int row = entry;
        for (int i = 0; i <= entry; i++) {
            if (ILing::beginsGroup(i)) row++;
        }
        return row;
    }

    static int menuRowCount(int entries) {
        return menuRowForEntry(entries - 1) + 1;
    }

    int mSel;
    bool mConfirmDelete;
};

#if ENABLE_DEBUG_WARPS
class WarpPresetsTab : public MenuTab {
public:
    WarpPresetsTab() : mSel(0) {}
    const char *title() const override { return "Warps"; }

    void update(Menu *menu, TMarioGamePad *pad) override {
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP)
            mSel = wrap(mSel - 1, Warp::kNumPresets);
        else if (rapid & TMarioGamePad::CSTICK_DOWN)
            mSel = wrap(mSel + 1, Warp::kNumPresets);
        if (rapid & TMarioGamePad::A) {
            const WarpDescriptor &d = Warp::kPresets[mSel];
            Warp::request(d.area, d.episode, d.overrideArea, d.extraFlag);
            menu->hide();
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        const int maxRows = h / ROW_H;
        const int start = listScrollStart(mSel, Warp::kNumPresets, maxRows);
        int end = start + maxRows;
        if (end > Warp::kNumPresets) end = Warp::kNumPresets;
        int ry = y;
        for (int i = start; i < end; i++, ry += ROW_H) {
            const bool selected = i == mSel;
            if (selected) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
                menu->drawText(">", x - 2, ry, ROW_SZ, ROW_SZ, cAccent());
            }
            menu->drawText(Warp::kPresets[i].name, x + 22, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());
        }
        drawScrollHints(menu, x, y, w, h, start, end, Warp::kNumPresets);
    }

private:
    int mSel;
};

class WarpStagesTab : public MenuTab {
public:
    WarpStagesTab() : mArea(0), mEpisode(0) {}
    const char *title() const override { return "Stages"; }

    void update(Menu *menu, TMarioGamePad *pad) override {
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP)
            mArea = wrap(mArea - 1, WARP_NUM_STAGES);
        else if (rapid & TMarioGamePad::CSTICK_DOWN)
            mArea = wrap(mArea + 1, WARP_NUM_STAGES);
        if (rapid & TMarioGamePad::CSTICK_LEFT)
            mEpisode = wrap(mEpisode - 1, WARP_NUM_EPISODES);
        else if (rapid & TMarioGamePad::CSTICK_RIGHT)
            mEpisode = wrap(mEpisode + 1, WARP_NUM_EPISODES);
        if (rapid & TMarioGamePad::A) {
            Warp::request(mArea, mEpisode, -1, -1);
            menu->hide();
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        const int maxRows = h / ROW_H;
        const int start = listScrollStart(mArea, WARP_NUM_STAGES, maxRows);
        int end = start + maxRows;
        if (end > WARP_NUM_STAGES) end = WARP_NUM_STAGES;
        const int epX = x + 230;
        char digit[2] = {'1', '\0'};
        int ry = y;
        for (int i = start; i < end; i++, ry += ROW_H) {
            const bool selected = i == mArea;
            if (selected) drawRowHighlight(menu, x, ry, w, ROW_H);
            menu->drawText(Warp::kStageNames[i], x + 4, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());
            for (int e = 0; e < WARP_NUM_EPISODES; e++) {
                digit[0] = (char)('1' + e);
                menu->drawText(digit, epX + e * 26, ry, ROW_SZ, ROW_SZ,
                               selected && e == mEpisode ? cAccent()
                               : selected ? cRow() : cRowDim());
            }
        }
    }

private:
    int mArea;
    int mEpisode;
};
#endif

// ---------------------------------------------------------------------
// Category settings tab (generic settings renderer)
//
// Renders every setting tagged with `mCat` -- one tab per SettingCategory.
// The setting/value store lives in settings.*; this only navigates and
// draws. The filtered id list is rebuilt each frame from Settings metadata
// (SETTING_COUNT is tiny), so adding a setting needs no change here.
// ---------------------------------------------------------------------
namespace {

const char kCategoryTitles[] =
    "QoL\0Savestate\0Misc\0Cosmetic\0UI\0Timer";

enum CategoryTitleOffset {
    TITLE_QOL       = 0,
    TITLE_SAVESTATE = TITLE_QOL + sizeof("QoL"),
    TITLE_MISC      = TITLE_SAVESTATE + sizeof("Savestate"),
    TITLE_COSMETIC  = TITLE_MISC + sizeof("Misc"),
    TITLE_UI        = TITLE_COSMETIC + sizeof("Cosmetic"),
    TITLE_TIMER     = TITLE_UI + sizeof("UI"),
};

static_assert(sizeof(kCategoryTitles) == 37, "category title offsets changed");
static_assert(SETTING_COUNT <= 0x100, "setting ids no longer fit in a byte");
static_assert(SETTING_CAT_COUNT <= 0x100, "setting categories no longer fit in a byte");

}  // namespace

class CategorySettingsTab : public MenuTab {
public:
    CategorySettingsTab(u8 titleOffset, SettingCategory cat)
        : mSel(0), mCat((u8)cat), mTitleOffset(titleOffset) {}

    const char *title() const override { return kCategoryTitles + mTitleOffset; }

    void update(Menu *menu, TMarioGamePad *pad) override {
        u8  ids[SETTING_COUNT];
        int n = buildList(ids);
        if (n == 0) {
            return;
        }
        u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, n);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, n);
        }
        if (mSel >= n) {
            mSel = n - 1;
        }
        SettingId id = (SettingId)ids[mSel];
        if ((rapid & TMarioGamePad::A) || (rapid & TMarioGamePad::CSTICK_RIGHT)) {
            gSettings.cycle(id, +1);
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            gSettings.cycle(id, -1);
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        u8  ids[SETTING_COUNT];
        int n = buildList(ids);
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
            SettingId id       = (SettingId)ids[i];
            bool      selected = (i == mSel);
            if (selected) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
            }
            menu->drawText(Settings::name(id), x + 4, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());
            const char *val = gSettings.valueLabel(id);
            int         vx  = x + w - Menu::textWidth(val, ROW_SZ) - 8;
            menu->drawText(val, vx, ry, ROW_SZ, ROW_SZ, cValue());
            ry += ROW_H;
        }
    
        drawScrollHints(menu, x, y, w, h, start, end, n);
    }

private:
    int buildList(u8 *out) const {
        int n = 0;
        for (int i = 0; i < SETTING_COUNT; i++) {
            if (Settings::category((SettingId)i) == (SettingCategory)mCat) {
                out[n++] = (u8)i;
            }
        }
        return n;
    }

    u8 mSel;
    u8 mCat;
    u8 mTitleOffset;
};

static_assert(sizeof(CategorySettingsTab) == 8, "category tab must stay one slot");

// ---------------------------------------------------------------------
// Creation -- shared visual editor for compact QFT, Input and Metadata.
// ---------------------------------------------------------------------
class CreationTab final : public MenuTab {
public:
    CreationTab() : mSel(ROW_QFT_EDITOR) {}

    const char *title() const override { return "Creation"; }
    bool grabsInput() const override {
        return gQftDisplay.editing() || gInputDisplay.editing() ||
               gMetadataDisplay.editing() || gCreationExtras.editing();
    }
    bool fullScreen() const override { return grabsInput(); }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (gQftDisplay.editing()) {
            gQftDisplay.updateEditor(pad);
            return;
        }
        if (gInputDisplay.editing()) {
            gInputDisplay.updateEditor(pad);
            return;
        }
        if (gMetadataDisplay.editing()) {
            gMetadataDisplay.updateEditor(pad);
            return;
        }
        if (gCreationExtras.editing()) {
            gCreationExtras.updateEditor(pad);
            return;
        }
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            moveSelection(-1);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            moveSelection(+1);
        }
        if (rapid & (TMarioGamePad::A | TMarioGamePad::CSTICK_RIGHT)) {
            activate(+1);
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            activate(-1);
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (gQftDisplay.editing()) {
            gQftDisplay.drawEditor(menu);
            return;
        }
        if (gInputDisplay.editing()) {
            gInputDisplay.drawEditor(menu);
            return;
        }
        if (gMetadataDisplay.editing()) {
            gMetadataDisplay.drawEditor(menu);
            return;
        }
        if (gCreationExtras.editing()) {
            gCreationExtras.drawEditor(menu);
            return;
        }

        const int hintY = y + h - FOOT_SZ;
        h -= ROW_H;
        const int count = ROW_COUNT;
        const int maxRows = h / ROW_H;
        const int start = listScrollStart(mSel, count, maxRows);
        int end = start + maxRows;
        if (end > count) end = count;

        int ry = y;
        for (int row = start; row < end; row++) {
            if (isSeparator(row)) {
                const char *label = row == ROW_QFT_HEADER ? "QFT"
                    : row == ROW_INPUT_HEADER ? "INPUT DISPLAY"
                    : row == ROW_METADATA_HEADER ? "METADATA"
                    : gCreationExtras.menuRowName(extraRow(row));
                drawSectionHeader(menu, x, ry, w, label);
            } else {
                const bool selected = row == mSel;
                if (selected) drawRowHighlight(menu, x, ry, w, ROW_H);
                const char *name = rowName(row);
                const char *value = rowValue(row);
                menu->drawText(name, x + 4, ry, ROW_SZ, ROW_SZ,
                               selected ? cRowSel() : cRow());
                menu->drawText(value,
                               x + w - Menu::textWidth(value, ROW_SZ) - 8,
                               ry, ROW_SZ, ROW_SZ, cValue());
            }
            ry += ROW_H;
        }
        drawScrollHints(menu, x, y, w, h, start, end, count);
        menu->drawText(SUSAMUNE_GLYPH_A " Open/Change   Saved when menu closes",
                       x + 4, hintY, FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    enum Row {
        ROW_QFT_HEADER,
        ROW_QFT_EDITOR,
        ROW_QFT_LEADING_ZERO,
        ROW_SUNSHINE_TIMER,
        ROW_INPUT_HEADER,
        ROW_INPUT_FIRST,
        ROW_INPUT_END = ROW_INPUT_FIRST + InputDisplay::MENU_ROW_COUNT,
        ROW_METADATA_HEADER = ROW_INPUT_END,
        ROW_METADATA_FIRST,
        ROW_METADATA_END = ROW_METADATA_FIRST + MetadataDisplay::FIELD_COUNT + 4,
        ROW_EXTRAS_FIRST = ROW_METADATA_END,
        ROW_COUNT = ROW_EXTRAS_FIRST + CreationExtras::MENU_ROW_COUNT,
    };

    static bool isSeparator(int row) {
        return row == ROW_QFT_HEADER || row == ROW_INPUT_HEADER ||
               row == ROW_METADATA_HEADER ||
               (row >= ROW_EXTRAS_FIRST &&
                gCreationExtras.menuRowSeparator(extraRow(row)));
    }

    void moveSelection(int dir) {
        do {
            mSel = (u8)wrap(mSel + dir, ROW_COUNT);
        } while (isSeparator(mSel));
    }

    void activate(int dir) {
        if (mSel == ROW_QFT_EDITOR) {
            gQftDisplay.beginEditor();
        } else if (mSel == ROW_QFT_LEADING_ZERO) {
            gQftDisplay.toggleLeadingZero();
        } else if (mSel == ROW_SUNSHINE_TIMER) {
            gCreationExtras.beginTimerEditor();
        } else if (mSel >= ROW_INPUT_FIRST && mSel < ROW_INPUT_END) {
            gInputDisplay.adjustMenuRow(inputRow(mSel), dir);
        } else if (mSel >= ROW_METADATA_FIRST && mSel < ROW_METADATA_END) {
            gMetadataDisplay.adjustMenuRow(metadataRow(mSel), dir);
        } else if (mSel >= ROW_EXTRAS_FIRST) {
            gCreationExtras.adjustMenuRow(extraRow(mSel), dir);
        }
    }

    const char *rowName(int row) const {
        if (row == ROW_QFT_EDITOR) return "QFT timer";
        if (row == ROW_QFT_LEADING_ZERO) return "QFT leading zero";
        if (row == ROW_SUNSHINE_TIMER) return "Sunshine timer";
        if (row >= ROW_INPUT_FIRST && row < ROW_INPUT_END)
            return InputDisplay::menuRowName(inputRow(row));
        if (row >= ROW_METADATA_FIRST && row < ROW_METADATA_END)
            return gMetadataDisplay.menuRowName(metadataRow(row));
        return gCreationExtras.menuRowName(extraRow(row));
    }

    const char *rowValue(int row) const {
        if (row == ROW_QFT_EDITOR) return "Edit";
        if (row == ROW_QFT_LEADING_ZERO)
            return gQftDisplay.leadingZero() ? "On" : "Off";
        if (row == ROW_SUNSHINE_TIMER) return "Edit";
        if (row >= ROW_INPUT_FIRST && row < ROW_INPUT_END)
            return gInputDisplay.menuRowValue(inputRow(row));
        if (row >= ROW_METADATA_FIRST && row < ROW_METADATA_END)
            return gMetadataDisplay.menuRowValue(metadataRow(row));
        return gCreationExtras.menuRowValue(extraRow(row));
    }

    static int inputRow(int row) {
        const int local = row - ROW_INPUT_FIRST;
        if (local == 0) return 4;
        if (local <= 4) return local - 1;
        return 5;
    }

    static int metadataRow(int row) {
        const int local = row - ROW_METADATA_FIRST;
        const int style = 2 + MetadataDisplay::FIELD_COUNT;
        if (local == 0) return style;
        if (local <= style) return local - 1;
        return style + 1;
    }

    static int extraRow(int row) { return row - ROW_EXTRAS_FIRST; }

    u8 mSel;
};

// ---------------------------------------------------------------------
// Binds tab
//
// One row per BindId. A on a row arms gBinds' recorder, which watches the
// raw pad and commits a combo when a held button comes back up (or when
// four are down). All of the recorder's logic is in binds.cpp; this tab
// only starts it, reports it, and takes the pad away from the rest of the
// menu while it runs -- otherwise pressing L to record would switch tabs
// and Y+Start would close the menu.
// ---------------------------------------------------------------------
class BindsTab : public MenuTab {
public:
    BindsTab() : mSel(0) {}

    const char *title() const override { return "Binds"; }

    bool grabsInput() const override { return gBinds.recording(); }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (gBinds.recording()) {
            const u32 rapid = pad->mButtons.mRapidInput;
            // Only the C-stick is safe to react to here: every real button is
            // a candidate for the combo being recorded.
            if (rapid & (TMarioGamePad::CSTICK_LEFT | TMarioGamePad::CSTICK_RIGHT |
                         TMarioGamePad::CSTICK_UP | TMarioGamePad::CSTICK_DOWN)) {
                gBinds.cancelRecord();
            }
            return;
        }

        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, BIND_COUNT);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, BIND_COUNT);
        }
        if (rapid & TMarioGamePad::A) {
            gBinds.beginRecord((BindId)mSel);
        } else if (rapid & TMarioGamePad::B) {
            gBinds.set((BindId)mSel, 0);  // clear
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        // Last line of the content area is this tab's own hint: its controls
        // differ enough from the rest of the menu to be worth spelling out.
        const int hintY = y + h - FOOT_SZ;
        h -= ROW_H;

        int maxRows = h / ROW_H;
        int start   = listScrollStart(mSel, BIND_COUNT, maxRows);
        int end     = start + maxRows;
        if (end > BIND_COUNT) {
            end = BIND_COUNT;
        }

        char text[kBindTextMax];
        int  ry = y;
        for (int i = start; i < end; i++) {
            BindId id       = (BindId)i;
            bool   selected = (i == mSel);
            if (selected) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
            }
            menu->drawText(Binds::name(id), x + 4, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());

            const char *val;
            Color       valCol = cValue();
            if (gBinds.recording() && gBinds.recordTarget() == id) {
                if (gBinds.recordPreview() != 0) {
                    Binds::format(gBinds.recordPreview(), text);
                    val = text;
                } else {
                    val = "press buttons...";
                }
                valCol = cAccent();
            } else {
                Binds::format(gBinds.get(id), text);
                val = text;
            }
            int vx = x + w - Menu::textWidth(val, ROW_SZ) - 8;
            menu->drawText(val, vx, ry, ROW_SZ, ROW_SZ, valCol);
            ry += ROW_H;
        }

        drawScrollHints(menu, x, y, w, h, start, end, BIND_COUNT);

        menu->drawText(gBinds.recording()
                           ? "Release a button to set, or " SUSAMUNE_GLYPH_C " to cancel"
                           : SUSAMUNE_GLYPH_A " Set bind    "
                             SUSAMUNE_GLYPH_B " Clear",
                       x + 4, hintY, FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    int mSel;
};

// =====================================================================
// Menu
// =====================================================================

// Static tab instances (constructed via placement new in Menu::Menu so their
// vtables are set without relying on C++ static-init, which the injected mod
// does not run).
namespace {
#if ENABLE_DEBUG_WARPS
u8 sPresetsBuf[sizeof(WarpPresetsTab)] __attribute__((aligned(8)));
u8 sStagesBuf[sizeof(WarpStagesTab)]   __attribute__((aligned(8)));
#endif
u8 sQolBuf[sizeof(CategorySettingsTab)]        __attribute__((aligned(8)));
u8 sCosmeticBuf[sizeof(CategorySettingsTab)]   __attribute__((aligned(8)));
u8 sMiscBuf[sizeof(CategorySettingsTab)]       __attribute__((aligned(8)));
u8 sSavestateBuf[sizeof(CategorySettingsTab)]  __attribute__((aligned(8)));
u8 sUiBuf[sizeof(CategorySettingsTab)]  __attribute__((aligned(8)));
u8 sTimerBuf[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
u8 sCreationBuf[sizeof(CreationTab)]             __attribute__((aligned(8)));
u8 sILingBuf[sizeof(ILingTab)]                   __attribute__((aligned(8)));
u8 sBindsBuf[sizeof(BindsTab)]                 __attribute__((aligned(8)));
}  // namespace

Menu::Menu() : mText(gpSystemFont->mFont, " ") {
    mOrtho       = nullptr;
    mShown       = false;
    mCurTab      = 0;
    mNumTabs     = 0;
    mTabFirst    = 0;
    mToastBuf[0] = '\0';
    mToastFrames = 0;
    mSaveWatch   = false;
    mCRepeatFrames = 0;

    // Cache font metrics. J2DTextBox::draw(x, y) places the text *baseline* at
    // y (glyphs render upward from it: top = y - ascent*size/height). drawText
    // converts a top-anchored y into that baseline so text lines up with the
    // fills drawn behind it.
    mFontAscent = mText.mFont->getAscent();
    mFontHeight = mText.mFont->getHeight();
    if (mFontHeight <= 0) {
        mFontHeight = 1;
    }

    // Advance metrics for the static textWidth(). mFont->_4/_8 are JUTFont's
    // fixed-advance override: when set, every glyph advances by _8 regardless
    // of its width entry.
    sFont        = mText.mFont;
    sFontWidth   = sFont->getWidth();
    if (sFontWidth <= 0) {
        sFontWidth = 1;
    }
    sCharSpacing = (int)mText.mCharSpacing;
    sFontFixed   = (sFont->_4 != 0);
    sFontFixedW  = (int)sFont->_8;

    // The ctor above allocated a small string buffer on the current heap via
    // setString(); free it and never let mText reallocate again. From here on
    // drawText() only assigns mStrPtr to borrowed const strings.
    if (mText.mStrPtr) {
        delete[] mText.mStrPtr;
        mText.mStrPtr = nullptr;
    }

#if ENABLE_DEBUG_WARPS
    mTabs[mNumTabs++] = new (sPresetsBuf) WarpPresetsTab();
    mTabs[mNumTabs++] = new (sStagesBuf) WarpStagesTab();
#endif
    mTabs[mNumTabs++] = new (sQolBuf) CategorySettingsTab(TITLE_QOL, SETTING_CAT_QOL);
    mTabs[mNumTabs++] =
        new (sCosmeticBuf) CategorySettingsTab(TITLE_COSMETIC, SETTING_CAT_COSMETIC);
    mTabs[mNumTabs++] = new (sMiscBuf) CategorySettingsTab(TITLE_MISC, SETTING_CAT_MISC);
    mTabs[mNumTabs++] =
        new (sSavestateBuf) CategorySettingsTab(TITLE_SAVESTATE, SETTING_CAT_SAVESTATE);
    mTabs[mNumTabs++] = new (sUiBuf) CategorySettingsTab(TITLE_UI, SETTING_CAT_UI);
    mTabs[mNumTabs++] = new (sTimerBuf) CategorySettingsTab(TITLE_TIMER, SETTING_CAT_TIMER);
    mTabs[mNumTabs++] = new (sCreationBuf) CreationTab();
    mTabs[mNumTabs++] = new (sILingBuf) ILingTab();
    mTabs[mNumTabs++] = new (sBindsBuf) BindsTab();
}

int Menu::textWidth(const char *s, int sizeX) {
    if (!sFont) {
        return 0;
    }

    // Mirror J2DPrint::parse, which is what J2DTextBox::draw runs through. Per
    // glyph the pen advances by TWidth::mWidth (both width bytes are read
    // unsigned there), and only the first glyph of the line also pays its
    // mMargin left bearing -- later glyphs are drawn back-shifted by their own
    // margin instead. The font is proportional, so anything that assumes a
    // fixed advance drifts and right-aligned text stops lining up.
    int units = 0;  // total advance in font design units
    int count = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p;) {
        int code = *p;
        if (sFont->isLeadByte(code) && p[1]) {
            code = (code << 8) | p[1];
            p += 2;
        } else {
            p += 1;
        }
        if (sFontFixed) {
            units += sFontFixedW;
        } else {
            JUTFont::TWidth tw;
            sFont->getWidthEntry(code, &tw);
            units += (u8)tw.mWidth;
            if (count == 0) {
                units += (u8)tw.mMargin;
            }
        }
        count++;
    }

    int w = units * sizeX / sFontWidth;
    if (count > 1) {
        w += sCharSpacing * (count - 1);
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

namespace {

__attribute__((noinline)) void drawPolygon(J2DOrthoGraph *ortho,
                                           const s16 *xy, int n, bool closed,
                                           const Color &color) {
    // setup2D() reasserts the flat vertex state and the position matrix, the
    // same preconditions J2DGrafContext::fillBox relies on.
    if (!ortho || (closed && n < 2)) {
        return;
    }
    // Vertices go to the write-gather pipe by address: GX.h reaches it through
    // a `wgPipe` object that none of the game maps export.
    volatile u16 *const wgU16 = (volatile u16 *)0xCC008000;
    volatile u32 *const wgU32 = (volatile u32 *)0xCC008000;

    ortho->setup2D();
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_SET);
    if (closed) {
        GXSetLineWidth(20, GX_TO_ZERO);
    }
    GXBegin(closed ? GX_LINESTRIP : GX_TRIANGLEFAN, GX_VTXFMT0,
            (u16)(n + closed));
    const u32 packed = ((u32)color.r << 24) | ((u32)color.g << 16) |
                       ((u32)color.b << 8) | (u32)color.a;
    for (int i = 0; i < n + closed; i++) {
        const int j = (i == n) ? 0 : i;
        *wgU16 = (u16)xy[j * 2];
        *wgU16 = (u16)xy[j * 2 + 1];
        *wgU16 = 0;
        *wgU32 = packed;
    }
}

}  // namespace

void Menu::fillPoly(const s16 *xy, int n, Color color) {
    drawPolygon(mOrtho, xy, n, false, color);
}

void Menu::strokePoly(const s16 *xy, int n, Color color) {
    drawPolygon(mOrtho, xy, n, true, color);
}

void Menu::switchTab(int dir) {
    mCurTab = wrap(mCurTab + dir, mNumTabs);
    mCRepeatFrames = 0;
}

u32 Menu::navigationInput(TMarioGamePad *pad) {
    u32 rapid = pad->mButtons.mRapidInput;
    const u32 vertical = pad->mButtons.mInput &
        (TMarioGamePad::CSTICK_UP | TMarioGamePad::CSTICK_DOWN);
    if (!vertical || (rapid & vertical)) {
        mCRepeatFrames = 0;
    } else if (++mCRepeatFrames >= 18) {
        rapid |= vertical;
        mCRepeatFrames = 14;
    }
    return rapid;
}

// =====================================================================
// Toast + settings auto-save
// =====================================================================

void Menu::toast(const char *msg) {
    strncpy(mToastBuf, msg, sizeof(mToastBuf));
    mToastFrames = kToastFrames;
}

void Menu::drawToast() {
    if (mToastFrames <= 0 || mToastBuf[0] == '\0') {
        return;
    }

    const int sz   = 16;
    const int padX = 10;
    const int padY = 6;
    const int x    = 20;
    const int y    = 412;
    const int w    = textWidth(mToastBuf, sz) + padX * 2;
    const int h    = sz + padY * 2;

    // Background first: the text is drawn over gameplay with the menu closed,
    // so without a panel behind it it is unreadable on a bright stage.
    fillBox(x, y, w, h, col(0, 0, 0, 200));
    fillBox(x, y, 3, h, cAccent());  // accent edge, matching the menu panel
    drawText(mToastBuf, x + padX, y + padY, sz, sz, col(255, 255, 255, 255));
}

void Menu::requestSettingsSave() {
    gSettings.save();
    if (gSettings.saveState() == SETTINGS_SAVE_UNSUPPORTED) {
        const char *error = settingsStorageError(gSettings.lastError());
        if (error) {
            snprintf(mToastBuf, sizeof(mToastBuf), "Settings unavailable: %s", error);
        } else {
            snprintf(mToastBuf, sizeof(mToastBuf), "Settings unavailable (error %lu)",
                     gSettings.lastError());
        }
        mToastFrames = kToastFrames;
        return;
    }
    mSaveWatch = true;
    toast("Saving settings...");
}

void Menu::hide() {
    mShown = false;
    if (gSettings.dirty() || gBinds.dirty() || gInputDisplay.dirty() ||
        gMetadataDisplay.dirty() || gQftDisplay.dirty() ||
        gCreationExtras.dirty()) {
        requestSettingsSave();
    }
}

void Menu::pollSettingsSave() {
    if (!mSaveWatch) {
        return;
    }

    SettingsSaveState st = gSettings.pollSave();
    if (st == SETTINGS_SAVE_PENDING) {
        // Hold the "saving" message up rather than letting it time out.
        mToastFrames = kToastFrames;
        return;
    }

    mSaveWatch = false;
    switch (st) {
    case SETTINGS_SAVE_OK:
        toast("Settings saved");
        break;
    case SETTINGS_SAVE_ERROR: {
        const char *error = settingsStorageError(gSettings.lastError());
        if (error) {
            snprintf(mToastBuf, sizeof(mToastBuf), "Settings save failed: %s", error);
        } else {
            snprintf(mToastBuf, sizeof(mToastBuf), "Settings save failed (error %lu)",
                     gSettings.lastError());
        }
        mToastFrames = kToastFrames;
        break;
    }
    case SETTINGS_SAVE_TIMEOUT:
        toast("Settings save timed out");
        break;
    default:
        break;
    }
}

void Menu::drawTabStrip(int x, int y, int w) {
    const int stripX = x + TAB_CHEV;
    const int visW   = w - TAB_CHEV * 2;

    // The strip scrolls by whole tabs, not by pixels: mTabFirst is the leftmost
    // one drawn and it always sits flush at stripX. Scrolling by pixels instead
    // leaves a ragged part-tab-wide gap on the left, since a tab that does not
    // fit entirely is skipped.
    if (mTabFirst > mCurTab) {
        mTabFirst = mCurTab;
    }
    if (mTabFirst < 0 || mTabFirst >= mNumTabs) {
        mTabFirst = 0;
    }
    // Advance the window until the selected tab fits at its right end.
    while (mTabFirst < mCurTab) {
        int span = 0;
        for (int i = mTabFirst; i <= mCurTab; i++) {
            span += tabWidth(i) + (i > mTabFirst ? TAB_GAP : 0);
        }
        if (span <= visW) {
            break;
        }
        mTabFirst++;
    }

    int cursor = stripX;
    int last   = mTabFirst;
    for (int i = mTabFirst; i < mNumTabs; i++) {
        int tw = tabWidth(i);
        if (i > mTabFirst && cursor + tw > stripX + visW) {
            break;  // no room for this one; the rest are further right still
        }
        bool active = (i == mCurTab);
        if (active) {
            fillBox(cursor, y, tw, TAB_STRIP_H, cAccent());
        }
        drawText(mTabs[i]->title(), cursor + TAB_INNER, y + 6, TAB_SZ, TAB_SZ,
                 active ? cTabOnText() : cTabIdle());
        cursor += tw + TAB_GAP;
        last = i;
    }

    // Scroll chevrons when tabs overflow either edge.
    if (mTabFirst > 0) {
        drawText(SUSAMUNE_GLYPH_LEFT, x - 2, y + 6, TAB_SZ, TAB_SZ, cAccent());
    }
    if (last < mNumTabs - 1) {
        drawText(SUSAMUNE_GLYPH_RIGHT, x + w - TAB_CHEV + 2, y + 6,
                 TAB_SZ, TAB_SZ, cAccent());
    }
}

int Menu::tabWidth(int i) const {
    return textWidth(mTabs[i]->title(), TAB_SZ) + TAB_INNER * 2;
}

void Menu::update(TMarioGamePad *pad) {
    u32 rapid = pad->mButtons.mRapidInput;

    // Toast bookkeeping runs whether or not the menu is open -- the save it
    // reports is normally started by the menu closing.
    if (mToastFrames > 0) {
        mToastFrames--;
    }
    pollSettingsSave();

    // A tab recording a button combo owns the pad outright: the close combo and
    // the tab-switch buttons are all bindable, so nothing else may look at them.
    if (mShown && mTabs[mCurTab]->grabsInput()) {
        mCRepeatFrames = 0;
        mTabs[mCurTab]->update(this, pad);
        return;
    }

    if (gBinds.wasPressed(BIND_MENU_TOGGLE)) {
        mShown = !mShown;
        // Closing with edits pending writes them back to the SD card. Gated on
        // dirty() so merely opening and closing the menu never touches storage.
        if (!mShown && (gSettings.dirty() || gBinds.dirty() ||
                        gInputDisplay.dirty() || gMetadataDisplay.dirty() ||
                        gQftDisplay.dirty() || gCreationExtras.dirty())) {
            requestSettingsSave();
        }
        return;
    }
    if (!mShown) {
        mCRepeatFrames = 0;
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
        gInputDisplay.draw(this);
        gMetadataDisplay.draw(this);
        gQFTTimer.draw(this);
        gAttemptCounter.draw(this);
        gCreationExtras.draw(this);
        drawToast();  // still visible with the menu closed
        bgmStatsDraw(this);
        return;
    }

    if (mTabs[mCurTab]->fullScreen()) {
        mTabs[mCurTab]->draw(this, 0, 0, 640, 480);
        drawToast();
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

    // The close hint names the live menu bind rather than a fixed combo, since
    // it is user-configurable (and re-bindable to something unguessable).
    {
        const char *hint = SUSAMUNE_GLYPH_L "/" SUSAMUNE_GLYPH_R " Tabs    "
                           SUSAMUNE_GLYPH_C " Move    "
                           SUSAMUNE_GLYPH_A " Select    ";
        int hx = PANEL_X + PAD;
        drawText(hint, hx, FOOTER_Y, FOOT_SZ, FOOT_SZ, cFooter());
        hx += textWidth(hint, FOOT_SZ);

        char combo[kBindTextMax];
        Binds::format(gBinds.get(BIND_MENU_TOGGLE), combo);
        drawText(combo, hx, FOOTER_Y, FOOT_SZ, FOOT_SZ, cFooter());
        hx += textWidth(combo, FOOT_SZ);
        drawText(" Close", hx, FOOTER_Y, FOOT_SZ, FOOT_SZ, cFooter());
    }

    // Last, so it sits above the panel rather than under the backdrop.
    drawToast();
    bgmStatsDraw(this);
}

// =====================================================================
// Global instance
// =====================================================================

Menu *gMenu = nullptr;

namespace {
u8 sMenuBuf[sizeof(Menu)] __attribute__((aligned(8)));
}  // namespace

void menuInit() { gMenu = new (sMenuBuf) Menu(); }
