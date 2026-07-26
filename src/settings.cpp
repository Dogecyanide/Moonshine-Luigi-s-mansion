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

#include "Dolphin/OS.h"  // DCInvalidateRange, DCStoreRange
#include "susamune/susamune_cfg.h"

namespace {

// Stable ini keys, in SettingId order, from the same list that builds the
// enum -- so the launcher's key table and this one cannot drift apart.
#define SUSAMUNE_SETTING_KEY(id, key) key,
const char *const kIniKeys[SETTING_COUNT] = { SUSAMUNE_SETTING_LIST(SUSAMUNE_SETTING_KEY) };
#undef SUSAMUNE_SETTING_KEY


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
    // The three "stateful" ports. Unlike the patch/hook features these are
    // implemented as game logic in main.cpp (Intro Skip, Stage Intro Skip);
    // No Shine Get Animation is a one-word patch in features.cpp.
    { "Intro skip", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_MISC },
    { "Stage intro skip", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_MISC },
    { "No shine get animation", SETTING_BOOL, 2, 0, nullptr, SETTING_CAT_MISC },
};

const u32 kSettingsMagic   = 0x53535454u;  // 'SSTT'
const u16 kSettingsVersion = 1u;

const char *const kBoolLabels[2] = { "Off", "On" };

// How long to wait for the kernel to acknowledge a save before giving up.
// Generous: the kernel only services the doorbell between disc reads, and a
// FatFS write of a few hundred bytes can wait behind one.
const u32 kSaveTimeoutFrames = 300;  // ~5s at 60Hz

}  // namespace

Settings gSettings;

void Settings::resetDefaults() {
    mData.magic   = kSettingsMagic;
    mData.version = kSettingsVersion;
    mData.count   = SETTING_COUNT;
    for (int i = 0; i < SETTING_COUNT; i++) {
        mData.values[i] = kSettingDescs[i].defaultValue;
    }
    mDirty          = false;
    mSaveState      = SETTINGS_SAVE_IDLE;
    mLastError      = 0;
    mSaveSeq        = 0;
    mSaveWaitFrames = 0;
}

// ---------------------------------------------------------------------
// Persistence
//
// On console the Nintendont ARM kernel has already parsed susamune.ini into
// the MEM2 handoff block by the time the game boots (see SusamuneCfg.c), so
// loading is just "validate and copy". On emulator there is no launcher and
// hence no backend, so we keep the compiled-in defaults.
// ---------------------------------------------------------------------

#if IS_EMULATOR

void Settings::init() { resetDefaults(); }

void Settings::save() {
    mSaveState = SETTINGS_SAVE_UNSUPPORTED;
    mDirty     = false;
}

SettingsSaveState Settings::pollSave() { return mSaveState; }

#else

void Settings::init() {
    resetDefaults();

    // The kernel wrote this block from the ARM side before the game booted;
    // drop anything our cache may have speculatively pulled in first.
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    DCInvalidateRange((void *)cfg, sizeof(SusamuneCfg));

    if (cfg->magic != SUSAMUNE_CFG_MAGIC || cfg->version != SUSAMUNE_CFG_VERSION) {
        // No launcher, a stock Nintendont, or a build mismatch. Defaults stand
        // and save() will still work if a compatible kernel is listening --
        // but not against a block we could not identify, so leave it alone.
        mSaveState = SETTINGS_SAVE_UNSUPPORTED;
        return;
    }

    // Copy only what the writer actually filled in. A kernel older than the
    // mod carries fewer settings than SETTING_COUNT; the remainder keep their
    // compiled-in defaults, which is what makes adding a setting safe.
    u16 n = cfg->count;
    if (n > SETTING_COUNT) {
        n = SETTING_COUNT;
    }
    for (u16 i = 0; i < n; i++) {
        u8 v = cfg->values[i];
        if (v == SUSAMUNE_CFG_UNSET) {
            continue;  // absent from the ini -- keep the default
        }
        // Route through set() so an out-of-range value in a hand-edited ini
        // is clamped into the setting's choice range rather than trusted.
        set((SettingId)i, v);
    }

    // set() marks dirty; adopting persisted values is not a user edit.
    mDirty     = false;
    mSaveSeq   = cfg->saveSeq;
    mSaveState = SETTINGS_SAVE_IDLE;

    // First boot on this SD card: the kernel found no susamune.ini and cannot
    // author one, because the defaults live here rather than in the launcher.
    // Write it out now so the user has a file to look at (and to hand-edit)
    // without having to visit the menu first. Fire-and-forget -- the menu that
    // would report the result does not exist this early.
    if (cfg->flags & SUSAMUNE_CFG_FLAG_NO_FILE) {
        save();
    }
}

void Settings::save() {
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;

    if (mSaveState == SETTINGS_SAVE_UNSUPPORTED) {
        mDirty = false;
        return;
    }

    for (int i = 0; i < SETTING_COUNT; i++) {
        cfg->values[i] = mData.values[i];
    }
    cfg->count = SETTING_COUNT;

    // Publish the payload before the doorbell, so the kernel can never see a
    // bumped saveSeq alongside a half-written values[].
    DCStoreRange((void *)cfg->values, sizeof(cfg->values));

    mSaveSeq     = cfg->saveSeq + 1;
    cfg->saveSeq = mSaveSeq;
    DCStoreRange((void *)cfg, 32);  // line 0: magic/version/count/saveSeq

    mDirty          = false;
    mLastError      = 0;
    mSaveWaitFrames = 0;
    mSaveState      = SETTINGS_SAVE_PENDING;
}

SettingsSaveState Settings::pollSave() {
    if (mSaveState != SETTINGS_SAVE_PENDING) {
        return mSaveState;
    }

    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    // Line 1 is written exclusively by the kernel, so invalidating it can
    // never discard a pending write of ours (see the ownership note in
    // susamune_cfg.h).
    DCInvalidateRange((void *)&cfg->ackSeq, 32);

    if (cfg->ackSeq == mSaveSeq) {
        mLastError = cfg->status;
        mSaveState = mLastError ? SETTINGS_SAVE_ERROR : SETTINGS_SAVE_OK;
    } else if (++mSaveWaitFrames > kSaveTimeoutFrames) {
        mSaveState = SETTINGS_SAVE_TIMEOUT;
    }
    return mSaveState;
}

#endif  // IS_EMULATOR

void Settings::set(SettingId id, u8 value) {
    const SettingDesc &d = kSettingDescs[id];
    if (d.numChoices != 0) {
        value = value % d.numChoices;
    }
    if (mData.values[id] != value) {
        mDirty = true;
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
    if (mData.values[id] != (u8)v) {
        mDirty = true;
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

const char *Settings::iniKey(SettingId id) { return kIniKeys[id]; }

// kSettingDescs is indexed by SettingId and must stay row-for-row aligned with
// SUSAMUNE_SETTING_LIST. Adding a list row without a descriptor row (or vice
// versa) fails here rather than silently shifting every setting's meaning.
static_assert(sizeof(kSettingDescs) / sizeof(kSettingDescs[0]) == SETTING_COUNT,
              "kSettingDescs must have one row per SUSAMUNE_SETTING_LIST entry");
static_assert(SETTING_COUNT <= SUSAMUNE_CFG_MAX_SETTINGS,
              "SETTING_COUNT exceeds the MEM2 handoff block's values[] capacity");
