#ifndef _SUSAMUNE_SETTINGS_HXX
#define _SUSAMUNE_SETTINGS_HXX

#include <Dolphin/types.h>

// =====================================================================
// settings.hxx
//
// The mod's persistent settings, kept deliberately independent of the
// menu that edits them (menu.cpp) and of any feature that reads them
// (e.g. savestate.cpp). A setting is one entry in a static descriptor
// table plus one byte of live value; everything else -- how the menu
// renders it, how a feature queries it, how it will one day serialise to
// the memory card / SD card -- is driven off that table so adding a
// setting is a one-line change.
// =====================================================================

// The full set of settings. Add a value here and a matching row in
// kSettingDescs (settings.cpp); nothing else needs to change. The enum order
// controls the values[] layout and the within-category display order in the
// menu (which filters by category), so keep each category's entries grouped.
enum SettingId {
    // -- Savestate --
    SETTING_SAVE_RNG_STATE,  // savestates rewind the libc RNG seed on load

    // -- Quality-of-life toggles (ported gecko codes; see features.cpp) --
    SETTING_INFINITE_LIVES,
    SETTING_UNLOCK_NOZZLES,
    SETTING_UNLOCK_YOSHI,
    SETTING_ANY_FRUIT_YOSHI,
    SETTING_INFINITE_JUICE,
    SETTING_EXIT_AREA_EVERYWHERE,
    SETTING_FMV_SKIPS,
    SETTING_RESPAWN_SHINES,
    SETTING_FRUIT_NEVER_TIMEOUT,
    SETTING_FREE_PAUSE,
    SETTING_DISABLE_BLUE_COIN,
    SETTING_DEATHLESS_BLOOPER,
    SETTING_FAST_TEXT,      // DPad Functions: replace dialog with "!!!"
    SETTING_FLUDD_SECRETS,  // CHOICE: Completed(default) / No FLUDD / All secrets

    // -- Cosmetic toggles --
    SETTING_MUTE_BGM,
    SETTING_SHINE_OUTFIT,
    SETTING_SHINY_SHINES,
    SETTING_SHADOW_MARIO_HP,
    SETTING_REPLACE_EPISODE_NAMES,

    // -- Misc toggles --
    SETTING_FAST_PIANTISSIMO,
    SETTING_NEVER_PAUSE_IGT,
    SETTING_FORCE_PLAZA_EVENTS,
    SETTING_NOZZLE_LOCK,  // CHOICE: Unlocked(default) / Rocket / Turbo / Hover

    SETTING_COUNT
};

enum SettingType {
    SETTING_BOOL,    // two states, rendered On / Off
    SETTING_CHOICE,  // 1-of-N, rendered as choices[value]
};

// Which menu tab a setting appears under. The menu builds one generic tab per
// category and renders the settings tagged with it.
enum SettingCategory {
    SETTING_CAT_SAVESTATE,
    SETTING_CAT_QOL,
    SETTING_CAT_COSMETIC,
    SETTING_CAT_MISC,
    SETTING_CAT_COUNT
};

// Static, const description of one setting. Lives in .rodata.
struct SettingDesc {
    const char        *name;        // label shown in the menu
    SettingType        type;
    u8                 numChoices;  // SETTING_BOOL => 2
    u8                 defaultValue;
    const char *const *choices;     // SETTING_CHOICE => numChoices labels
    SettingCategory    category;    // menu tab it renders under
};

// The serialisable payload. POD so it can be blitted to/from a save file
// wholesale once memory-card / SD persistence lands; magic + version let a
// loader reject a stale or foreign blob (mirrors the savestate header).
struct SettingsData {
    u32 magic;
    u16 version;
    u16 count;
    u8  values[SETTING_COUNT];
};

class Settings {
public:
    // Load compiled-in defaults. Call once at boot before anything reads a
    // setting (values live in BSS, so they are zero until this runs).
    void resetDefaults();

    u8   get(SettingId id) const { return mData.values[id]; }
    bool getBool(SettingId id) const { return mData.values[id] != 0; }
    void set(SettingId id, u8 value);

    // Advance a setting's value by dir (+1 / -1), wrapping. Toggles a bool,
    // steps a choice. Used by the menu for both A-press and left/right.
    void cycle(SettingId id, int dir);

    // Human-readable label for the current value ("On"/"Off" or a choice).
    const char *valueLabel(SettingId id) const;

    static const SettingDesc &desc(SettingId id);

    // Future persistence (memory card / SD). The data is already a POD blob;
    // these will stamp/verify magic+version and copy mData. Declared now so
    // callers can be written against them.
    // bool serialize(void *out, u32 cap) const;
    // bool deserialize(const void *in, u32 len);

private:
    SettingsData mData;
};

// Single global instance. POD (no virtuals / no ctor), so it lives in BSS
// zero-initialised; resetDefaults() installs real defaults at boot.
extern Settings gSettings;

#endif  // _SUSAMUNE_SETTINGS_HXX
