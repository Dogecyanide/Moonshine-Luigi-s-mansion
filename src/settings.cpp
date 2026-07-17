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

// Descriptor table, indexed by SettingId. One row per setting.
const SettingDesc kSettingDescs[SETTING_COUNT] = {
    // SETTING_SAVE_RNG_STATE: when On (default), a savestate load restores the
    // libc RNG seed so the RNG stream rewinds with the state -- the game's
    // historical behaviour. Off leaves the seed advancing across a load, so a
    // runner can practise varied RNG outcomes from the same snapshot.
    { "Save RNG state", SETTING_BOOL, 2, 1, nullptr },
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
