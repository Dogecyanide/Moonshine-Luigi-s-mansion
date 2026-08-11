#ifndef _SUSAMUNE_SETTINGS_HXX
#define _SUSAMUNE_SETTINGS_HXX

#include <Dolphin/types.h>

#include "susamune/settings_list.h"

// =====================================================================
// settings.hxx
//
// The mod's persistent settings, kept deliberately independent of the
// menu that edits them (menu.cpp) and of any feature that reads them
// (e.g. savestate.cpp). A setting is one entry in a static descriptor
// table plus one byte of live value; everything else -- how the menu
// renders it, how a feature queries it, how it serialises to
// susamune.ini on the SD card -- is driven off that table so adding a
// setting is a one-line change.
//
// Persistence (console only) is a two-step handoff with the Nintendont
// kernel; see susamune_cfg.h for the protocol and settings.cpp for the
// implementation. Values live in this BSS struct during gameplay, not in
// the MEM2 handoff block, so every read is a plain cached MEM1 load with
// no coherency rules -- features.cpp reads settings every frame.
// =====================================================================

// The full set of settings. Append a row to SUSAMUNE_SETTING_LIST
// (settings_list.h) and the same ordinal to src/settings_descs.inc. The list
// order is the persisted values[] layout, so new IDs go only at the global
// tail; the descriptor category controls which menu tab displays each row.
#define SUSAMUNE_SETTING_ENUM(id, key) id,
enum SettingId {
    SUSAMUNE_SETTING_LIST(SUSAMUNE_SETTING_ENUM)

    SETTING_COUNT
};
#undef SUSAMUNE_SETTING_ENUM

// Which menu tab a setting appears under. CUSTOM is rendered by its owning
// feature tab; the others use generic category tabs.
enum SettingCategory {
    SETTING_CAT_QOL,
    SETTING_CAT_SAVESTATE,
    SETTING_CAT_MISC,
    SETTING_CAT_COSMETIC,
    SETTING_CAT_UI,
    SETTING_CAT_TIMER,
    SETTING_CAT_CUSTOM,
    SETTING_CAT_COUNT
};

// Progress of an in-flight save, for the on-screen toast. The write itself is
// performed asynchronously by the Nintendont ARM kernel (the PPC cannot touch
// the SD card), so a save is a request plus a poll, never a blocking call.
enum SettingsSaveState {
    SETTINGS_SAVE_IDLE,     // nothing in flight
    SETTINGS_SAVE_PENDING,  // doorbell rung, waiting on the kernel
    SETTINGS_SAVE_OK,       // kernel wrote susamune.ini
    SETTINGS_SAVE_ERROR,    // kernel reported a FatFS error (see lastError)
    SETTINGS_SAVE_TIMEOUT,  // kernel never answered (old/stock launcher?)
    SETTINGS_SAVE_UNSUPPORTED,  // emulator build: no persistence backend
};
static_assert(SETTINGS_SAVE_UNSUPPORTED <= 0xFF,
              "settings save state no longer fits in a byte");

class Settings {
public:
    // Install compiled-in defaults, then adopt any values the launcher
    // persisted into the MEM2 handoff block. Call once at boot before
    // anything reads a setting (values live in BSS, so they are zero until
    // this runs) -- see onAppInit in main.cpp, which runs before
    // TApplication::proc() and therefore before the logo/title sequence.
    void init();

    // Install compiled-in defaults only. init() calls this first; it is also
    // the whole of init() on builds with no persistence backend.
    void resetDefaults();

    // --- persistence ---

    // Ask the launcher to write susamune.ini. Non-blocking: this stages the
    // values into the MEM2 block and rings the doorbell. No-op (and reports
    // SETTINGS_SAVE_UNSUPPORTED) when there is no backend. Clears dirty().
    void save();

    // Advance an in-flight save. Call once per frame; returns the current
    // state, transitioning PENDING -> OK / ERROR / TIMEOUT.
    SettingsSaveState pollSave();

    SettingsSaveState saveState() const { return (SettingsSaveState)mSaveState; }

    // FatFS FRESULT from the last failed save, for the toast text.
    u32 lastError() const { return mLastError; }

    // True when a value changed since the last save. The menu uses this to
    // avoid touching the SD card when nothing was edited.
    bool dirty() const { return mDirty; }

    u8   get(SettingId id) const { return mValues[id]; }
    bool getBool(SettingId id) const { return mValues[id] != 0; }
    void set(SettingId id, u8 value);

    // Advance a setting's value by dir (+1 / -1), wrapping. Toggles a bool,
    // steps a choice. Used by the menu for both A-press and left/right.
    void cycle(SettingId id, int dir);

    // Human-readable label for the current value ("On"/"Off" or a choice).
    const char *valueLabel(SettingId id) const;

    static const char     *name(SettingId id);
    static SettingCategory category(SettingId id);

private:
    u8   mValues[SETTING_COUNT];
    bool mDirty;
    u8   mSaveState;
    u32  mLastError;
    u32  mSaveSeq;      // the sequence number we are waiting on
    u32  mSaveWaitFrames;
};

static_assert(sizeof(Settings) == ((SETTING_COUNT + 5) & ~3) + 12,
              "Settings live state layout changed");

// Single global instance. POD (no virtuals / no ctor), so it lives in BSS
// zero-initialised; resetDefaults() installs real defaults at boot.
extern Settings gSettings;

#endif  // _SUSAMUNE_SETTINGS_HXX
