// =====================================================================
// settings.cpp
//
// The setting descriptor table and the tiny value store behind it. See
// settings.hxx for the design; features read settings through gSettings
// (e.g. savestate.cpp gates the RNG-seed snapshot on
// SETTING_SAVE_RNG_STATE) and menu.cpp renders/edits them generically off
// kSettingDescs.
// =====================================================================

#include "susamune/settings.hxx"

namespace {

// CHOICE labels. Index 0 is the default / "restore original" state (see the
// choice-feature apply in features.cpp), so it must be the game's normal
// behaviour for the corresponding gecko code.
const char *const kFluddLabels[3]  = { "Completed", "No FLUDD", "All secrets" };
const char *const kNozzleLabels[4] = { "Unlocked", "Rocket", "Turbo", "Hover" };

// Descriptor table, indexed by SettingId. One row per setting.
// Row layout: { name, type, numChoices, default, choices, category }.
const SettingDesc kSettingDescs[SETTING_COUNT] = {
    // SETTING_SAVE_RNG_STATE: when On (default), a savestate load restores the
    // libc RNG seed so the RNG stream rewinds with the state -- the game's
    // historical behaviour. Off leaves the seed advancing across a load, so a
    // runner can practise varied RNG outcomes from the same snapshot.
    { "Save RNG state", SETTING_BOOL, 2, 1, nullptr, SETTING_CAT_SAVESTATE },

    // Quality-of-life toggles. Each drives a set of memory patches in
    // features.cpp (ports of the corresponding SMS practice gecko codes);
    // default Off so nothing changes until the runner opts in.
    { "Infinite lives", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Unlock nozzles", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Unlock Yoshi", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Any fruit opens Yoshi eggs", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Infinite juice", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Exit area everywhere", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "FMV skips", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Respawn one-time shines", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Fruit never times out", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Free pause", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Disable blue coin flag", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Deathless blooper surfing", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "Fast text", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_QOL },
    { "FLUDD in secrets", SETTING_CHOICE, 3, 0, kFluddLabels, SETTING_CAT_QOL },

    // Cosmetic toggles.
    { "Mute background music", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_COSMETIC },
    { "Shine outfit", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_COSMETIC },
    { "Shiny shines", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_COSMETIC },
    { "Shadow Mario HP meter", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_COSMETIC },
    { "Episode names as IDs", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_COSMETIC },

    // Misc toggles.
    { "Fast Piantissimo", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_MISC },
    { "Never pause IGT", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_MISC },
    { "Force plaza events", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_MISC },
    { "Nozzle lock", SETTING_CHOICE, 4, 0, kNozzleLabels, SETTING_CAT_MISC },
};

const u32 kSettingsMagic   = 0x53535454u;  // 'SSTT'
const u16 kSettingsVersion = 1u;

const char *const kBoolLabels[2] = { "Off", "On" };

}  // namespace

Settings gSettings;

void Settings::resetDefaults() {
    mData.magic   = kSettingsMagic;
    mData.version = kSettingsVersion;
    mData.count   = SETTING_COUNT;
    for (int i = 0; i < SETTING_COUNT; i++) {
        mData.values[i] = kSettingDescs[i].defaultValue;
    }
}

void Settings::set(SettingId id, u8 value) {
    const SettingDesc &d = kSettingDescs[id];
    if (d.numChoices != 0) {
        value = value % d.numChoices;
    }
    mData.values[id] = value;
}

void Settings::cycle(SettingId id, int dir) {
    const SettingDesc &d = kSettingDescs[id];
    int n = d.numChoices ? d.numChoices : 2;
    int v = (int)mData.values[id] + dir;
    // Wrap into [0, n). dir is +/-1, so one add/sub suffices.
    if (v < 0) {
        v += n;
    } else if (v >= n) {
        v -= n;
    }
    mData.values[id] = (u8)v;
}

const char *Settings::valueLabel(SettingId id) const {
    const SettingDesc &d = kSettingDescs[id];
    u8 v = mData.values[id];
    if (d.type == SETTING_CHOICE && d.choices) {
        return d.choices[v % d.numChoices];
    }
    return kBoolLabels[v ? 1 : 0];
}

const SettingDesc &Settings::desc(SettingId id) { return kSettingDescs[id]; }
