#include "susamune/iling.hxx"

#include "Dolphin/OS.h"
#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "SMS/MSound/MSBGM.hxx"
#include "SMS/Manager/FlagManager.hxx"
#include "SMS/Manager/ItemManager.hxx"
#include "SMS/MapObj/MapObjBase.hxx"
#include "SMS/System/Application.hxx"
#include "susamune/addresses.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/settings.hxx"
#include "susamune/susamune_cfg.h"
#if IS_EMULATOR
#include "susamune/emulator_persistence.hxx"
#endif
#include "susamune/warp_wheel.hxx"

namespace {

enum FinishKind {
    FINISH_SHINE,
    FINISH_TRANSITION,
    FINISH_PLANT,
    FINISH_DEATH,
    FINISH_BOWSER,
};

enum EntryGroup {
    GROUP_BIANCO,
    GROUP_RICCO,
    GROUP_GELATO,
    GROUP_PINNA,
    GROUP_SIRENA,
    GROUP_NOKI,
    GROUP_PIANTA,
    GROUP_AIRSTRIP,
    GROUP_CORONA,
    GROUP_DELFINO,
    GROUP_ANY_PERCENT,
    GROUP_COUNT,
};

enum EntryFlags {
    ENTRY_NONE         = 0,
    ENTRY_CLEAR_RESULT = 1 << 0,
    ENTRY_CLEAR_NEXT   = 1 << 1,
    ENTRY_PB_OVERRIDE  = 1 << 2,
    ENTRY_CARRY_OVERLAY = 1 << 3,
    ENTRY_ACCEPT_PREREQ = 1 << 4,
    ENTRY_PLAZA         = 1 << 5,
    ENTRY_FLAG_MASK     = 0x3F,
};

struct Entry {
    LevelWarp::Dest start;
    u8 result;
    u8 flags;
    u8 prerequisite;
};

struct OverlayFlag {
    u32 id;
    u8 original;
    u8 temporary;
    u8 useBool;
    u8 pad;
};

const u8 kNoShine = 0xFF;
#define ENTRY_STATE(finish, flags) (((finish) << 6) | (flags))

#define RAW(label, area, episode, parent, finish, result, group, flags, prerequisite) \
    {{area, episode, parent}, result, ENTRY_STATE(finish, flags), prerequisite},
#define SHINE(label, area, episode, parent, id, group) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, ENTRY_NONE, kNoShine)
#define SHINE_SET(label, area, episode, parent, id, group, required) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CARRY_OVERLAY, required)
#define SHINE_SET_ALIAS(label, area, episode, parent, id, group, required) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CARRY_OVERLAY | ENTRY_ACCEPT_PREREQ, required)
#define SHINE_CLEAR(label, area, episode, parent, id, group) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT, kNoShine)
#define SHINE_CLEAR_SET(label, area, episode, parent, id, group, required) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT, required)
#define SHINE_FULL(label, area, episode, parent, id, group) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT | ENTRY_CARRY_OVERLAY, kNoShine)
#define SHINE_SECRET(label, area, episode, parent, id, group, slot) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT | ENTRY_PB_OVERRIDE | ENTRY_CARRY_OVERLAY, slot)
#define PLAZA(label, source, scenario, finish, result, slot) \
    RAW(label, TGameSequence::AREA_DOLPIC, scenario, source, finish, result, \
        GROUP_ANY_PERCENT, ENTRY_PLAZA | ENTRY_PB_OVERRIDE, slot)

// The display order is also the menu's group order. Shine ids are retail
// TShine event ids; unlike scene ids they distinguish episode, bonus and
// 100-coin Shines that can all be collected in the same stage.
const Entry kEntries[] = {
#include "iling_entries.inc"
};

#undef SHINE
#undef SHINE_SET
#undef SHINE_SET_ALIAS
#undef SHINE_CLEAR
#undef SHINE_CLEAR_SET
#undef SHINE_FULL
#undef SHINE_SECRET
#undef PLAZA
#undef RAW
#undef ENTRY_STATE

const int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);
constexpr u8 kGroupFirst[GROUP_COUNT] = {0, 13, 25, 37, 50, 63, 75, 87, 89, 90, 106};
const int kGeneratedLabelCount = 87;
const int kRegularLabelSize = 18;
// Fixed-width names and computed suffix offsets cost less than lookup tables.
constexpr char kRegularGroupNames[] =
    "Bianco\0Ricco\0\0Gelato\0Pinna\0\0Sirena\0Noki\0\0\0Pianta";
constexpr char kRegularSuffixes[] =
    "\0\0\0\0 Reds\0 (Full)\0 (Secret)\0 (Race)";
constexpr char kRegularLabelFormats[] =
    "%s %d%s\0%s 100 (E%d)\0%s Hidden";
enum LabelFormatOffset {
    LABEL_FORMAT_NORMAL = 0,
    LABEL_FORMAT_HUNDRED = 8,
    LABEL_FORMAT_HIDDEN = 21,
};

constexpr char *appendLabel(char *out, const char *text) {
    while (*text) {
        *out++ = *text++;
    }
    return out;
}

constexpr const char *regularGroupName(int group) {
    return kRegularGroupNames + group * 7;
}

constexpr int regularSuffixType(int flags) {
    return ((flags >> 3) & 1) + (flags & 1) + ((flags >> 2) & 1) +
           3 * ((flags >> 4) & 1);
}

constexpr const char *regularSuffix(int flags) {
    const int type = regularSuffixType(flags);
    return kRegularSuffixes + type * (type + 3);
}

constexpr void makeRegularLabel(int group, int episode, int result, int flags,
                                char *label) {
    char *out = appendLabel(label, regularGroupName(group));
    *out++ = ' ';
    if (result >= 100) {
        out = appendLabel(out, "100 (E");
        *out++ = '1' + episode;
        *out++ = ')';
    } else if (flags == ENTRY_NONE && result % 10 == 9) {
        out = appendLabel(out, "Hidden");
    } else {
        *out++ = '1' + episode;
        out = appendLabel(out, regularSuffix(flags));
    }
    *out = '\0';
}

constexpr bool sameLabel(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

constexpr bool verifyRegularLabel(const char *expected, int parent, int result,
                                  int group, int flags) {
    if (group >= GROUP_AIRSTRIP) {
        return true;
    }
    const int derivedGroup = result >= 100 ? result - 100 : result / 10;
    if (derivedGroup != group) {
        return false;
    }
    char generated[kRegularLabelSize] = {};
    makeRegularLabel(derivedGroup, parent, result, flags, generated);
    return sameLabel(expected, generated);
}

#define RAW(label, area, episode, parent, finish, result, group, flags, prerequisite) \
    static_assert(verifyRegularLabel(label, parent, result, group, flags), \
                  "IL label does not match its generated form");
#define SHINE(label, area, episode, parent, id, group) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, ENTRY_NONE, kNoShine)
#define SHINE_SET(label, area, episode, parent, id, group, required) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CARRY_OVERLAY, required)
#define SHINE_SET_ALIAS(label, area, episode, parent, id, group, required) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CARRY_OVERLAY | ENTRY_ACCEPT_PREREQ, required)
#define SHINE_CLEAR(label, area, episode, parent, id, group) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT, kNoShine)
#define SHINE_CLEAR_SET(label, area, episode, parent, id, group, required) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT, required)
#define SHINE_FULL(label, area, episode, parent, id, group) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT | ENTRY_CARRY_OVERLAY, kNoShine)
#define SHINE_SECRET(label, area, episode, parent, id, group, slot) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT | ENTRY_PB_OVERRIDE | ENTRY_CARRY_OVERLAY, slot)
#define PLAZA(label, source, scenario, finish, result, slot) \
    RAW(label, TGameSequence::AREA_DOLPIC, scenario, source, finish, result, \
        GROUP_ANY_PERCENT, ENTRY_PLAZA | ENTRY_PB_OVERRIDE, slot)

#include "iling_entries.inc"

#undef SHINE
#undef SHINE_SET
#undef SHINE_SET_ALIAS
#undef SHINE_CLEAR
#undef SHINE_CLEAR_SET
#undef SHINE_FULL
#undef SHINE_SECRET
#undef PLAZA
#undef RAW

#define ENTRY_LABEL(group, label) ENTRY_LABEL_I(group, label)
#define ENTRY_LABEL_I(group, label) ENTRY_LABEL_##group(label)
#define ENTRY_LABEL_GROUP_BIANCO(label)
#define ENTRY_LABEL_GROUP_RICCO(label)
#define ENTRY_LABEL_GROUP_GELATO(label)
#define ENTRY_LABEL_GROUP_PINNA(label)
#define ENTRY_LABEL_GROUP_SIRENA(label)
#define ENTRY_LABEL_GROUP_NOKI(label)
#define ENTRY_LABEL_GROUP_PIANTA(label)
#define ENTRY_LABEL_GROUP_AIRSTRIP(label) label "\0"
#define ENTRY_LABEL_GROUP_CORONA(label) label "\0"
#define ENTRY_LABEL_GROUP_DELFINO(label) label "\0"
#define ENTRY_LABEL_GROUP_ANY_PERCENT(label) label "\0"
#define SHINE(label, area, episode, parent, id, group) ENTRY_LABEL(group, label)
#define SHINE_SET(label, area, episode, parent, id, group, required) ENTRY_LABEL(group, label)
#define SHINE_SET_ALIAS(label, area, episode, parent, id, group, required) ENTRY_LABEL(group, label)
#define SHINE_CLEAR(label, area, episode, parent, id, group) ENTRY_LABEL(group, label)
#define SHINE_CLEAR_SET(label, area, episode, parent, id, group, required) ENTRY_LABEL(group, label)
#define SHINE_FULL(label, area, episode, parent, id, group) ENTRY_LABEL(group, label)
#define SHINE_SECRET(label, area, episode, parent, id, group, slot) ENTRY_LABEL(group, label)
#define PLAZA(label, source, scenario, finish, result, slot) label "\0"
#define RAW(label, area, episode, parent, finish, result, group, flags, prerequisite) \
    ENTRY_LABEL(group, label)

const char kLiteralEntryLabels[] =
#include "iling_entries.inc"
    ;

#undef SHINE
#undef SHINE_SET
#undef SHINE_SET_ALIAS
#undef SHINE_CLEAR
#undef SHINE_CLEAR_SET
#undef SHINE_FULL
#undef SHINE_SECRET
#undef PLAZA
#undef RAW
#undef ENTRY_LABEL_GROUP_BIANCO
#undef ENTRY_LABEL_GROUP_RICCO
#undef ENTRY_LABEL_GROUP_GELATO
#undef ENTRY_LABEL_GROUP_PINNA
#undef ENTRY_LABEL_GROUP_SIRENA
#undef ENTRY_LABEL_GROUP_NOKI
#undef ENTRY_LABEL_GROUP_PIANTA
#undef ENTRY_LABEL_GROUP_AIRSTRIP
#undef ENTRY_LABEL_GROUP_CORONA
#undef ENTRY_LABEL_GROUP_DELFINO
#undef ENTRY_LABEL_GROUP_ANY_PERCENT
#undef ENTRY_LABEL_I
#undef ENTRY_LABEL

const u8 kPlazaStoryHigh[10] = {0x00, 0x10, 0xF0, 0xF0, 0xF0,
                                0x30, 0xF0, 0xF0, 0xF0, 0xF0};
const int kPbSlotCount = 121;
const u32 kPinnaUnlockFlag = 0x10389;
const u32 kYoshiUnlockedFlag = 0x1038F;
const u32 kPostCoronaFlag = 0x103AE;
const u32 kPinna4ShineFlag = 0x10021;
const int kOverlayFlagCount = 2;
const int kBannerFrames = 180;
const int kRecentCount = 5;
const int kShineFanfareDelay = 1;
const u32 kPbSaveTimeoutFrames = 300;
const u32 kPbRetryDelayFrames = 300;

static_assert(sizeof(Entry) == 6, "ILing entry layout changed");
static_assert(kEntryCount == 117, "ILing entry count changed");
static_assert(kEntryCount <= 0x100, "recent IL entry index exceeds u8");
static_assert(kGroupFirst[GROUP_AIRSTRIP] == kGeneratedLabelCount,
              "generated IL label range changed");

struct AttemptState {
    bool running;
    bool ready;
    bool carryRestorePending;
    bool transitionPending;
    bool havePlazaStoryFlags;
    u8 plazaStoryFlags;
    u8 overlayCount;
    OverlayFlag overlayFlags[kOverlayFlagCount];
    LevelWarp::Dest start;
    u8 finish;
    int selectedEntry;
    u32 serial;
};

AttemptState sAttemptState;
AttemptState sSavedAttemptState;
#define sRunning sAttemptState.running
#define sAttemptReady sAttemptState.ready
#define sCarryRestorePending sAttemptState.carryRestorePending
#define sTransitionPending sAttemptState.transitionPending
#define sHavePlazaStoryFlags sAttemptState.havePlazaStoryFlags
#define sPlazaStoryFlags sAttemptState.plazaStoryFlags
#define sOverlayCount sAttemptState.overlayCount
#define sOverlayFlags sAttemptState.overlayFlags
#define sAttemptStart sAttemptState.start
#define sFinishKind sAttemptState.finish
#define sSelectedEntry sAttemptState.selectedEntry
#define sAttemptSerial sAttemptState.serial

bool sHaveSetupShineCount;
u8 sSetupShineCount;
bool sHaveSetupMovieFlag;
bool sSetupMovieFlag;
#if IS_EMULATOR
s32 sPbQf[kPbSlotCount];
#else
#define sPbQf reinterpret_cast<s32 *>(SUSAMUNE_MEM2_PB_LIVE_PPC_BASE)
static_assert(kPbSlotCount * sizeof(s32) <= SUSAMUNE_MEM2_PB_LIVE_SIZE,
              "live PB mirror exceeds its MEM2 window");
#endif
int sBannerFrames;
int sFanfareDelay;
char sBannerText[32];
s32 sRecentQf[kRecentCount];
u8 sRecentEntry[kRecentCount];
u8 sRecentCount;
u8 sRecentNext;

bool sPbBackend;
bool sPbDirty;
bool sPbPending;
bool sPbTimeoutNotified;
u32 sPbSaveSeq;
u32 sPbSaveWaitFrames;
u32 sPbRetryFrames;

bool sHaveSavedAttempt;

void resetPBBackend() {
    sPbBackend = false;
    sPbDirty = false;
    sPbPending = false;
    sPbTimeoutNotified = false;
    sPbSaveSeq = 0;
    sPbSaveWaitFrames = 0;
    sPbRetryFrames = 0;
}

void adoptPBs(const volatile SusamuneCfg *cfg) {
    volatile const SusamuneILingPbCfg *pbs = &cfg->ilingPbs;
    if (cfg->magic != SUSAMUNE_CFG_MAGIC ||
        cfg->version != SUSAMUNE_CFG_VERSION ||
        !(cfg->flags & SUSAMUNE_CFG_FLAG_ILING_PBS) ||
        pbs->magic != SUSAMUNE_ILING_PB_MAGIC ||
        pbs->version != SUSAMUNE_ILING_PB_VERSION ||
        pbs->count > SUSAMUNE_ILING_PB_MAX_SLOTS) {
        return;
    }

    u16 count = pbs->count;
    if (count > kPbSlotCount) {
        count = kPbSlotCount;
    }
    for (u16 i = 0; i < count; i++) {
        if (pbs->values[i] >= 0 &&
            pbs->values[i] <= SUSAMUNE_ILING_PB_MAX_QF) {
            sPbQf[i] = pbs->values[i];
        }
    }
    sPbSaveSeq = pbs->saveSeq;
    sPbBackend = true;
}

void loadPBs() {
    resetPBBackend();

#if IS_EMULATOR
    SusamuneCfg *cfg = EmulatorPersistence::lock();
    if (!cfg) return;
    adoptPBs(cfg);
    EmulatorPersistence::unlock();
#else
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    DCInvalidateRange((void *)cfg, 32);
    DCInvalidateRange((void *)&cfg->ilingPbs, sizeof(SusamuneILingPbCfg));
    adoptPBs(cfg);
#endif
}

void markPBsDirty() {
    if (sPbBackend) {
        sPbDirty = true;
    }
}

void stagePBSave() {
    if (!sPbBackend || !sPbDirty || sPbPending || sPbRetryFrames != 0) {
        return;
    }

#if IS_EMULATOR
    SusamuneCfg *cfg = EmulatorPersistence::lock();
    if (!cfg) {
        sPbBackend = false;
        return;
    }
    volatile SusamuneILingPbCfg *pbs = &cfg->ilingPbs;
#else
    volatile SusamuneILingPbCfg *pbs = &SUSAMUNE_CFG_PPC_PTR->ilingPbs;
#endif
    for (int i = 0; i < kPbSlotCount; i++) {
        pbs->values[i] = sPbQf[i];
    }
    pbs->magic = SUSAMUNE_ILING_PB_MAGIC;
    pbs->version = SUSAMUNE_ILING_PB_VERSION;
    if (pbs->count < kPbSlotCount ||
        pbs->count > SUSAMUNE_ILING_PB_MAX_SLOTS) {
        pbs->count = kPbSlotCount;
    }
#if IS_EMULATOR
    sPbSaveSeq = EmulatorPersistence::commit();
#else
    DCStoreRange((void *)pbs->values, sizeof(pbs->values));

    sPbSaveSeq++;
    pbs->saveSeq = sPbSaveSeq;
    DCStoreRange((void *)pbs, 32);
#endif

    sPbDirty = false;
    sPbPending = true;
    sPbTimeoutNotified = false;
    sPbSaveWaitFrames = 0;
}

void servicePBSave() {
    if (!sPbPending && sPbRetryFrames != 0) {
        sPbRetryFrames--;
    }
    if (sPbPending) {
#if IS_EMULATOR
        u32 error = 0;
        const EmulatorPersistence::SaveResult result =
            EmulatorPersistence::poll(sPbSaveSeq, &error);
        if (result != EmulatorPersistence::SAVE_PENDING) {
            sPbPending = false;
            sPbTimeoutNotified = false;
            sPbSaveWaitFrames = 0;
            if (result == EmulatorPersistence::SAVE_ERROR && gMenu) {
                char message[40];
                snprintf(message, sizeof(message), "PB card save failed: %lu", error);
                gMenu->toast(message);
            }
            if (result == EmulatorPersistence::SAVE_ERROR) {
                sPbDirty = true;
                sPbRetryFrames = kPbRetryDelayFrames;
            }
        } else if (!sPbTimeoutNotified &&
                   ++sPbSaveWaitFrames > kPbSaveTimeoutFrames) {
            sPbTimeoutNotified = true;
            if (gMenu) gMenu->toast("PB card save timed out");
        }
#else
        volatile SusamuneILingPbCfg *pbs = &SUSAMUNE_CFG_PPC_PTR->ilingPbs;
        DCInvalidateRange((void *)&pbs->ackSeq, 32);
        if (pbs->ackSeq == sPbSaveSeq) {
            sPbPending = false;
            sPbTimeoutNotified = false;
            sPbSaveWaitFrames = 0;
            if (pbs->status != 0 && gMenu) {
                char error[40];
                snprintf(error, sizeof(error), "PB save failed: %u",
                         pbs->status);
                gMenu->toast(error);
            }
            if (pbs->status != 0) {
                sPbDirty = true;
                sPbRetryFrames = kPbRetryDelayFrames;
            }
        } else if (!sPbTimeoutNotified &&
                   ++sPbSaveWaitFrames > kPbSaveTimeoutFrames) {
            sPbTimeoutNotified = true;
            if (gMenu) {
                gMenu->toast("PB save timed out");
            }
        }
#endif
    }
    stagePBSave();
}

bool validEntry(int entry) {
    // Internal entry ids are either table indexes or the -1 disarmed sentinel.
    return entry >= 0;
}

bool isPlazaEntry(int entry) {
    return validEntry(entry) && (kEntries[entry].flags & ENTRY_PLAZA);
}

u8 entryFinish(const Entry &entry) {
    return entry.result == 119 ? FINISH_BOWSER : entry.flags >> 6;
}

int pbSlot(int entry) {
    const Entry &item = kEntries[entry];
    return item.flags & ENTRY_PB_OVERRIDE ? item.prerequisite : item.result;
}

bool sameDest(const LevelWarp::Dest &a, const LevelWarp::Dest &b) {
    return a.area == b.area && a.episode == b.episode &&
           a.gameInt3 == b.gameInt3;
}

bool acceptsSkipOrigin(const Entry &item) {
    if (item.result == 27 && item.start.area == 4 && item.start.episode == 7) {
        return sAttemptStart.area == 4 && sAttemptStart.episode == 0;
    }
    if (item.result == 35 && item.start.area == 5 && item.start.episode == 5) {
        return sAttemptStart.area == 5 &&
               (sAttemptStart.episode == 2 || sAttemptStart.episode == 4);
    }
    return false;
}

bool sceneMatches(const TGameSequence &scene, const LevelWarp::Dest &dest) {
    return scene.mAreaID == dest.area && scene.mEpisodeID == dest.episode;
}

bool isInternalScene(const LevelWarp::Dest &start,
                     const TGameSequence &scene) {
    if (start.area == 0x34 && scene.mAreaID == TGameSequence::AREA_CORONABOSS) {
        return true;
    }
    return LevelWarp::parentArea(scene.mAreaID) == start.area;
}

int entryForStartScene(const TGameSequence &scene) {
    for (int i = 0; i < kEntryCount; i++) {
        if (!(kEntries[i].flags & ENTRY_PLAZA) &&
            sceneMatches(scene, kEntries[i].start)) {
            return i;
        }
    }
    return -1;
}

int entryForChildMode(const TGameSequence &scene, int active) {
    int first = -1;
    int secret = -1;
    int reds = -1;
    for (int i = 0; i < kEntryCount; i++) {
        const Entry &item = kEntries[i];
        if (!sceneMatches(scene, item.start)) {
            continue;
        }
        if (first < 0) {
            first = i;
        }
        if (item.flags & ENTRY_PB_OVERRIDE) {
            secret = i;
        } else if ((item.flags & ENTRY_CARRY_OVERLAY) &&
                   item.prerequisite != kNoShine) {
            reds = i;
        }
    }
    if (secret >= 0) {
        bool replay;
        if (active >= 0 && active < kEntryCount &&
            (kEntries[active].flags & ENTRY_CLEAR_RESULT)) {
            replay = false;
        } else if (active >= 0 && active < kEntryCount &&
                   kEntries[active].prerequisite != kNoShine &&
                   !(kEntries[active].flags & ENTRY_PB_OVERRIDE)) {
            replay = true;
        } else if (TFlagManager::smInstance) {
            replay = TFlagManager::smInstance->getFlag(
                0x10000u + kEntries[secret].result) != 0;
        } else {
            replay = false;
        }
        return replay && reds >= 0 ? reds : secret;
    }
    return first;
}

int entryForResult(u8 result) {
    for (int i = 0; i < kEntryCount; i++) {
        const Entry &item = kEntries[i];
        // A completed file makes Ricco 2's race award the replay Shine even
        // when the attempt began outside; its origin still selects Full.
        const bool riccoFullAlias = item.result == 11 && result == 19;
        if (entryFinish(item) == FINISH_SHINE &&
            (item.result == result || riccoFullAlias ||
             ((item.flags & ENTRY_ACCEPT_PREREQ) &&
              item.prerequisite == result)) &&
            (sameDest(item.start, sAttemptStart) || acceptsSkipOrigin(item))) {
            return i;
        }
    }
    return -1;
}

bool readOverlayFlag(const OverlayFlag &flag) {
    TFlagManager *flags = TFlagManager::smInstance;
    return flag.useBool ? flags->getBool(flag.id) : flags->getFlag(flag.id) != 0;
}

void writeOverlayFlag(const OverlayFlag &flag, bool value) {
    TFlagManager *flags = TFlagManager::smInstance;
    if (flag.useBool) {
        flags->setBool(value, flag.id);
    } else {
        flags->setFlag(flag.id, value ? 1 : 0);
    }
}

void applyOverlayFlag(u32 id, bool value, bool useBool) {
    if (!TFlagManager::smInstance || sOverlayCount >= kOverlayFlagCount) {
        return;
    }
    OverlayFlag &flag = sOverlayFlags[sOverlayCount++];
    flag.id = id;
    flag.useBool = useBool;
    flag.original = readOverlayFlag(flag);
    flag.temporary = value;
    writeOverlayFlag(flag, value);
}

void restoreOverlayFlags(bool force = false) {
    if (!TFlagManager::smInstance) {
        sOverlayCount = 0;
        sCarryRestorePending = false;
        return;
    }
    for (u8 i = 0; i < sOverlayCount; i++) {
        const OverlayFlag &flag = sOverlayFlags[i];
        // Preserve a real gameplay write that replaced our temporary value.
        if (force || readOverlayFlag(flag) == (flag.temporary != 0)) {
            writeOverlayFlag(flag, flag.original != 0);
        }
    }
    sOverlayCount = 0;
    sCarryRestorePending = false;
}

void applyEntryOverlay(int entry) {
    if (!validEntry(entry) || !TFlagManager::smInstance || sOverlayCount != 0) {
        return;
    }
    const Entry &item = kEntries[entry];
    if (item.prerequisite == kNoShine &&
        (item.flags & ENTRY_FLAG_MASK) == ENTRY_NONE) {
        return;
    }

    if (item.prerequisite != kNoShine &&
        !(item.flags & ENTRY_PB_OVERRIDE)) {
        applyOverlayFlag(0x10000u + item.prerequisite, true, false);
    }
    if (item.flags & ENTRY_CLEAR_RESULT) {
        applyOverlayFlag(0x10000u + item.result, false, false);
    }
    if (item.flags & ENTRY_CLEAR_NEXT) {
        applyOverlayFlag(0x10000u + item.result + 1, false, false);
    }
}

void reapplyOverlayFlags() {
    for (u8 i = 0; i < sOverlayCount; i++) {
        writeOverlayFlag(sOverlayFlags[i], sOverlayFlags[i].temporary != 0);
    }
}

u8 plazaStoryProfile(u8 scenario, u8 current) {
    return (current & 0x0F) | kPlazaStoryHigh[scenario];
}

void applyPlazaOverlay(int entry) {
    if (!isPlazaEntry(entry) || !TFlagManager::smInstance) {
        return;
    }

    TFlagManager *flags = TFlagManager::smInstance;
    flags->setBool(false, 0x30001);  // do not inherit a death return

    const u8 scenario = kEntries[entry].start.episode;
    if (!sHavePlazaStoryFlags) {
        sPlazaStoryFlags = flags->Type1Flag.m1Type[0x70];
        sHavePlazaStoryFlags = true;
    }
    u8 &story = flags->Type1Flag.m1Type[0x70];
    story = plazaStoryProfile(scenario, story);
    if (scenario <= 1 && !sHaveSetupMovieFlag) {
        const u32 movieFlag = 0x3000B + scenario;
        sSetupMovieFlag = flags->getBool(movieFlag);
        sHaveSetupMovieFlag = true;
        flags->setBool(true, movieFlag);
    }
    if (scenario == 5 || scenario == 2) {
        const u8 minimum = scenario == 5 ? 5 : 20;
        const s32 count = flags->getFlag(0x40000);
        if (count < minimum) {
            sSetupShineCount = (u8)count;
            sHaveSetupShineCount = true;
            flags->setFlag(0x40000, minimum);
        }
    }

    if (sOverlayCount != 0) {
        reapplyOverlayFlags();
        return;
    }

    switch (scenario) {
    case 2:
        applyOverlayFlag(kPostCoronaFlag, true, true);
        break;
    case 7:
        applyOverlayFlag(kPinnaUnlockFlag, false, true);
        break;
    case 8:
        applyOverlayFlag(kYoshiUnlockedFlag, false, true);
        applyOverlayFlag(kPinna4ShineFlag, true, false);
        break;
    case 9:
        applyOverlayFlag(kPostCoronaFlag, false, true);
        break;
    }
}

void restorePlazaSetupState() {
    if (sHaveSetupShineCount && TFlagManager::smInstance) {
        TFlagManager::smInstance->setFlag(0x40000, sSetupShineCount);
    }
    if (sHaveSetupMovieFlag && TFlagManager::smInstance) {
        const u8 scenario = validEntry(sSelectedEntry)
                                ? kEntries[sSelectedEntry].start.episode
                                : 1;
        TFlagManager::smInstance->setBool(sSetupMovieFlag,
                                           0x3000B + scenario);
    }
    sHaveSetupShineCount = false;
    sHaveSetupMovieFlag = false;
}

bool plazaOverlayRunsLive(int entry) {
    if (!isPlazaEntry(entry)) {
        return false;
    }
    const u8 scenario = kEntries[entry].start.episode;
    return scenario == 0 || scenario == 1 || scenario == 5 || scenario == 7;
}

void restorePlazaStoryFlags() {
    if (sHavePlazaStoryFlags && TFlagManager::smInstance) {
        u8 &story = TFlagManager::smInstance->Type1Flag.m1Type[0x70];
        story = (story & 0x0F) | (sPlazaStoryFlags & 0xF0);
    }
    sHavePlazaStoryFlags = false;
}

TMapObjBase *findCoverFruit() {
    // TCoverFruit lives in the item-manager list under its map-data name.
    TObjManager *manager = gpItemManager;
    if (!manager || !manager->mObjAry) {
        return nullptr;
    }

    for (size_t i = 0; i < manager->mObjCount; i++) {
        TMapObjBase *obj = reinterpret_cast<TMapObjBase *>(manager->mObjAry[i]);
        if (obj && obj->mRegisterName &&
            strcmp(obj->mRegisterName, "FruitCoverPine") == 0) {
            return obj;
        }
    }
    return nullptr;
}

void clearAttempt() {
    restorePlazaSetupState();
    restorePlazaStoryFlags();
    restoreOverlayFlags(isPlazaEntry(sSelectedEntry));
    sRunning = false;
    sAttemptReady = false;
    sTransitionPending = false;
    sSelectedEntry = -1;
}

void armAttempt(const Entry &entry, int selected) {
    sRunning = true;
    sAttemptReady = false;
    sTransitionPending = false;
    sAttemptStart = entry.start;
    sFinishKind = entryFinish(entry);
    sSelectedEntry = selected;
    sAttemptSerial = gQFTTimer.attemptSerial();
}

void formatTime(s32 qf, char *out, u32 size) {
    const s32 millis = (qf * 1001) / 120;
    snprintf(out, size, "%d:%02d.%03d", (int)(millis / 60000),
             (int)((millis / 1000) % 60), (int)(millis % 1000));
}

void formatDelta(s32 qf, char *out, u32 size) {
    const s32 millis = (qf * 1001) / 120;
    if (millis < 60000) {
        snprintf(out, size, "-%d.%03d", (int)(millis / 1000),
                 (int)(millis % 1000));
    } else {
        snprintf(out, size, "-%d:%02d.%03d", (int)(millis / 60000),
                 (int)((millis / 1000) % 60), (int)(millis % 1000));
    }
}

void startPbFanfare() {
    JAISound *sound = MSBgm::startBGM(BGM_FANFARE_CASINO);
    if (sound) {
        sound->setVolume(0.85f, 0, 8);
    }
}

void recordPB(int entry, s32 qf) {
    const int slot = pbSlot(entry);
    if (sPbQf[slot] >= 0 && qf >= sPbQf[slot]) {
        return;
    }

    const s32 previous = sPbQf[slot];
    sPbQf[slot] = qf;
    markPBsDirty();
    char time[20];
    char delta[20];
    formatTime(qf, time, sizeof(time));
    if (previous < 0) {
        snprintf(sBannerText, sizeof(sBannerText), "NEW PB: %s --", time);
    } else {
        formatDelta(previous - qf, delta, sizeof(delta));
        snprintf(sBannerText, sizeof(sBannerText), "NEW PB: %s %s", time, delta);
    }
    sBannerFrames = kBannerFrames;

    if (!gSettings.getBool(SETTING_ILING_FANFARE)) {
        sFanfareDelay = 0;
    } else if (entryFinish(kEntries[entry]) == FINISH_SHINE) {
        // The retail Shine Get request is submitted during this frame.
        sFanfareDelay = kShineFanfareDelay;
    } else {
        startPbFanfare();
    }
}

void recordResult(int entry, s32 qf) {
    sRecentQf[sRecentNext] = qf;
    sRecentEntry[sRecentNext] = (u8)entry;
    if (++sRecentNext == kRecentCount) {
        sRecentNext = 0;
    }
    if (sRecentCount < kRecentCount) {
        sRecentCount++;
    }
    recordPB(entry, qf);
}

}  // namespace

namespace ILing {

void init() {
    for (int i = 0; i < kPbSlotCount; i++) {
        sPbQf[i] = -1;
    }
    sRunning = false;
    sAttemptReady = false;
    sCarryRestorePending = false;
    sTransitionPending = false;
    sHaveSetupShineCount = false;
    sHaveSetupMovieFlag = false;
    sHavePlazaStoryFlags = false;
    sOverlayCount = 0;
    sSelectedEntry = -1;
    sAttemptSerial = 0;
    sBannerFrames = 0;
    sFanfareDelay = 0;
    sRecentCount = 0;
    sRecentNext = 0;
    sHaveSavedAttempt = false;
    loadPBs();
}

void onPersistenceReady() {
#if IS_EMULATOR
    loadPBs();
#endif
}

int count() { return kEntryCount; }

const char *label(int entry) {
    if (entry < kGeneratedLabelCount) {
        const Entry &item = kEntries[entry];
        const bool hundred = item.result >= 100;
        const int group = hundred ? item.result - 100 : item.result / 10;
        const u8 flags = item.flags & ENTRY_FLAG_MASK;
        int formatOffset = LABEL_FORMAT_NORMAL;
        const char *suffix = regularSuffix(flags);
        if (hundred) {
            formatOffset = LABEL_FORMAT_HUNDRED;
        } else if (flags == ENTRY_NONE && item.result - group * 10 == 9) {
            formatOffset = LABEL_FORMAT_HIDDEN;
        }
        static char generated[kRegularLabelSize];
        sprintf(generated, kRegularLabelFormats + formatOffset,
                regularGroupName(group),
                item.start.gameInt3 + 1, suffix);
        return generated;
    }

    return PackedText::at(kLiteralEntryLabels, entry - kGeneratedLabelCount);
}

s32 pbQf(int entry) {
    return sPbQf[pbSlot(entry)];
}

int jumpGroup(int entry, int direction) {
    int group = 0;
    while (group + 1 < GROUP_COUNT && entry >= kGroupFirst[group + 1]) {
        group++;
    }
    group += direction > 0 ? 1 : -1;
    if (group < 0) {
        group = GROUP_COUNT - 1;
    } else if (group >= GROUP_COUNT) {
        group = 0;
    }
    return kGroupFirst[group];
}

bool beginsGroup(int entry) {
    for (int group = 1; group < GROUP_COUNT; group++) {
        if (entry == kGroupFirst[group]) {
            return true;
        }
    }
    return false;
}

bool start(int entry) {
    clearAttempt();
    if (gSettings.getBool(SETTING_DISABLE_WARPS)) {
        return false;
    }

    const Entry &item = kEntries[entry];
    const u8 routeFlags = item.flags & ENTRY_FLAG_MASK;
    if ((routeFlags & (ENTRY_CLEAR_RESULT | ENTRY_CARRY_OVERLAY)) ==
        (ENTRY_CLEAR_RESULT | ENTRY_CARRY_OVERLAY)) {
        gSettings.set(SETTING_FLUDD_SECRETS, 1);
    } else if (routeFlags == ENTRY_CARRY_OVERLAY) {
        gSettings.set(SETTING_FLUDD_SECRETS, 2);
    }
    armAttempt(item, entry);
    if (isPlazaEntry(entry)) {
        if (!TFlagManager::smInstance) {
            clearAttempt();
            return false;
        }
        const LevelWarp::Dest source = {item.start.gameInt3, 0, 0};
        const LevelWarp::Dest destination = {item.start.area, item.start.episode, 0};
        LevelWarp::warpFrom(source, destination);
        return true;
    }

    LevelWarp::warpTo(item.start);
    return true;
}

void clearPB(int entry) {
    const int slot = pbSlot(entry);
    sPbQf[slot] = -1;
    markPBsDirty();
    sBannerFrames = 0;
}

void onWarpTail() {
    if (!sRunning || !validEntry(sSelectedEntry)) {
        return;
    }
    if (isPlazaEntry(sSelectedEntry)) {
        applyPlazaOverlay(sSelectedEntry);
    } else {
        applyEntryOverlay(sSelectedEntry);
    }
}

void beforeStageSetup() {
    const TGameSequence &scene = gpApplication.mCurrentScene;

    if (sRunning && sTransitionPending && validEntry(sSelectedEntry) &&
        sFinishKind == FINISH_TRANSITION) {
        clearAttempt();
    }

    if (sRunning) {
        if (sceneMatches(scene, sAttemptStart)) {
            sAttemptReady = false;
            sAttemptSerial = gQFTTimer.attemptSerial();
            if (isPlazaEntry(sSelectedEntry)) {
                applyPlazaOverlay(sSelectedEntry);
            } else if (sSelectedEntry >= 0) {
                applyEntryOverlay(sSelectedEntry);
            }
            return;
        }
        if (!isPlazaEntry(sSelectedEntry) &&
            isInternalScene(sAttemptStart, scene)) {
            if (validEntry(sSelectedEntry) &&
                (kEntries[sSelectedEntry].flags & ENTRY_CARRY_OVERLAY)) {
                applyEntryOverlay(sSelectedEntry);
            }
            return;
        }
        clearAttempt();
    }

    const int entry = entryForStartScene(scene);
    if (entry >= 0) {
        // Natural entry and ordinary level reset arm every valid result from
        // this start scene; the exact TShine id chooses the PB at the finish.
        armAttempt(kEntries[entry], -1);
    }
}

void onStageSetup() {
    if (!sRunning) {
        return;
    }

    const TGameSequence &scene = gpApplication.mCurrentScene;
    const bool atStart = sceneMatches(scene, sAttemptStart);
    const bool plaza = isPlazaEntry(sSelectedEntry);
    const bool internal = !plaza &&
                          isInternalScene(sAttemptStart, scene);
    if (!atStart && !internal) {
        return;
    }

    if (plaza) {
        restorePlazaSetupState();
        u8 &story = TFlagManager::smInstance->Type1Flag.m1Type[0x70];
        story = plazaStoryProfile(kEntries[sSelectedEntry].start.episode, story);
        reapplyOverlayFlags();
        if (kEntries[sSelectedEntry].start.episode == 8) {
            TMapObjBase *coverFruit = findCoverFruit();
            if (coverFruit) {
                coverFruit->makeObjAppeared();
            }
        }
        if (!plazaOverlayRunsLive(sSelectedEntry)) {
            restoreOverlayFlags(true);
            restorePlazaStoryFlags();
        }
    } else if (validEntry(sSelectedEntry) &&
               (kEntries[sSelectedEntry].flags & ENTRY_CARRY_OVERLAY)) {
        sCarryRestorePending = sOverlayCount != 0;
    } else {
        restoreOverlayFlags();
    }
}

void update() {
    servicePBSave();

    if (sRunning && !sTransitionPending && validEntry(sSelectedEntry) &&
        sFinishKind == FINISH_TRANSITION) {
        s32 qf;
        u16 target;
        if (gQFTTimer.consumeTransition(&qf, &target)) {
            if ((u8)target == kEntries[sSelectedEntry].result) {
                recordResult(sSelectedEntry, qf);
            }
            // Keep Plaza setup flags alive until the committed stage takes
            // over, but never let another portal event record this attempt.
            sTransitionPending = true;
            return;
        }
    }

    if ((sOverlayCount != 0 || sHavePlazaStoryFlags) &&
        gpApplication.mContext != TApplication::CONTEXT_DIRECT_STAGE &&
        gpApplication.mContext != TApplication::CONTEXT_DIRECT_MOVIE &&
        gpApplication.mContext != TApplication::CONTEXT_DIRECT_SHINE_SELECT) {
        // An aborted intermediate movie must not carry practice flags into
        // file select or another save file.
        clearAttempt();
    }

    if (sFanfareDelay > 0 && --sFanfareDelay == 0 &&
        gSettings.getBool(SETTING_ILING_FANFARE)) {
        MSBgm::stopBGM(BGM_GET_SHINE, 0);
        startPbFanfare();
    }

    if (sBannerFrames > 0) {
        sBannerFrames--;
    }
    if (!sRunning) {
        return;
    }

    if (isPlazaEntry(sSelectedEntry) && gpMarDirector &&
        (gpMarDirector->mCurState == TMarDirector::STATE_PAUSE_MENU ||
         gpMarDirector->mCurState == TMarDirector::STATE_SAVE_CARD)) {
        // Never let temporary route progression reach a card save.
        clearAttempt();
        return;
    }

    if (sTransitionPending && sFinishKind == FINISH_TRANSITION) {
        return;
    }

    const u32 serial = gQFTTimer.attemptSerial();
    if (sAttemptReady && serial != sAttemptSerial) {
        const TGameSequence &scene = gpApplication.mCurrentScene;
        if (sceneMatches(scene, sAttemptStart)) {
            // A same-scene reset keeps its explicitly selected Secret/Reds mode.
            sAttemptSerial = serial;
        } else {
            // A child reset becomes an independent Secret/Reds attempt. Read
            // the temporary main-Shine mode before clearAttempt restores it.
            const int entry = isInternalScene(sAttemptStart, scene)
                                  ? entryForChildMode(scene, sSelectedEntry)
                                  : -1;
            clearAttempt();
            if (entry >= 0) {
                armAttempt(kEntries[entry], entry);
                sAttemptSerial = serial;
                sAttemptReady = true;
            }
            return;
        }
    }

    if (sCarryRestorePending && gpMarDirector &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING) {
        // setMario has consumed the temporary mode; restore before gameplay
        // can write the memory card.
        restoreOverlayFlags();
    }

    if (!sAttemptReady) {
        if (serial == sAttemptSerial) {
            return;
        }
        sAttemptSerial = serial;
        sAttemptReady = true;
    }

    s32 qf;
    int completedEntry = -1;
    if (sFinishKind == FINISH_TRANSITION) {
        return;
    } else if (sFinishKind == FINISH_BOWSER) {
        if (!gQFTTimer.consumeBowser(&qf)) {
            return;
        }
        completedEntry = kGroupFirst[GROUP_CORONA];
    } else if (sFinishKind == FINISH_PLANT || sFinishKind == FINISH_DEATH) {
        if (!gQFTTimer.consumeCustom(sFinishKind == FINISH_DEATH, &qf)) {
            return;
        }
        completedEntry = sSelectedEntry;
    } else {
        if (!gQFTTimer.consumeShine(&qf)) {
            return;
        }
        const u32 shineId = *reinterpret_cast<volatile u32 *>(
            SUSAMUNE_ADDR_LAST_SHINE_ID);
        if (shineId <= 0xFF) {
            completedEntry = entryForResult((u8)shineId);
        }
    }

    if (completedEntry >= 0) {
        recordResult(completedEntry, qf);
    }
    if (completedEntry >= 0 && sFinishKind == FINISH_PLANT) {
        // The timed hit precedes the retail death/event sequence. Keep
        // ownership of the Plaza flags until reset, departure or pause.
        sAttemptReady = false;
        return;
    }
    clearAttempt();
}

void onSavestateSaved() {
    sSavedAttemptState = sAttemptState;
    sHaveSavedAttempt = true;
}

void onSavestateLoaded() {
    sFanfareDelay = 0;
    sBannerFrames = 0;
    sHaveSetupShineCount = false;
    sHaveSetupMovieFlag = false;
    if (!sHaveSavedAttempt) {
        sRunning = false;
        sAttemptReady = false;
        sCarryRestorePending = false;
        sTransitionPending = false;
        sHavePlazaStoryFlags = false;
        sOverlayCount = 0;
        sSelectedEntry = -1;
        return;
    }
    // The restored game flags match the save-time attempt. Restore that
    // attempt's temporary overlays, then disarm it: loaded time is never PB-
    // eligible. A genuine reset or stage load arms the next attempt normally.
    sAttemptState = sSavedAttemptState;
    clearAttempt();
}

void draw(Menu *menu) {
    if (!menu || menu->shown()) {
        return;
    }

    if (gSettings.getBool(SETTING_ILING_RECENT) && sRecentCount != 0) {
        const int x = 382;
        const int y = 92;
        const int w = 250;
        const int lineH = 15;
        const int h = 22 + lineH * sRecentCount;
        menu->fillBox(x, y, w, h, JUtility::TColor(12, 20, 34, 205));
        menu->fillBox(x, y, 3, h, JUtility::TColor(80, 180, 255, 255));
        menu->drawText("Recent ILs", x + 10, y + 5, 12, 12,
                       JUtility::TColor(160, 220, 255, 255));

        for (u8 row = 0; row < sRecentCount; row++) {
            int index = sRecentNext - 1 - row;
            if (index < 0) {
                index += kRecentCount;
            }
            char time[20];
            char line[48];
            formatTime(sRecentQf[index], time, sizeof(time));
            snprintf(line, sizeof(line), "%s  %s",
                     label(sRecentEntry[index]), time);
            menu->drawText(line, x + 10, y + 21 + row * lineH, 12, 12,
                           JUtility::TColor(245, 248, 255, 255));
        }
    }

    if (sBannerFrames > 0) {
        const int size = 22;
        const int textW = Menu::textWidth(sBannerText, size);
        const int w = textW + 28;
        const int h = 42;
        const int x = (640 - w) / 2;
        const int y = 42;

        menu->fillBox(x, y, w, h, JUtility::TColor(90, 58, 4, 230));
        menu->fillBox(x, y, 4, h, JUtility::TColor(255, 196, 40, 255));
        menu->drawText(sBannerText, x + 14, y + 10, size, size,
                       JUtility::TColor(255, 239, 178, 255));
    }
}

}  // namespace ILing
