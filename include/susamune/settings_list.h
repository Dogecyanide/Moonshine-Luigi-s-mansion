#ifndef SUSAMUNE_SETTINGS_LIST_H
#define SUSAMUNE_SETTINGS_LIST_H

// =====================================================================
// settings_list.h
//
// The canonical list of persisted settings, shared by every toolchain:
// the mod (C++, Kuribo clang), the Nintendont ARM kernel (devkitARM),
// and the PPC loader (devkitPPC). Keep this file plain C.
//
// Each row is (enumerator, ini_key):
//   - the enumerator defines SettingId and therefore the values[] layout
//     of the MEM2 handoff blob, which carries no per-key names -- only a
//     count. So ROWS MUST NOT BE REORDERED: append new settings at the end
//     of their category instead. Appending is always safe -- an older
//     kernel reports a smaller count and the rest keep their defaults.
//
//     REMOVING a row shifts every later index, and the launcher and the
//     mod_<region>.bin files no longer ship as one binary, so a stale
//     launcher would map keys onto the wrong settings. Bump
//     SUSAMUNE_CFG_VERSION (susamune_cfg.h) when you remove one: a
//     mismatched pair then refuses the block instead of scrambling values.
//     susamune.ini itself is keyed by name, so a user only loses the
//     removed key, not the rest of their file.
//   - the ini_key is what appears in susamune.ini. It is deliberately
//     separate from the menu display name in kSettingDescs so that
//     relabelling a menu entry never invalidates a user's saved file.
//     Only the launcher expands the keys into strings -- the mod reads
//     values by index and would otherwise carry the table for nothing.
//
// The menu display name, type, choice labels, default and category stay
// in kSettingDescs (settings.cpp) -- the launcher has no use for them and
// they cannot be expressed in a C-compatible header.
// =====================================================================

#define SUSAMUNE_SETTING_LIST(X)                                              \
    /* -- Savestate -- */                                                     \
    X(SETTING_SAVE_RNG_STATE,       "save_rng_state")                         \
    /* -- Quality-of-life -- */                                               \
    X(SETTING_INFINITE_LIVES,       "infinite_lives")                         \
    X(SETTING_UNLOCK_NOZZLES,       "unlock_nozzles")                         \
    X(SETTING_UNLOCK_YOSHI,         "unlock_yoshi")                           \
    X(SETTING_ANY_FRUIT_YOSHI,      "any_fruit_yoshi")                        \
    X(SETTING_INFINITE_JUICE,       "infinite_juice")                         \
    X(SETTING_EXIT_AREA_EVERYWHERE, "exit_area_everywhere")                   \
    X(SETTING_FMV_SKIPS,            "fmv_skips")                              \
    X(SETTING_RESPAWN_SHINES,       "respawn_shines")                         \
    X(SETTING_FRUIT_NEVER_TIMEOUT,  "fruit_never_timeout")                    \
    X(SETTING_FREE_PAUSE,           "free_pause")                             \
    X(SETTING_DISABLE_BLUE_COIN,    "disable_blue_coin")                      \
    X(SETTING_DEATHLESS_BLOOPER,    "deathless_blooper")                      \
    X(SETTING_FAST_TEXT,            "fast_text")                              \
    X(SETTING_FLUDD_SECRETS,        "fludd_secrets")                          \
    /* -- Cosmetic -- */                                                      \
    X(SETTING_MUTE_BGM,             "mute_bgm")                               \
    X(SETTING_SHINE_OUTFIT,         "shine_outfit")                           \
    X(SETTING_SHINY_SHINES,         "shiny_shines")                           \
    X(SETTING_SHADOW_MARIO_HP,      "shadow_mario_hp")                        \
    X(SETTING_REPLACE_EPISODE_NAMES, "replace_episode_names")                 \
    /* -- Misc -- */                                                          \
    X(SETTING_FAST_PIANTISSIMO,     "fast_piantissimo")                       \
    X(SETTING_NEVER_PAUSE_IGT,      "never_pause_igt")                        \
    X(SETTING_FORCE_PLAZA_EVENTS,   "force_plaza_events")                     \
    X(SETTING_NOZZLE_LOCK,          "nozzle_lock")                            \
    X(SETTING_STAGE_INTRO_SKIP,     "stage_intro_skip")                       \
    X(SETTING_NO_SHINE_ANIM,        "no_shine_anim")                         \
    X(SETTING_SHOW_BGM_SLOTS,       "show_bgm_slots")                         \
    /* -- Quality-of-life (appended for persistence compatibility) -- */      \
    X(SETTING_DISABLE_Z_MENU,       "disable_z_menu")                         \
    X(SETTING_AREA_LOCK,            "area_lock")                              \
    X(SETTING_INTRO_SKIP,           "intro_skip")

#endif  // SUSAMUNE_SETTINGS_LIST_H
