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
#include "susamune/mem2_map.h"
#include "susamune/binds.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/input_display.hxx"
#include "susamune/iling.hxx"
#include "susamune/metadata_display.hxx"
#include "susamune/packed_text.hxx"
#include "susamune/attempt_counter.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/qft_display.hxx"
#include "susamune/records.hxx"
#include "susamune/records_persistence.hxx"
#include "susamune/settings.hxx"
#include "susamune/susamune_cfg.h"
#include "susamune/wallkick_display.hxx"
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
#include "SMS/MSound/MSound.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
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
#if !IS_EMULATOR
    static const char kErrors[] =
        "storage I/O error\0storage internal error\0storage not ready\0"
        "settings file missing\0settings path missing\0invalid settings filename\0"
        "storage access denied\0settings file already exists\0invalid settings file\0"
        "storage is write-protected\0invalid storage device\0storage not mounted\0"
        "storage has no FAT filesystem\0format aborted\0storage timed out\0"
        "settings file locked\0not enough memory\0too many open files\0"
        "invalid storage parameter";
    return error >= 1 && error <= 19
               ? PackedText::at(kErrors, error - 1)
               : nullptr;
#else
    switch (error) {
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
    default:  return nullptr;
    }
#endif
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

const int TAB_GAP   = 12;  // space between tabs
const int TAB_INNER = 10;  // highlight padding around a tab's text
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
    virtual bool favoriteHint() const { return false; }

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

static void drawValueRow(Menu *menu, int x, int y, int w, const char *name,
                         const char *value, bool selected, bool starred,
                         bool arrow);

// ---------------------------------------------------------------------
// IL and travel practice
// ---------------------------------------------------------------------
class ILingTab : public MenuTab {
public:
    ILingTab()
        : mSel(0), mConfirmDelete(false), mEditingName(false),
          mNameCursor(0), mNamePage(0), mNameLength(0), mNameUpper(false) {
        mNameBuffer[0] = '\0';
    }

    const char *title() const override { return "ILs"; }
    bool grabsInput() const override {
        return mConfirmDelete || mEditingName || gCreationExtras.editing();
    }
    bool fullScreen() const override {
        return mEditingName || gCreationExtras.editing();
    }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (gCreationExtras.editing()) {
            gCreationExtras.updateEditor(pad);
            return;
        }
        if (mEditingName) {
            updateNameEditor(pad);
            return;
        }
        if (mConfirmDelete) {
            const u32 rapid = pad->mButtons.mRapidInput;
            if (rapid & TMarioGamePad::A) {
                ILing::clearPB(selectedEntry());
                mConfirmDelete = false;
                menu->toast("PB deleted");
            } else if (rapid & TMarioGamePad::B) {
                mConfirmDelete = false;
            }
            return;
        }

        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, OPTION_COUNT + ILing::count());
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, OPTION_COUNT + ILing::count());
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            if (isOption()) {
                jumpFromOptions(-1);
            } else {
                jumpSection(-1);
            }
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            if (isOption()) {
                jumpFromOptions(+1);
            } else {
                jumpSection(+1);
            }
        }
        if (isOption() && mSel >= 2 && mSel <= 6 &&
            (rapid & TMarioGamePad::X)) {
            const SettingId id = optionSetting(mSel);
            gSettings.toggleFavorite(id);
            menu->toast(gSettings.favorite(id)
                            ? "Added to Shined"
                            : "Removed from Shined");
            return;
        }
        if (rapid & TMarioGamePad::A) {
            if (isOption()) {
                activateOption(menu, +1);
            } else if (ILing::start(selectedEntry())) {
                menu->hide();
            } else {
                menu->toast("Warps disabled");
            }
        } else if (!isOption() && (rapid & TMarioGamePad::X) &&
                   ILing::pbQf(selectedEntry()) >= 0) {
            mConfirmDelete = true;
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (gCreationExtras.editing()) {
            ILing::drawRecentPreview(menu);
            gCreationExtras.drawEditor(menu);
            return;
        }
        if (mEditingName) {
            drawNameEditor(menu);
            return;
        }
        if (mConfirmDelete) {
            const char *question = "Delete PB?";
            const char *entry = ILing::label(selectedEntry());
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
        const int start = listScrollStart(menuRowForSelection(mSel), rows, maxRows);
        int end = start + maxRows;
        if (end > rows) {
            end = rows;
        }

        int ry = y;
        int row = 0;

        if (row >= start && row < end) {
            drawSectionHeader(menu, x, ry, w, "PB PROFILE");
            ry += ROW_H;
        }
        row++;

        for (int i = 0; i < 2 && row < end; i++, row++) {
            if (row < start) {
                continue;
            }

            const bool selected = i == mSel;
            const char *value = optionValue(i);
            drawValueRow(menu, x, ry, w, optionName(i), value, selected,
                         false, true);
            ry += ROW_H;
        }

        if (ILing::pbProfile() == 0) {
            if (row >= start && row < end) {
                char theory[24];
                const char *value = "Incomplete";
                s32 qf;
                if (ILing::anyPercentTheoryQf(&qf)) {
                    const s32 millis = (qf * 1001) / 120;
                    snprintf(theory, sizeof(theory), "%d:%02d:%02d.%03d",
                             (int)(millis / 3600000),
                             (int)((millis / 60000) % 60),
                             (int)((millis / 1000) % 60),
                             (int)(millis % 1000));
                    value = theory;
                }
                drawValueRow(menu, x, ry, w, "Theoretical best", value,
                             false, false, true);
                ry += ROW_H;
            }
            row++;
        }

        if (row >= start && row < end) {
            drawSectionHeader(menu, x, ry, w, "PB OPTIONS");
            ry += ROW_H;
        }
        row++;

        for (int i = 2; i < OPTION_COUNT && row < end; i++, row++) {
            if (row < start) continue;
            const bool selected = i == mSel;
            const bool starred = i <= 6 &&
                gSettings.favorite(optionSetting(i));
            const char *value = optionValue(i);
            drawValueRow(menu, x, ry, w, optionName(i), value, selected,
                         starred, true);
            ry += ROW_H;
        }

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

            const bool selected = !isOption() && i == selectedEntry();
            char pb[24];
            const char *value = "(PB: --)";
            const s32 qf = ILing::pbQf(i);
            if (qf >= 0) {
                    ILing::formatTime(qf, pb, sizeof(pb),
                                  "(PB: %d:%02d.%03d)");
                value = pb;
            }
            drawValueRow(menu, x, ry, w, ILing::label(i), value, selected,
                         false, true);
            ry += ROW_H;
            row++;
        }

        drawScrollHints(menu, x, y, w, listH, start, end, rows);
        const char *hint = isOption()
            ? SUSAMUNE_GLYPH_A " Toggle" SUSAMUNE_GLYPH_SLASH "Edit  "
              SUSAMUNE_GLYPH_X " Shine  " SUSAMUNE_GLYPH_C
              " U" SUSAMUNE_GLYPH_SLASH "D Select L"
              SUSAMUNE_GLYPH_SLASH "R Section"
            : SUSAMUNE_GLYPH_A " Start  " SUSAMUNE_GLYPH_X " Delete  "
              SUSAMUNE_GLYPH_C " U" SUSAMUNE_GLYPH_SLASH "D Select L"
              SUSAMUNE_GLYPH_SLASH "R Section";
        menu->drawText(hint, x + 4, y + h - FOOT_SZ,
                       FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    enum { OPTION_COUNT = 8 };

    bool isOption() const { return mSel < OPTION_COUNT; }
    int selectedEntry() const { return mSel - OPTION_COUNT; }

    static SettingId optionSetting(int option) {
        static const u8 kSettings[5] = {
            SETTING_ILING_RECORDING,
            SETTING_ILING_POPUP,
            SETTING_ILING_FANFARE,
            SETTING_ILING_RECENT,
            SETTING_ILING_SHORT_NAMES,
        };
        return (SettingId)kSettings[option - 2];
    }

    static const char *optionName(int option) {
        static const char kNames[] =
            "PB profile\0Profile name\0Record IL PBs\0PB popup\0PB fanfare\0"
            "Recent IL history\0Recent IL names\0Recent IL display";
        return PackedText::at(kNames, option);
    }

    static const char *optionValue(int option) {
        if (option == 0) return ILing::pbProfileName(ILing::pbProfile());
        if (option == 1) {
            return ILing::pbProfileNameEditable(ILing::pbProfile())
                       ? "Edit"
                       : "Fixed";
        }
        if (option == OPTION_COUNT - 1) {
            return "Edit";
        }
        if (option == OPTION_COUNT - 2) {
            return gSettings.getBool(SETTING_ILING_SHORT_NAMES)
                       ? "Short"
                       : "Long";
        }
        if (option == 2 && gSettings.getBool(SETTING_STAGE_INTRO_SKIP)) {
            return "Off (Intro Skip)";
        }
        return gSettings.valueLabel(optionSetting(option));
    }

    void activateOption(Menu *menu, int direction) {
        if (mSel == 0) {
            ILing::cyclePbProfile(direction);
            menu->toast(ILing::pbProfileName(ILing::pbProfile()));
        } else if (mSel == 1) {
            if (ILing::pbProfileNameEditable(ILing::pbProfile())) {
                beginNameEditor();
            } else {
                menu->toast("This profile name is fixed");
            }
        } else
        if (mSel == OPTION_COUNT - 1) {
            gCreationExtras.beginRecentIlEditor();
        } else {
            gSettings.cycle(optionSetting(mSel), direction);
        }
    }

    void jumpSection(int direction) {
        const int entry = selectedEntry();
        int groupFirst = entry;
        while (groupFirst > 0 && !ILing::beginsGroup(groupFirst)) groupFirst--;
        const int destination = ILing::jumpGroup(entry, direction);
        if ((direction < 0 && groupFirst == 0) ||
            (direction > 0 && destination == 0)) {
            mSel = 0;
            return;
        }
        mSel = OPTION_COUNT + destination;
    }

    void jumpFromOptions(int direction) {
        const int first = direction > 0 ? 0 : ILing::jumpGroup(0, -1);
        mSel = OPTION_COUNT + first;
    }

    static int menuRowForEntry(int entry) {
        int row = entry;
        for (int i = 0; i <= entry; i++) {
            if (ILing::beginsGroup(i)) row++;
        }
        return row;
    }

    static int menuRowCount(int entries) {
        return 2 + OPTION_COUNT + theoryRows() +
               menuRowForEntry(entries - 1) + 1;
    }

    static int menuRowForSelection(int selection) {
        if (selection < 2) return 1 + selection;
        if (selection < OPTION_COUNT) return 2 + theoryRows() + selection;
        return 2 + OPTION_COUNT + theoryRows() +
               menuRowForEntry(selection - OPTION_COUNT);
    }

    static int theoryRows() { return ILing::pbProfile() == 0 ? 1 : 0; }

    void beginNameEditor() {
        const char *name = ILing::pbProfileName(ILing::pbProfile());
        mNameLength = 0;
        while (mNameLength + 1 < SUSAMUNE_ILING_PROFILE_NAME_SIZE &&
               name[mNameLength]) {
            mNameBuffer[mNameLength] = name[mNameLength];
            mNameLength++;
        }
        mNameBuffer[mNameLength] = '\0';
        mNameCursor = 0;
        mNamePage = 0;
        mNameUpper = false;
        mEditingName = true;
    }

    void updateNameEditor(TMarioGamePad *pad) {
        const u32 pressed = pad->mButtons.mRapidInput;
        if (pressed & TMarioGamePad::START) {
            if (!(pad->mButtons.mInput & TMarioGamePad::X)) {
                ILing::setPbProfileName(ILing::pbProfile(), mNameBuffer);
            }
            mEditingName = false;
            return;
        }
        if (pressed & TMarioGamePad::Z) {
            mNameLength = 0;
            mNameBuffer[0] = '\0';
            return;
        }
        updateCreationKeyboardText(pad, mNameBuffer, mNameLength,
                                   SUSAMUNE_ILING_PROFILE_NAME_SIZE - 1,
                                   mNamePage, mNameUpper, mNameCursor);
    }

    void drawNameEditor(Menu *menu) const {
        drawCreationKeyboard(menu, "Name PB profile",
                             mNameBuffer[0] ? mNameBuffer : "(Custom profile)",
                             mNamePage, mNameUpper, mNameCursor);
    }

    int mSel;
    bool mConfirmDelete;
    bool mEditingName;
    u8 mNameCursor;
    u8 mNamePage;
    u8 mNameLength;
    bool mNameUpper;
    char mNameBuffer[SUSAMUNE_ILING_PROFILE_NAME_SIZE];
};

// ---------------------------------------------------------------------
// Records -- nested achievement details and regional/global statistics.
// ---------------------------------------------------------------------
namespace {

const char *recordTierName(Records::Tier tier) {
    const char *name = Records::tierName(tier);
    return name ? name : "";
}

Color recordTierColor(Records::Tier tier) {
    switch (tier) {
    case Records::TIER_BRONZE:   return col(205, 127, 50, 255);
    case Records::TIER_SILVER:   return col(205, 215, 225, 255);
    case Records::TIER_GOLD:     return col(255, 196, 40, 255);
    case Records::TIER_DIAMOND:  return col(60, 160, 255, 255);
    case Records::TIER_DEMON:    return col(186, 65, 230, 255);
    case Records::TIER_FRONTIER: return col(60, 210, 100, 255);
    default:                     return cRow();
    }
}

const char *recordText(const char *text, const char *fallback = "") {
    return text && text[0] ? text : fallback;
}

int fittedRecordTextSize(const char *text, int maxWidth, int largest,
                         int smallest) {
    text = recordText(text);
    for (int size = largest; size > smallest; size--) {
        if (Menu::textWidth(text, size) <= maxWidth) return size;
    }
    return smallest;
}

}  // namespace

class RecordsTab : public MenuTab {
public:
    RecordsTab()
        : mPage(PAGE_ROOT), mSel(0), mCategory(0), mAchievement(0), mWorld(0),
          mScope(RecordsPersistence::SCOPE_GLOBAL) {}

    const char *title() const override { return "Records"; }
    void update(Menu *menu, TMarioGamePad *pad) override {
        const u32 rapid = menu->navigationInput(pad);
        if ((rapid & TMarioGamePad::B) && mPage != PAGE_ROOT) {
            if (mPage == PAGE_ACHIEVEMENT_DETAIL) {
                mPage = PAGE_ACHIEVEMENTS;
            } else if (mPage == PAGE_ACHIEVEMENTS) {
                mPage = PAGE_ACHIEVEMENT_CATEGORIES;
            } else if (mPage == PAGE_WORLD_DETAIL) {
                mPage = PAGE_WORLDS;
            } else {
                mPage = PAGE_ROOT;
            }
            return;
        }

        switch (mPage) {
        case PAGE_ROOT:
            moveSelection(rapid, 4);
            if (mSel == 3 &&
                (rapid & (TMarioGamePad::CSTICK_LEFT |
                          TMarioGamePad::CSTICK_RIGHT | TMarioGamePad::A))) {
                const int direction =
                    (rapid & TMarioGamePad::CSTICK_LEFT) ? -1 : 1;
                mScope = (u8)wrap(mScope + direction,
                                  RecordsPersistence::SCOPE_COUNT);
            } else if (rapid & TMarioGamePad::A) {
                mPage = mSel == 0 ? PAGE_ACHIEVEMENT_CATEGORIES
                                  : mSel == 1 ? PAGE_STATS_OVERVIEW
                                              : PAGE_WORLDS;
                mSel = 0;
            }
            break;
        case PAGE_ACHIEVEMENT_CATEGORIES:
            if (rapid & TMarioGamePad::CSTICK_UP)
                mCategory = wrap(mCategory - 1, Records::CATEGORY_COUNT);
            else if (rapid & TMarioGamePad::CSTICK_DOWN)
                mCategory = wrap(mCategory + 1, Records::CATEGORY_COUNT);
            if (rapid & TMarioGamePad::A) {
                mAchievement = 0;
                mPage = PAGE_ACHIEVEMENTS;
            }
            break;
        case PAGE_ACHIEVEMENTS:
            {
            const int count = Records::categoryAchievementCount(
                (Records::Category)mCategory);
            if (count <= 0) {
                mAchievement = 0;
                break;
            }
            if (rapid & TMarioGamePad::CSTICK_UP)
                mAchievement = wrap(mAchievement - 1, count);
            else if (rapid & TMarioGamePad::CSTICK_DOWN)
                mAchievement = wrap(mAchievement + 1, count);
            if (rapid & TMarioGamePad::A)
                mPage = PAGE_ACHIEVEMENT_DETAIL;
            break;
            }
        case PAGE_ACHIEVEMENT_DETAIL:
            break;
        case PAGE_STATS_OVERVIEW:
            break;
        case PAGE_WORLDS:
            if (rapid & TMarioGamePad::CSTICK_UP)
                mWorld = wrap(mWorld - 1, Records::WORLD_COUNT);
            else if (rapid & TMarioGamePad::CSTICK_DOWN)
                mWorld = wrap(mWorld + 1, Records::WORLD_COUNT);
            if (rapid & TMarioGamePad::A)
                mPage = PAGE_WORLD_DETAIL;
            break;
        case PAGE_WORLD_DETAIL:
            break;
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        switch (mPage) {
        case PAGE_ROOT: drawRoot(menu, x, y, w, h); break;
        case PAGE_ACHIEVEMENT_CATEGORIES:
            drawAchievementCategories(menu, x, y, w, h);
            break;
        case PAGE_ACHIEVEMENTS: drawAchievements(menu, x, y, w, h); break;
        case PAGE_ACHIEVEMENT_DETAIL:
            drawAchievementDetail(menu, x, y, w, h);
            break;
        case PAGE_STATS_OVERVIEW: drawOverview(menu, x, y, w, h); break;
        case PAGE_WORLDS: drawWorlds(menu, x, y, w, h); break;
        case PAGE_WORLD_DETAIL: drawWorldDetail(menu, x, y, w, h); break;
        }
    }

private:
    enum Page : u8 {
        PAGE_ROOT,
        PAGE_ACHIEVEMENT_CATEGORIES,
        PAGE_ACHIEVEMENTS,
        PAGE_ACHIEVEMENT_DETAIL,
        PAGE_STATS_OVERVIEW,
        PAGE_WORLDS,
        PAGE_WORLD_DETAIL,
    };

    Records::AchievementId selectedAchievement() const {
        int selection = mAchievement;
        const Records::Category category = (Records::Category)mCategory;
        for (int tier = 0; tier < Records::TIER_COUNT; tier++) {
            const int count = Records::categoryTierAchievementCount(
                category, (Records::Tier)tier);
            if (selection < count) {
                return Records::categoryTierAchievement(
                    category, (Records::Tier)tier, selection);
            }
            selection -= count;
        }
        return Records::ACHIEVEMENT_INVALID;
    }

    static int categoryVisualRows(Records::Category category) {
        int rows = 0;
        for (int tier = 0; tier < Records::TIER_COUNT; tier++) {
            const int count = Records::categoryTierAchievementCount(
                category, (Records::Tier)tier);
            if (count > 0) rows += count + 1;
        }
        return rows;
    }

    static int achievementVisualRow(Records::Category category,
                                    int selection) {
        int row = 0;
        for (int tier = 0; tier < Records::TIER_COUNT; tier++) {
            const int count = Records::categoryTierAchievementCount(
                category, (Records::Tier)tier);
            if (count <= 0) continue;
            row++;
            if (selection < count) return row + selection;
            selection -= count;
            row += count;
        }
        return 0;
    }

    void moveSelection(u32 rapid, int count) {
        if (rapid & TMarioGamePad::CSTICK_UP)
            mSel = wrap(mSel - 1, count);
        else if (rapid & TMarioGamePad::CSTICK_DOWN)
            mSel = wrap(mSel + 1, count);
    }

    static void formatDuration(u32 seconds, char *out, u32 size) {
        snprintf(out, size, "%lu:%02lu", seconds / 3600,
                 (seconds / 60) % 60);
    }

    static u32 successRate(u32 finishes, u32 attempts) {
        if (!attempts) return 0;
        if (finishes >= attempts) return 100;
        return (finishes * 100u + attempts / 2) / attempts;
    }

    static u32 scopedWorldTime(RecordsPersistence::Scope scope,
                               Records::World world) {
        static const u8 stats[] = {
            Records::STAT_AREA_BIANCO_SECONDS,
            Records::STAT_AREA_RICCO_SECONDS,
            Records::STAT_AREA_GELATO_SECONDS,
            Records::STAT_AREA_PINNA_SECONDS,
            Records::STAT_AREA_SIRENA_SECONDS,
            Records::STAT_AREA_NOKI_SECONDS,
            Records::STAT_AREA_PIANTA_SECONDS,
        };
        if (world == Records::WORLD_DELFINO) {
            return RecordsPersistence::stat(
                       scope, Records::STAT_AREA_AIRSTRIP_SECONDS) +
                   RecordsPersistence::stat(
                       scope, Records::STAT_AREA_DELFINO_SECONDS) +
                   RecordsPersistence::stat(
                       scope, Records::STAT_AREA_CORONA_SECONDS);
        }
        return world < Records::WORLD_DELFINO
                   ? RecordsPersistence::stat(
                         scope, (Records::StatId)stats[world])
                   : 0;
    }

    static const char *storageStatus() {
#if IS_EMULATOR
        return "Session only (Dolphin alpha)";
#else
        if (!RecordsPersistence::persistent()) return "Progress unavailable";
        if (!RecordsPersistence::writable()) return "Progress is read-only";
        if (RecordsPersistence::lastError() != 0) return "Progress save error";
        if (RecordsPersistence::pending()) return "Saving progress...";
        if (RecordsPersistence::dirty()) return "Progress not checkpointed";
        return "Progress saved";
#endif
    }

    void drawRoot(Menu *menu, int x, int y, int w, int h) const {
        char unlocked[20];
        snprintf(unlocked, sizeof(unlocked), "%d " SUSAMUNE_GLYPH_SLASH
                 " %d", Records::unlockedCount(), Records::achievementCount());
        drawValueRow(menu, x, y, w, "Achievements  >", unlocked,
                     mSel == 0, false, true);
        drawValueRow(menu, x, y + ROW_H, w, "Statistics overview  >", nullptr,
                     mSel == 1, false, true);
        drawValueRow(menu, x, y + ROW_H * 2, w, "Worlds  >", nullptr,
                     mSel == 2, false, true);
        drawValueRow(menu, x, y + ROW_H * 3, w, "Region",
                     RecordsPersistence::scopeName(
                         (RecordsPersistence::Scope)mScope),
                     mSel == 3, false, true);
        menu->drawText("V1.2.0 - Trial By Sunshine",
                       x + 4, y + h - 44, FOOT_SZ, FOOT_SZ, cRowDim());
        menu->drawText(storageStatus(), x + 4, y + h - 24,
                       FOOT_SZ, FOOT_SZ,
                       RecordsPersistence::lastError() ?
                           col(255, 130, 100, 255) : cFooter());
    }

    void drawAchievementCategories(Menu *menu, int x, int y, int w,
                                   int h) const {
        int ry = y;
        for (int i = 0; i < Records::CATEGORY_COUNT; i++, ry += ROW_H) {
            const Records::Category category = (Records::Category)i;
            char count[20];
            snprintf(count, sizeof(count), "%d " SUSAMUNE_GLYPH_SLASH " %d",
                     Records::categoryUnlockedCount(category),
                     Records::categoryAchievementCount(category));
            drawValueRow(menu, x, ry, w,
                         recordText(Records::categoryName(category),
                                    "(unnamed)"),
                         count, i == mCategory, false, true);
        }
        menu->drawText(SUSAMUNE_GLYPH_A " Open    "
                       SUSAMUNE_GLYPH_B " Back",
                       x + 4, y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawAchievements(Menu *menu, int x, int y, int w, int h) {
        const Records::Category category = (Records::Category)mCategory;
        const int count = Records::categoryAchievementCount(category);
        drawSectionHeader(menu, x, y, w,
                          recordText(Records::categoryName(category),
                                     "Achievements"));
        if (count <= 0) {
            menu->drawText("(none)", x + 4, y + ROW_H,
                           ROW_SZ, ROW_SZ, cRowDim());
            menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                           y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
            return;
        }
        const int listY = y + ROW_H;
        const int listH = h - ROW_H - FOOT_SZ;
        const int maxRows = listH / ROW_H;
        const int rows = categoryVisualRows(category);
        const int selectedRow = achievementVisualRow(category, mAchievement);
        const int start = listScrollStart(selectedRow, rows, maxRows);
        int end = start + maxRows;
        if (end > rows) end = rows;
        int ry = listY;
        int row = 0;
        int selection = 0;
        for (int tier = 0; tier < Records::TIER_COUNT; tier++) {
            const Records::Tier recordTier = (Records::Tier)tier;
            const int tierCount = Records::categoryTierAchievementCount(
                category, recordTier);
            if (tierCount <= 0) continue;

            if (row >= start && row < end) {
                drawSectionHeader(menu, x, ry, w,
                                  recordTierName(recordTier));
                ry += ROW_H;
            }
            row++;
            for (int i = 0; i < tierCount; i++, row++, selection++) {
                if (row < start || row >= end) continue;
                const Records::AchievementId id =
                    Records::categoryTierAchievement(category, recordTier, i);
                const Records::AchievementDesc *desc =
                    id == Records::ACHIEVEMENT_INVALID
                        ? nullptr : Records::achievement(id);
                const bool isUnlocked = desc && Records::unlocked(id);
                char progress[12];
                const char *value = nullptr;
                const u16 streak = category == Records::CATEGORY_STREAKS
                    ? Records::streakProgress(id)
                    : 0;
                if (streak) {
                    const u32 goal = streak & 0xff;
                    snprintf(progress, sizeof(progress),
                             "%lu " SUSAMUNE_GLYPH_SLASH " %lu",
                             isUnlocked ? goal : (u32)(streak >> 8), goal);
                    value = progress;
                }
                drawValueRow(menu, x, ry, w,
                             desc ? recordText(desc->name, "(unnamed)")
                                  : "(unavailable)",
                             value, selection == mAchievement,
                             isUnlocked, true);
                ry += ROW_H;
            }
        }
        drawScrollHints(menu, x, listY, w, listH, start, end, rows);
        menu->drawText(SUSAMUNE_GLYPH_A " Details    "
                       SUSAMUNE_GLYPH_B " Back",
                       x + 4, y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawAchievementDetail(Menu *menu, int x, int y, int w, int h) const {
        const Records::AchievementId id = selectedAchievement();
        const Records::AchievementDesc *desc =
            id == Records::ACHIEVEMENT_INVALID ? nullptr
                                               : Records::achievement(id);
        if (!desc) return;
        const Color color = recordTierColor(desc->tier);
        const char *name = recordText(desc->name, "Unnamed achievement");
        const bool isUnlocked = Records::unlocked(id);
        menu->fillBox(x, y + 4, w, 3, color);
        const int nameSize = fittedRecordTextSize(name, w - 8, 22, 13);
        menu->drawText(name, x + 4, y + 20, nameSize, nameSize, cRowSel());
        menu->drawText(recordTierName(desc->tier), x + 4, y + 56,
                       ROW_SZ, ROW_SZ, color);
        const char *state = isUnlocked ? "UNLOCKED" : "LOCKED";
        menu->drawText(state,
                       x + w - Menu::textWidth(state, ROW_SZ) - 8,
                       y + 56, ROW_SZ, ROW_SZ,
                       isUnlocked ? cValue() : cRowDim());

        const char *description = recordText(desc->description,
                                             "Description unavailable.");
        const int descriptionSize = fittedRecordTextSize(
            description, w - 8, 14, 10);
        menu->drawText(description, x + 4, y + 88,
                       descriptionSize, descriptionSize, cRow());

        menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawOverview(Menu *menu, int x, int y, int w, int h) const {
        static const char names[] =
            "Time played\0Attempts\0ILs finished\0PBs earned\0Deaths\0"
            "Creation time";
        static const u8 offsets[] = {0, 12, 21, 34, 45, 52};
        static const u8 stats[] = {
            Records::STAT_PLAY_SECONDS, Records::STAT_ATTEMPTS,
            Records::STAT_IL_FINISHES, Records::STAT_PBS_EARNED,
            Records::STAT_DEATHS, Records::STAT_CREATION_SECONDS,
        };
        char value[24];
        const RecordsPersistence::Scope scope =
            (RecordsPersistence::Scope)mScope;
        drawSectionHeader(menu, x, y, w,
                          recordText(RecordsPersistence::scopeName(scope),
                                     "Statistics"));
        int ry = y + ROW_H;
        for (u32 i = 0; i < sizeof(stats); i++, ry += ROW_H) {
            const u32 amount = RecordsPersistence::stat(
                scope, (Records::StatId)stats[i]);
            if (stats[i] == Records::STAT_PLAY_SECONDS ||
                stats[i] == Records::STAT_CREATION_SECONDS) {
                formatDuration(amount, value, sizeof(value));
            } else if (stats[i] == Records::STAT_IL_FINISHES) {
                const u32 attempts = RecordsPersistence::stat(
                    scope, Records::STAT_ATTEMPTS);
                snprintf(value, sizeof(value),
                         "%lu (%lu pct)", amount,
                         successRate(amount, attempts));
            } else {
                snprintf(value, sizeof(value), "%lu", amount);
            }
            drawValueRow(menu, x, ry, w, names + offsets[i], value,
                         false, false, false);
        }

        char achievements[20];
        snprintf(achievements, sizeof(achievements),
                 "%d " SUSAMUNE_GLYPH_SLASH " %d", Records::unlockedCount(),
                 Records::achievementCount());
        drawValueRow(menu, x, ry, w, "Achievements", achievements,
                     false, false, false);
        menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawWorlds(Menu *menu, int x, int y, int w, int h) {
        const int maxRows = (h - ROW_H - FOOT_SZ) / ROW_H;
        const int start = listScrollStart(mWorld, Records::WORLD_COUNT,
                                          maxRows);
        int end = start + maxRows;
        if (end > Records::WORLD_COUNT) end = Records::WORLD_COUNT;
        const RecordsPersistence::Scope scope =
            (RecordsPersistence::Scope)mScope;
        drawSectionHeader(menu, x, y, w,
                          recordText(RecordsPersistence::scopeName(scope),
                                     "Worlds"));
        int ry = y + ROW_H;
        for (int i = start; i < end; i++, ry += ROW_H) {
            char duration[24];
            formatDuration(scopedWorldTime(scope, (Records::World)i),
                           duration, sizeof(duration));
            drawValueRow(menu, x, ry, w,
                         recordText(Records::worldName((Records::World)i),
                                    "(unknown)"),
                         duration, i == mWorld, false, true);
        }
        drawScrollHints(menu, x, y + ROW_H, w, h - ROW_H - FOOT_SZ,
                        start, end, Records::WORLD_COUNT);
        menu->drawText(SUSAMUNE_GLYPH_A " Open    "
                       SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void formatPBSummary(Records::World world, bool anyPercent,
                         char *out, u32 size) const {
        u8 coverage = 0;
        u8 goal = 0;
        s32 qf = 0;
        const bool complete = Records::worldPBSummary(
            world, !anyPercent, &coverage, &goal, &qf);
        if (!goal) {
            snprintf(out, size, "Unavailable");
            return;
        }

        if (!complete) {
            snprintf(out, size,
                     "Incomplete %lu " SUSAMUNE_GLYPH_SLASH " %lu",
                     (u32)coverage, (u32)goal);
            return;
        }

        char time[24];
        ILing::formatTime(qf, time, sizeof(time));
        snprintf(out, size, "%s (%lu " SUSAMUNE_GLYPH_SLASH " %lu)",
                 time, (u32)coverage, (u32)goal);
    }

    void drawWorldDetail(Menu *menu, int x, int y, int w, int h) const {
        const Records::World world = (Records::World)mWorld;
        const RecordsPersistence::Scope scope =
            (RecordsPersistence::Scope)mScope;
        char header[48];
        snprintf(header, sizeof(header), "%s - %s",
                 recordText(Records::worldName(world), "World"),
                 recordText(RecordsPersistence::scopeName(scope), "Region"));
        drawSectionHeader(menu, x, y, w, header);

        const u32 attempts = RecordsPersistence::stat(
            scope, Records::worldAttemptStat(world));
        const u32 finishes = RecordsPersistence::stat(
            scope, Records::worldFinishStat(world));
        char value[48];
        int ry = y + ROW_H;
        formatDuration(scopedWorldTime(scope, world), value, sizeof(value));
        drawValueRow(menu, x, ry, w, "Time", value, false, false, false);
        ry += ROW_H;
        snprintf(value, sizeof(value), "%lu", attempts);
        drawValueRow(menu, x, ry, w, "Attempts", value,
                     false, false, false);
        ry += ROW_H;
        snprintf(value, sizeof(value), "%lu", finishes);
        drawValueRow(menu, x, ry, w, "Finishes", value,
                     false, false, false);
        ry += ROW_H;
        snprintf(value, sizeof(value), "%lu pct",
                 successRate(finishes, attempts));
        drawValueRow(menu, x, ry, w, "Success rate", value,
                     false, false, false);
        ry += ROW_H;

        char pbHeader[64];
        snprintf(pbHeader, sizeof(pbHeader), "PBs: %s - %s",
                 recordText(Records::currentRegionScope(), "Region"),
                 recordText(ILing::pbProfileName(ILing::pbProfile()),
                            "Profile"));
        drawSectionHeader(menu, x, ry, w, pbHeader);
        ry += ROW_H;

        formatPBSummary(world, true, value, sizeof(value));
        drawValueRow(menu, x, ry, w, "Any percent PBs", value,
                     false, false, false);
        ry += ROW_H;
        formatPBSummary(world, false, value, sizeof(value));
        drawValueRow(menu, x, ry, w, "All-IL PBs", value,
                     false, false, false);

        menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    u8 mPage;
    int mSel;
    int mCategory;
    int mAchievement;
    int mWorld;
    u8 mScope;
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
    "Gameplay\0Savestate\0Practice\0Cosmetic\0Display\0Timer\0Shined";

enum CategoryTitleOffset {
    TITLE_QOL       = 0,
    TITLE_SAVESTATE = TITLE_QOL + sizeof("Gameplay"),
    TITLE_MISC      = TITLE_SAVESTATE + sizeof("Savestate"),
    TITLE_COSMETIC  = TITLE_MISC + sizeof("Practice"),
    TITLE_UI        = TITLE_COSMETIC + sizeof("Cosmetic"),
    TITLE_TIMER     = TITLE_UI + sizeof("Display"),
    TITLE_STARRED   = TITLE_TIMER + sizeof("Timer"),
};

static_assert(sizeof(kCategoryTitles) == 58, "category title offsets changed");
static_assert(SETTING_COUNT <= 0x100, "setting ids no longer fit in a byte");
static_assert(SETTING_CAT_COUNT <= 0x100, "setting categories no longer fit in a byte");
const u8 kStarredCategory = SETTING_CAT_COUNT;

const u8 kSettingSectionStarts[] = {
    SETTING_FAST_TEXT,
    SETTING_FMV_SKIPS,
    SETTING_FREE_PAUSE,
    SETTING_YOSHI_NOZZLE_SAVE_PROMPT,
    SETTING_NOZZLE_LOCK,
    SETTING_ATTEMPT_COUNTER,
    SETTING_FORCE_BOX_GAME,
    SETTING_MUTE_BGM,
    SETTING_VISIBLE_GOOP,
    SETTING_SHOW_BGM_SLOTS,
    SETTING_WALLKICK_DISPLAY,
    SETTING_TIMER_SUNSHINE_VISIBILITY,
    SETTING_TIMER_FREEZE_DURATION,
    SETTING_TIMER_SECTIONS,
    SETTING_SAVE_RNG_STATE,
    SETTING_SAVESTATE_FEEDBACK,
};
const char kSettingSectionNames[] =
    "GENERAL\0SKIPS & UNLOCKS\0WORLD RULES\0SAVE PROMPTS\0"
    "LEVEL RULES\0PRACTICE TOOLS\0BOX GAMES\0PRESENTATION\0WORLD\0"
    "DIAGNOSTICS\0PRACTICE FEEDBACK\0DISPLAY\0FREEZE EVENTS\0"
    "SECTION HISTORY\0STATE\0FEEDBACK";

}  // namespace

class CategorySettingsTab : public MenuTab {
public:
    CategorySettingsTab(u8 titleOffset, u8 cat)
        : mSel(0), mCat(cat), mTitleOffset(titleOffset), mResetConfirm(0) {}

    const char *title() const override { return kCategoryTitles + mTitleOffset; }
    bool favoriteHint() const override { return true; }
    bool grabsInput() const override {
        return mResetConfirm ||
               (hasVisualEditor() && gCreationExtras.editing());
    }
    bool fullScreen() const override { return grabsInput(); }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (mResetConfirm) {
            const u32 rapid = pad->mButtons.mRapidInput;
            if (rapid & TMarioGamePad::B) {
                mResetConfirm = 0;
            } else if (rapid & TMarioGamePad::A) {
                if (mResetConfirm & 1) mResetConfirm++;
                else {
                    const bool records = mResetConfirm == 4;
                    mResetConfirm = 0;
                    if (records) {
                        RecordsPersistence::resetAll();
                        menu->toast("Records reset");
                    } else {
                        menu->factoryReset();
                    }
                }
            }
            return;
        }
        if (hasVisualEditor() && gCreationExtras.editing()) {
            gCreationExtras.updateEditor(pad);
            return;
        }
        u8  ids[SETTING_COUNT];
        const int settings = buildList(ids);
        const int n = settings + extraRows();
        if (n == 0) {
            mSel = 0;
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
        if (rapid & TMarioGamePad::CSTICK_LEFT) {
            jumpSection(ids, settings, n, -1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            jumpSection(ids, settings, n, +1);
        }
        if (mSel >= settings) {
            if (rapid & TMarioGamePad::A) {
                if (hasFeedbackEditor())
                    gCreationExtras.beginSavestateFeedbackEditor();
                else if (hasWallkickEditor())
                    gCreationExtras.beginWallkickEditor();
                else if (hasFactoryReset())
                    mResetConfirm = mSel == settings ? 1 : 3;
            }
            return;
        }
        SettingId id = (SettingId)ids[mSel];
        if (rapid & TMarioGamePad::X) {
            gSettings.toggleFavorite(id);
            menu->toast(gSettings.favorite(id)
                            ? "Added to Shined"
                            : "Removed from Shined");
            if (isStarred() && mSel >= settings - 1 && mSel > 0) mSel--;
        } else if (rapid & TMarioGamePad::A) {
            gSettings.cycle(id, +1);
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (mResetConfirm) {
            const bool records = mResetConfirm >= 3;
            const bool first = mResetConfirm & 1;
            const char *title = records
                ? (first ? "Reset all Records progress?"
                         : "Reset Records now?")
                : (first ? "Reset settings, binds and layouts?"
                         : "Reset settings now?");
            const char *detail = records
                ? (first ? "PBs and settings stay."
                         : "PB achievements can return.")
                : (first ? "This requires one more confirmation."
                         : "PBs and Records stay.");
            const char *hint = first
                ? SUSAMUNE_GLYPH_A " Continue    " SUSAMUNE_GLYPH_B " Cancel"
                : SUSAMUNE_GLYPH_A " Reset    " SUSAMUNE_GLYPH_B " Cancel";
            menu->fillBox(88, 176, 464, 128, Color(8, 11, 20, 245));
            menu->fillBox(88, 176, 464, 3, cAccent());
            menu->drawText(title, 320 - Menu::textWidth(title, 15) / 2,
                           200, 15, 15, cRowSel());
            menu->drawText(detail, 320 - Menu::textWidth(detail, 12) / 2,
                           232, 12, 12, cRow());
            menu->drawText(hint, 320 - Menu::textWidth(hint, 12) / 2,
                           270, 12, 12, cFooter());
            return;
        }
        if (hasVisualEditor() && gCreationExtras.editing()) {
            gCreationExtras.drawEditor(menu);
            return;
        }
        u8  ids[SETTING_COUNT];
        const int settings = buildList(ids);
        const int n = settings + extraRows();
        if (n == 0) {
            menu->drawText(isStarred() ? "Nothing Shined yet" : "(none)",
                           x + 4, y, ROW_SZ, ROW_SZ, cRowDim());
            if (isStarred())
                menu->drawText("Press X on a setting to add it here.", x + 4,
                               y + ROW_H, FOOT_SZ, FOOT_SZ, cFooter());
            return;
        }
        const u32 metrics = displayMetrics(ids, settings, n);
        const int rows = metrics >> 16;
        const int maxRows = h / ROW_H;
        const int start = listScrollStart(
            (u16)metrics, rows, maxRows);
        int end = start + maxRows;
        if (end > rows) end = rows;
        int ry = y;
        int row = 0;
        for (int i = 0; i < n && row < end; i++) {
            const char *section = sectionName(ids, settings, i);
            if (section) {
                if (row >= start) {
                    drawSectionHeader(menu, x, ry, w, section);
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
            const char *name;
            const char *val;
            if (i < settings) {
                const SettingId id = (SettingId)ids[i];
                name = Settings::name(id);
                val = gSettings.valueLabel(id);
            } else {
                name = hasFeedbackEditor() ? "Feedback display"
                     : hasWallkickEditor() ? "Wallkick display style"
                     : i == settings ? "Factory reset"
                                     : "Reset Records";
                val = hasVisualEditor() ? "Edit" : "Reset";
            }
            const bool starred = i < settings &&
                gSettings.favorite((SettingId)ids[i]);
            drawValueRow(menu, x, ry, w, name, val, selected, starred, false);
            ry += ROW_H;
            row++;
        }
    
        drawScrollHints(menu, x, y, w, h, start, end, rows);
    }

private:
    bool isStarred() const { return mCat == kStarredCategory; }
    bool hasFeedbackEditor() const {
        return mCat == SETTING_CAT_SAVESTATE;
    }
    bool hasWallkickEditor() const { return mCat == SETTING_CAT_UI; }
    bool hasVisualEditor() const {
        return hasFeedbackEditor() || hasWallkickEditor();
    }
    bool hasFactoryReset() const { return mCat == SETTING_CAT_MISC; }
    int extraRows() const {
        return hasFactoryReset() ? 2 : hasVisualEditor() ? 1 : 0;
    }

    int buildList(u8 *out) const {
        int n = 0;
        const int count = isStarred() ? SETTING_FAVORITES_0 : SETTING_COUNT;
        for (int i = 0; i < count; i++) {
            const SettingId id = (SettingId)i;
            if (isStarred()
                    ? Settings::category(id) != SETTING_CAT_HIDDEN &&
                          gSettings.favorite(id)
                    : Settings::category(id) == (SettingCategory)mCat) {
                out[n++] = (u8)i;
            }
        }
        return n;
    }

    const char *sectionName(const u8 *ids, int settings, int logical) const {
        if (logical < settings) {
            if (isStarred()) return nullptr;
            const SettingId id = (SettingId)ids[logical];
            for (u32 i = 0; i < sizeof(kSettingSectionStarts); i++) {
                if (kSettingSectionStarts[i] == id)
                    return PackedText::at(kSettingSectionNames, i);
            }
            return nullptr;
        }
        if (hasFeedbackEditor()) return "FEEDBACK STYLE";
        if (hasWallkickEditor()) return "DISPLAY STYLE";
        if (hasFactoryReset()) return logical == settings ? "RESET" : nullptr;
        return nullptr;
    }

    __attribute__((always_inline))
    u32 displayMetrics(const u8 *ids, int settings, int logicalCount) const {
        int rows = logicalCount;
        int selectedRow = mSel;
        for (int i = 0; i < logicalCount; i++) {
            if (!sectionName(ids, settings, i)) continue;
            rows++;
            if (i <= mSel) selectedRow++;
        }
        return ((u32)rows << 16) | (u16)selectedRow;
    }

    void jumpSection(const u8 *ids, int settings, int n, int direction) {
        int row = mSel;
        for (int left = n; left; left--) {
            row = wrap(row + direction, n);
            if (sectionName(ids, settings, row)) {
                mSel = row;
                return;
            }
        }
    }

    u8 mSel;
    u8 mCat;
    u8 mTitleOffset;
    u8 mResetConfirm;
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
        if (rapid & TMarioGamePad::CSTICK_LEFT) {
            jumpSection(-1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            jumpSection(+1);
        } else if (rapid & TMarioGamePad::A) {
            activate(+1);
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
                drawValueRow(menu, x, ry, w, rowName(row), rowValue(row),
                             selected, false, false);
            }
            ry += ROW_H;
        }
        drawScrollHints(menu, x, y, w, h, start, end, count);
        menu->drawText(SUSAMUNE_GLYPH_A " Open" SUSAMUNE_GLYPH_SLASH
                       "Change   " SUSAMUNE_GLYPH_C " L"
                       SUSAMUNE_GLYPH_SLASH "R Section   Saved on close",
                       x + 4, hintY, FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    enum Row {
        ROW_QFT_HEADER,
        ROW_QFT_EDITOR,
        ROW_QFT_LEADING_ZERO,
        ROW_SUNSHINE_TIMER_CHARACTERS,
        ROW_SUNSHINE_TIMER_STREAK,
        ROW_SUNSHINE_TIMER_LABEL,
        ROW_SUNSHINE_TIMER_LABEL_VISIBLE,
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

    void jumpSection(int dir) {
        if (dir > 0) {
            for (int row = mSel + 1; row < ROW_COUNT; row++) {
                if (isSeparator(row)) {
                    mSel = (u8)row;
                    moveSelection(+1);
                    return;
                }
            }
            mSel = 0;
            moveSelection(+1);
            return;
        }

        int current = -1;
        for (int row = 0; row < mSel; row++) {
            if (isSeparator(row)) current = row;
        }
        if (current >= 0 && mSel > current + 1) {
            mSel = (u8)current;
            moveSelection(+1);
            return;
        }
        for (int row = current - 1; row >= 0; row--) {
            if (isSeparator(row)) {
                mSel = (u8)row;
                moveSelection(+1);
                return;
            }
        }
        for (int row = ROW_COUNT - 1; row >= 0; row--) {
            if (isSeparator(row)) {
                mSel = (u8)row;
                moveSelection(+1);
                return;
            }
        }
    }

    void activate(int dir) {
        if (mSel == ROW_QFT_EDITOR) {
            gQftDisplay.beginEditor();
        } else if (mSel == ROW_QFT_LEADING_ZERO) {
            gQftDisplay.toggleLeadingZero();
        } else if (mSel == ROW_SUNSHINE_TIMER_CHARACTERS) {
            gCreationExtras.beginTimerCharacterEditor();
        } else if (mSel == ROW_SUNSHINE_TIMER_STREAK) {
            gCreationExtras.beginColorEditor(SUSAMUNE_CREATION_TIMER_BG, 1,
                                             "Sunshine timer streak");
        } else if (mSel == ROW_SUNSHINE_TIMER_LABEL) {
            gCreationExtras.beginColorEditor(
                SUSAMUNE_CREATION_TIMER_LABEL, 1,
                "TIME" SUSAMUNE_GLYPH_SLASH "TEMPO label colour");
        } else if (mSel == ROW_SUNSHINE_TIMER_LABEL_VISIBLE) {
            gCreationExtras.toggleTimerLabel();
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
        if (row == ROW_SUNSHINE_TIMER_CHARACTERS)
            return "Sunshine timer characters";
        if (row == ROW_SUNSHINE_TIMER_STREAK)
            return "Sunshine timer streak";
        if (row == ROW_SUNSHINE_TIMER_LABEL)
            return "TIME" SUSAMUNE_GLYPH_SLASH "TEMPO label colour";
        if (row == ROW_SUNSHINE_TIMER_LABEL_VISIBLE)
            return "Show TIME" SUSAMUNE_GLYPH_SLASH "TEMPO label";
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
        if (row >= ROW_SUNSHINE_TIMER_CHARACTERS &&
            row <= ROW_SUNSHINE_TIMER_LABEL)
            return "Edit";
        if (row == ROW_SUNSHINE_TIMER_LABEL_VISIBLE)
            return gCreationExtras.timerLabelVisible() ? "On" : "Off";
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

namespace {

Records::AchievementId sAchievementBannerId = Records::ACHIEVEMENT_INVALID;
int sAchievementBannerFrames = 0;
bool sAchievementChimePending = false;
bool sAchievementBatchActive = false;

void updateAchievementChime() {
    if (!sAchievementChimePending) return;
    if (ILing::achievementChimeBlocked()) {
        // A PB already owns this celebration batch.
        sAchievementChimePending = false;
        return;
    }
    if (!gpMSound || gpApplication.mContext !=
                            TApplication::CONTEXT_DIRECT_STAGE ||
        !gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState < TMarDirector::STATE_GAME_STARTING ||
        gpMarDirector->mCurState == TMarDirector::STATE_STAGE_EXIT ||
        gpMarDirector->mCurState == TMarDirector::STATE_STAGE_EXIT_2) {
        return;
    }
    gpMSound->startSoundSystemSE(MSD_SE_SY_MENU_SHINE_LIGHT, 0, nullptr, 0);
    sAchievementChimePending = false;
}

void updateAchievementBanner() {
    if (sAchievementBannerFrames > 0) sAchievementBannerFrames--;
    if (sAchievementBannerFrames != 0) {
        updateAchievementChime();
        return;
    }

    Records::AchievementId id;
    if (Records::popUnlock(&id)) {
        sAchievementBannerId = id;
        sAchievementBannerFrames = 120;  // four seconds at Susamune's 30 fps
        if (!sAchievementBatchActive) {
            sAchievementBatchActive = true;
            sAchievementChimePending = true;
        }
        updateAchievementChime();
    } else {
        sAchievementBatchActive = false;
        sAchievementChimePending = false;
    }
}

void drawAchievementBanner(Menu *menu) {
    if (!menu || sAchievementBannerFrames == 0) return;
    const Records::AchievementDesc *desc =
        Records::achievement(sAchievementBannerId);
    if (!desc) return;

    const int labelSize = 12;
    const char *tier = recordTierName(desc->tier);
    const int w = 410;
    const int h = 58;
    const int x = (640 - w) / 2;
    const int y = 96;  // below ILing's simultaneous PB banner
    const int border = 4;
    const Color outline = recordTierColor(desc->tier);

    menu->fillBox(x, y, w, h, outline);
    menu->fillBox(x + border, y + border, w - border * 2, h - border * 2,
                  col(4, 6, 12, 225));
    menu->drawText("ACHIEVEMENT UNLOCKED", x + 14, y + 10,
                   labelSize, labelSize, cRowDim());
    menu->drawText(tier,
                   x + w - Menu::textWidth(tier, labelSize) - 14,
                   y + 10, labelSize, labelSize, outline);
    const char *name = recordText(desc->name, "Unnamed achievement");
    const int nameSize = fittedRecordTextSize(name, w - 28, 18, 12);
    menu->drawText(name, x + 14, y + 31,
                   nameSize, nameSize, cRowSel());
}

}  // namespace

// Static tab instances (constructed via placement new in Menu::Menu so their
// vtables are set without relying on C++ static-init, which the injected mod
// does not run).
namespace {
struct __attribute__((aligned(8))) MenuRuntime {
#if ENABLE_DEBUG_WARPS
    u8 presets[sizeof(WarpPresetsTab)] __attribute__((aligned(8)));
    u8 stages[sizeof(WarpStagesTab)] __attribute__((aligned(8)));
#endif
    u8 starred[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 qol[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 cosmetic[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 misc[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 savestate[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 ui[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 timer[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 creation[sizeof(CreationTab)] __attribute__((aligned(8)));
    u8 iling[sizeof(ILingTab)] __attribute__((aligned(8)));
    u8 records[sizeof(RecordsTab)] __attribute__((aligned(8)));
    u8 binds[sizeof(BindsTab)] __attribute__((aligned(8)));
    u8 menu[sizeof(Menu)] __attribute__((aligned(8)));
};

MenuRuntime &sMenuRuntime = *reinterpret_cast<MenuRuntime *>(
    SUSAMUNE_MEM2_MENU_RUNTIME_PPC_BASE);
static_assert(sizeof(MenuRuntime) <= SUSAMUNE_MENU_RUNTIME_SIZE,
              "menu state exceeds its MEM2 runtime window");

#if ENABLE_DEBUG_WARPS
#define sPresetsBuf sMenuRuntime.presets
#define sStagesBuf sMenuRuntime.stages
#endif
#define sStarredBuf sMenuRuntime.starred
#define sQolBuf sMenuRuntime.qol
#define sCosmeticBuf sMenuRuntime.cosmetic
#define sMiscBuf sMenuRuntime.misc
#define sSavestateBuf sMenuRuntime.savestate
#define sUiBuf sMenuRuntime.ui
#define sTimerBuf sMenuRuntime.timer
#define sCreationBuf sMenuRuntime.creation
#define sILingBuf sMenuRuntime.iling
#define sRecordsBuf sMenuRuntime.records
#define sBindsBuf sMenuRuntime.binds
#define sMenuBuf sMenuRuntime.menu
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
    mTabs[mNumTabs++] =
        new (sStarredBuf) CategorySettingsTab(TITLE_STARRED, kStarredCategory);
    mTabs[mNumTabs++] = new (sMiscBuf) CategorySettingsTab(TITLE_MISC, SETTING_CAT_MISC);
    mTabs[mNumTabs++] = new (sILingBuf) ILingTab();
    mTabs[mNumTabs++] = new (sTimerBuf) CategorySettingsTab(TITLE_TIMER, SETTING_CAT_TIMER);
    mTabs[mNumTabs++] =
        new (sSavestateBuf) CategorySettingsTab(TITLE_SAVESTATE, SETTING_CAT_SAVESTATE);
    mTabs[mNumTabs++] = new (sQolBuf) CategorySettingsTab(TITLE_QOL, SETTING_CAT_QOL);
    mTabs[mNumTabs++] = new (sUiBuf) CategorySettingsTab(TITLE_UI, SETTING_CAT_UI);
    mTabs[mNumTabs++] =
        new (sCosmeticBuf) CategorySettingsTab(TITLE_COSMETIC, SETTING_CAT_COSMETIC);
    mTabs[mNumTabs++] = new (sCreationBuf) CreationTab();
    mTabs[mNumTabs++] = new (sRecordsBuf) RecordsTab();
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

__attribute__((noinline)) static void drawValueRow(
    Menu *menu, int x, int y, int w, const char *name, const char *value,
    bool selected, bool starred, bool arrow) {
    if (selected) drawRowHighlight(menu, x, y, w, ROW_H);
    if (selected && arrow)
        menu->drawText(">", x - 2, y, ROW_SZ, ROW_SZ, cAccent());
    if (starred)
        menu->drawText(SUSAMUNE_GLYPH_SHINED, x + (arrow ? 8 : 4), y,
                       ROW_SZ, ROW_SZ, cAccent());
    menu->drawText(name, x + (arrow ? 12 : 4) +
                         (starred ? (arrow ? 12 : 16) : 0), y,
                   ROW_SZ, ROW_SZ, selected ? cRowSel() : cRow());
    if (value)
        menu->drawText(value, x + w - Menu::textWidth(value, ROW_SZ) - 8,
                       y, ROW_SZ, ROW_SZ, cValue());
}

void Menu::factoryReset() {
    gCreationExtras.restoreHudDefaults();
    gSettings.resetDefaults();
    requestSettingsSave();
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
    // Derive the window from the selected tab so navigation history and
    // regional font widths cannot leave different tabs visible.
    mTabFirst = 0;
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

    updateAchievementBanner();

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
        WallkickDisplay::draw(this);
        drawToast();  // still visible with the menu closed
        drawAchievementBanner(this);
        bgmStatsDraw(this);
        return;
    }

    if (mTabs[mCurTab]->fullScreen()) {
        mTabs[mCurTab]->draw(this, 0, 0, 640, 480);
        drawToast();
        drawAchievementBanner(this);
        return;
    }

    // Dim the whole frame, then the panel on top.
    fillBox(0, 0, 640, 480, cBackdrop());
    fillBox(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, cPanel());
    // Accent rule at the top edge of the panel.
    fillBox(PANEL_X, PANEL_Y, PANEL_W, 3, cAccent());

    // Title + accent underline.
    drawText("susamune", PANEL_X + PAD - 2, PANEL_Y + 12,
             TITLE_SZ, TITLE_SZ, cTitle());
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
        const char *hint = mTabs[mCurTab]->favoriteHint()
            ? SUSAMUNE_GLYPH_L SUSAMUNE_GLYPH_SLASH SUSAMUNE_GLYPH_R " Tabs  "
              SUSAMUNE_GLYPH_C " Move  " SUSAMUNE_GLYPH_A " Select  "
              SUSAMUNE_GLYPH_X " Shine  "
            : SUSAMUNE_GLYPH_L SUSAMUNE_GLYPH_SLASH SUSAMUNE_GLYPH_R " Tabs    "
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
    drawAchievementBanner(this);
    bgmStatsDraw(this);
}

// =====================================================================
// Global instance
// =====================================================================

Menu *gMenu = nullptr;

void menuInit() { gMenu = new (sMenuBuf) Menu(); }
