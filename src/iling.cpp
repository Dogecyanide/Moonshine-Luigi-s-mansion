#include "susamune/iling.hxx"

#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "SMS/MSound/MSBGM.hxx"
#include "SMS/Manager/FlagManager.hxx"
#include "SMS/Manager/ItemManager.hxx"
#include "SMS/MapObj/MapObjBase.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/Watergun.hxx"
#include "SMS/System/Application.hxx"
#include "susamune/addresses.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/menu.hxx"
#include "susamune/mem2_map.h"
#include "susamune/packed_text.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/records.hxx"
#include "susamune/settings.hxx"
#include "susamune/susamune_cfg.h"
#if IS_EMULATOR
#include "susamune/emulator_persistence.hxx"
#endif
#include "susamune/warp_wheel.hxx"

namespace {

typedef JUtility::TColor Color;

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
#define PINNA_EYG(label, slot) \
    RAW(label, 5, 2, 2, FINISH_SHINE, 35, GROUP_PINNA, \
        ENTRY_CLEAR_RESULT | ENTRY_PB_OVERRIDE | ENTRY_CARRY_OVERLAY, slot)
#define SHINE_INSIDE(label, area, episode, parent, id, group, slot) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT | ENTRY_PB_OVERRIDE, slot)
#define SHINE_SECRET(label, area, episode, parent, id, group, slot) \
    RAW(label, area, episode, parent, FINISH_SHINE, id, group, \
        ENTRY_CLEAR_RESULT | ENTRY_PB_OVERRIDE | ENTRY_CARRY_OVERLAY, slot)
#define PLAZA(label, source, scenario, finish, result, slot) \
    RAW(label, TGameSequence::AREA_DOLPIC, scenario, source, finish, result, \
        GROUP_ANY_PERCENT, ENTRY_PLAZA | ENTRY_PB_OVERRIDE, slot)

// The display order is also the menu's group order. Shine ids are retail
// TShine event ids; unlike scene ids they distinguish episode, bonus and
// 100-coin Shines that can all be collected in the same stage.
constexpr Entry kEntries[] = {
#include "iling_entries.inc"
};

#undef SHINE
#undef SHINE_SET
#undef SHINE_SET_ALIAS
#undef SHINE_CLEAR
#undef SHINE_CLEAR_SET
#undef SHINE_FULL
#undef PINNA_EYG
#undef SHINE_INSIDE
#undef SHINE_SECRET
#undef PLAZA
#undef RAW
#undef ENTRY_STATE

const int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);
constexpr u8 kGroupFirst[GROUP_COUNT] = {
    0, 13, 25, 38, 52, 65, 78, 90, 92, 94, 110
};
const int kGeneratedLabelCount = 90;
const int kRegularLabelSize = 18;
// Fixed-width names and computed suffix offsets cost less than lookup tables.
constexpr char kRegularGroupNames[] =
    "Bianco\0Ricco\0\0Gelato\0Pinna\0\0Sirena\0Noki\0\0\0Pianta";
constexpr char kRegularShortGroupNames[] =
    "BH\0RH\0GB\0PP\0SB\0NB\0PV";
constexpr char kHundredGroupLetters[] = "BRGPSNV";
constexpr char kMenuGroupNames[] =
    "BIANCO\0RICCO\0GELATO\0PINNA\0SIRENA\0NOKI\0PIANTA\0"
    "AIRSTRIP\0CORONA\0DELFINO\0ANY PERCENT";
constexpr u8 kMenuGroupOffsets[] = {0, 7, 13, 20, 26, 33, 38, 45, 54, 61, 69};
static_assert(sizeof(kMenuGroupOffsets) == GROUP_COUNT,
              "IL menu group labels changed");
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
#define PINNA_EYG(label, slot) \
    static_assert(sameLabel(label, "Pinna Park EYG"), \
                  "Pinna EYG label changed");
#define SHINE_INSIDE(label, area, episode, parent, id, group, slot) \
    static_assert((slot) == 52 || (slot) == 122 || (slot) == 123, \
                  "inside IL slot changed");
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
#undef PINNA_EYG
#undef SHINE_INSIDE
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
#define PINNA_EYG(label, slot) ENTRY_LABEL(GROUP_PINNA, label)
#define SHINE_INSIDE(label, area, episode, parent, id, group, slot) \
    ENTRY_LABEL(group, label)
#define SHINE_SECRET(label, area, episode, parent, id, group, slot) ENTRY_LABEL(group, label)
#define PLAZA(label, source, scenario, finish, result, slot) label "\0"
#define RAW(label, area, episode, parent, finish, result, group, flags, prerequisite) \
    ENTRY_LABEL(group, label)

const char kLiteralEntryLabels[] =
#include "iling_entries.inc"
    ;

constexpr char kLiteralShortLabels[] =
    "AS1\0ASR\0CM\0BOW\0DCS\0PAC\0DSL\0LIL\0GRS\0LHS\0BG1\0BG2\0LB\0RB\0"
    "CHK\0SG\0D100\0UB\0BS\0GB\0BP\0DSM\0TS\0GP\0PE\0HS\0RE\0B2E\0"
    "SE\0NE\0CE";

constexpr int packedLabelCount(const char *pool, u32 bytes) {
    int count = 1;
    for (u32 i = 0; i + 1 < bytes; i++) {
        if (!pool[i]) count++;
    }
    return count;
}

#undef SHINE
#undef SHINE_SET
#undef SHINE_SET_ALIAS
#undef SHINE_CLEAR
#undef SHINE_CLEAR_SET
#undef SHINE_FULL
#undef PINNA_EYG
#undef SHINE_INSIDE
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
const int kPbSlotCount = 125;
const int kEntryGelato4Inside = 31;
const int kEntryPinnaEyg = 46;
const int kEntryNoki3Inside = 67;
const int kEntryNoki4Eel = 69;
const int kEntryCorona = 92;
const int kEntryBowser = 93;
static_assert(kEntries[kEntryGelato4Inside].prerequisite == 123,
              "Gelato 4 Inside entry moved");
static_assert(kEntries[kEntryPinnaEyg].prerequisite == 121,
              "Pinna EYG entry moved");
static_assert(kEntries[kEntryNoki3Inside].start.area == 0x2C &&
              kEntries[kEntryNoki3Inside].result == 52,
              "Noki 3 Inside entry moved");
static_assert(kEntries[kEntryNoki4Eel].prerequisite == 122,
              "Noki 4 Eel entry moved");
static_assert(kEntries[kEntryCorona].result == 119 &&
              kEntries[kEntryBowser].result == 124,
              "Corona entries moved");
const u8 kAnyPercentTheorySlots[] = {
    1, 2, 3, 4, 5, 6,                  // Bianco 2-7
    27, 26,                            // Gelato 8, 7
    10, 11, 12, 13, 14, 15, 16,       // Ricco 1-7
    60, 65, 62, 61, 64, 63, 66,       // Pianta 1-7
    30, 31, 32, 33, 121, 36,           // Pinna 1-4, EYG, 7
    40, 41, 42, 43, 44, 45, 46,       // Sirena 1-7
    50, 51, 52, 53, 54, 55, 56,       // Noki 1-7
    119,                               // Corona
    80, 81, 82, 83, 84, 85, 108, 109, 120, 110, 111,
    86,                                // Airstrip 1
};
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
static_assert(kEntryCount == 121, "ILing entry count changed");
static_assert(kEntryCount <= 0x100, "recent IL entry index exceeds u8");
static_assert(sizeof(kAnyPercentTheorySlots) == 55,
              "Any% theory route changed");
static_assert(kGroupFirst[GROUP_AIRSTRIP] == kGeneratedLabelCount,
              "generated IL label range changed");
static_assert(packedLabelCount(kLiteralShortLabels,
                              sizeof(kLiteralShortLabels)) ==
                  kEntryCount - kGeneratedLabelCount,
              "short IL label table changed");

struct AttemptState {
    bool running;
    bool ready;
    bool carryRestorePending;
    bool transitionPending;
    bool recordsEligible;
    bool nativeIgt;
    bool havePlazaStoryFlags;
    u8 plazaStoryFlags;
    u8 overlayCount;
    OverlayFlag overlayFlags[kOverlayFlagCount];
    LevelWarp::Dest start;
    u8 finish;
    int selectedEntry;
    u32 serial;
};

#if IS_EMULATOR
#define sPbProfiles (*reinterpret_cast<s32 (*)[SUSAMUNE_ILING_PROFILE_COUNT] \
                                            [SUSAMUNE_ILING_PB_MAX_SLOTS]>( \
    SUSAMUNE_DOLPHIN_PB_LIVE_PPC_BASE))
static_assert(SUSAMUNE_ILING_PROFILE_COUNT * SUSAMUNE_ILING_PB_MAX_SLOTS *
                  sizeof(s32) <= SUSAMUNE_DOLPHIN_PB_LIVE_SIZE,
              "Dolphin live PB mirror exceeds its MEM2 window");
static_assert((SUSAMUNE_DOLPHIN_PB_LIVE_PPC_BASE & 31u) == 0,
              "Dolphin live PB mirror is not cache-line aligned");
#else
#define sPbProfiles (*reinterpret_cast<s32 (*)[SUSAMUNE_ILING_PROFILE_COUNT] \
                                            [SUSAMUNE_ILING_PB_MAX_SLOTS]>( \
    SUSAMUNE_MEM2_PB_LIVE_PPC_BASE))
static_assert(SUSAMUNE_ILING_PROFILE_COUNT * SUSAMUNE_ILING_PB_MAX_SLOTS *
                  sizeof(s32) <= SUSAMUNE_MEM2_PB_LIVE_SIZE,
              "live PB mirror exceeds its MEM2 window");
#endif

struct RuntimeState {
    char customProfileNames[SUSAMUNE_ILING_CUSTOM_NAME_COUNT]
                           [SUSAMUNE_ILING_PROFILE_NAME_SIZE];
    char bannerText[32];
    s32 recentQf[kRecentCount];
    u32 pbSaveSeq;
    u32 pbSaveWaitFrames;
    u32 pbRetryFrames;
    int bannerFrames;
    int fanfareDelay;
    u8 achievementChimeBlockFrames;
    u8 recentEntry[kRecentCount];
    u8 activePbProfile;
    u8 recentCount;
    u8 recentNext;
    bool haveSetupShineCount;
    u8 setupShineCount;
    bool haveSetupMovieFlag;
    bool setupMovieFlag;
    bool rocketEquipPending;
    bool temporaryRocketActive;
    bool bowserNozzleShieldPending;
    bool bowserNozzleShieldActive;
    u8 savedSecondNozzle;
    s32 savedSecondNozzleFlag;
    s32 savedBowserNozzleFlag;
    bool pbBackend;
    bool pbDirty;
    bool pbPending;
    bool pbTimeoutNotified;
    bool haveSavedAttempt;
};

struct ILingRuntime {
    AttemptState attempt;
    AttemptState savedAttempt;
    RuntimeState state;
    char generatedLabel[kRegularLabelSize];
    char generatedShortLabel[6];
};

#define sILingRuntime (*reinterpret_cast<ILingRuntime *>( \
    SUSAMUNE_MEM2_ILING_RUNTIME_PPC_BASE))
static_assert(sizeof(ILingRuntime) <= SUSAMUNE_ILING_RUNTIME_SIZE,
              "IL runtime exceeds its MEM2 window");

#define sAttemptState sILingRuntime.attempt
#define sSavedAttemptState sILingRuntime.savedAttempt
#define sRuntime sILingRuntime.state
#define sGeneratedLabel sILingRuntime.generatedLabel
#define sGeneratedShortLabel sILingRuntime.generatedShortLabel
#define sRunning sAttemptState.running
#define sAttemptReady sAttemptState.ready
#define sCarryRestorePending sAttemptState.carryRestorePending
#define sTransitionPending sAttemptState.transitionPending
#define sRecordsEligible sAttemptState.recordsEligible
#define sNativeIgt sAttemptState.nativeIgt
#define sHavePlazaStoryFlags sAttemptState.havePlazaStoryFlags
#define sPlazaStoryFlags sAttemptState.plazaStoryFlags
#define sOverlayCount sAttemptState.overlayCount
#define sOverlayFlags sAttemptState.overlayFlags
#define sAttemptStart sAttemptState.start
#define sFinishKind sAttemptState.finish
#define sSelectedEntry sAttemptState.selectedEntry
#define sAttemptSerial sAttemptState.serial
#define sCustomPbProfileNames sRuntime.customProfileNames
#define sBannerText sRuntime.bannerText
#define sRecentQf sRuntime.recentQf
#define sPbSaveSeq sRuntime.pbSaveSeq
#define sPbSaveWaitFrames sRuntime.pbSaveWaitFrames
#define sPbRetryFrames sRuntime.pbRetryFrames
#define sBannerFrames sRuntime.bannerFrames
#define sFanfareDelay sRuntime.fanfareDelay
#define sAchievementChimeBlockFrames sRuntime.achievementChimeBlockFrames
#define sRecentEntry sRuntime.recentEntry
#define sActivePbProfile sRuntime.activePbProfile
#define sRecentCount sRuntime.recentCount
#define sRecentNext sRuntime.recentNext
#define sHaveSetupShineCount sRuntime.haveSetupShineCount
#define sSetupShineCount sRuntime.setupShineCount
#define sHaveSetupMovieFlag sRuntime.haveSetupMovieFlag
#define sSetupMovieFlag sRuntime.setupMovieFlag
#define sRocketEquipPending sRuntime.rocketEquipPending
#define sTemporaryRocketActive sRuntime.temporaryRocketActive
#define sBowserNozzleShieldPending sRuntime.bowserNozzleShieldPending
#define sBowserNozzleShieldActive sRuntime.bowserNozzleShieldActive
#define sSavedSecondNozzle sRuntime.savedSecondNozzle
#define sSavedSecondNozzleFlag sRuntime.savedSecondNozzleFlag
#define sSavedBowserNozzleFlag sRuntime.savedBowserNozzleFlag
#define sPbBackend sRuntime.pbBackend
#define sPbDirty sRuntime.pbDirty
#define sPbPending sRuntime.pbPending
#define sPbTimeoutNotified sRuntime.pbTimeoutNotified
#define sHaveSavedAttempt sRuntime.haveSavedAttempt

const char *defaultCustomProfileName(int index) {
    return index == 0 ? "Custom 1" : "Custom 2";
}

void copyProfileName(char *out, const char *name, const char *fallback) {
    int n = 0;
    if (name) {
        while (n + 1 < SUSAMUNE_ILING_PROFILE_NAME_SIZE && name[n] >= ' ' &&
               name[n] <= '~') {
            out[n] = name[n];
            n++;
        }
    }
    if (n == 0) {
        while (n + 1 < SUSAMUNE_ILING_PROFILE_NAME_SIZE && fallback[n]) {
            out[n] = fallback[n];
            n++;
        }
    }
    out[n] = '\0';
    for (n++; n < SUSAMUNE_ILING_PROFILE_NAME_SIZE; n++) out[n] = '\0';
}

s32 *activePBs() { return sPbProfiles[sActivePbProfile]; }

void reconcileRecordsPBs() {
    Records::reconcilePBProfiles(
        &sPbProfiles[0][0], SUSAMUNE_ILING_PB_MAX_SLOTS,
        SUSAMUNE_ILING_PROFILE_COUNT, sActivePbProfile);
}

void resetPBProfiles() {
    sActivePbProfile = 0;
    memset(sPbProfiles, 0xff, sizeof(sPbProfiles));
    for (int i = 0; i < SUSAMUNE_ILING_CUSTOM_NAME_COUNT; i++) {
        copyProfileName(sCustomPbProfileNames[i], nullptr,
                        defaultCustomProfileName(i));
    }
}

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
    if (cfg->magic != SUSAMUNE_CFG_MAGIC ||
        cfg->version != SUSAMUNE_CFG_VERSION) {
        return;
    }

    volatile const SusamuneILingProfilesCfg *profiles = &cfg->ilingProfiles;
    if ((cfg->flags & SUSAMUNE_CFG_FLAG_ILING_PROFILES) &&
        profiles->magic == SUSAMUNE_ILING_PROFILE_MAGIC &&
        profiles->version == SUSAMUNE_ILING_PROFILE_VERSION &&
        profiles->profileCount == SUSAMUNE_ILING_PROFILE_COUNT &&
        profiles->slotCount <= SUSAMUNE_ILING_PB_MAX_SLOTS &&
        profiles->nameSize == SUSAMUNE_ILING_PROFILE_NAME_SIZE &&
        profiles->activeProfile < SUSAMUNE_ILING_PROFILE_COUNT) {
        for (int profile = 0; profile < SUSAMUNE_ILING_PROFILE_COUNT;
             profile++) {
            for (u16 slot = 0; slot < profiles->slotCount; slot++) {
                const s32 value = profiles->values[profile][slot];
                if (value >= 0 && value <= SUSAMUNE_ILING_PB_MAX_QF) {
                    sPbProfiles[profile][slot] = value;
                }
            }
        }
        for (int i = 0; i < SUSAMUNE_ILING_CUSTOM_NAME_COUNT; i++) {
            const volatile char *name = profiles->customNames[i];
            copyProfileName(sCustomPbProfileNames[i],
                            const_cast<const char *>(name),
                            defaultCustomProfileName(i));
        }
        sActivePbProfile = profiles->activeProfile;
        sPbSaveSeq = profiles->saveSeq;
        sPbBackend = true;
        return;
    }

    volatile const SusamuneILingPbCfg *pbs = &cfg->ilingPbs;
    if ((cfg->flags & SUSAMUNE_CFG_FLAG_ILING_PBS) &&
        pbs->magic == SUSAMUNE_ILING_PB_MAGIC &&
        pbs->version == SUSAMUNE_ILING_PB_VERSION &&
        pbs->count <= SUSAMUNE_ILING_PB_MAX_SLOTS) {
        for (u16 slot = 0; slot < pbs->count; slot++) {
            const s32 value = pbs->values[slot];
            if (value >= 0 && value <= SUSAMUNE_ILING_PB_MAX_QF) {
                sPbProfiles[0][slot] = value;
            }
        }
        sPbSaveSeq = pbs->saveSeq;
        sPbBackend = true;
        sPbDirty = true;
    }
}

void loadPBs() {
    resetPBBackend();

#if IS_EMULATOR
    SusamuneCfg *cfg = EmulatorPersistence::lock();
    if (cfg) {
        adoptPBs(cfg);
        EmulatorPersistence::unlock();
    }
#else
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    DCInvalidateRange((void *)cfg, 32);
    DCInvalidateRange((void *)&cfg->ilingPbs, sizeof(SusamuneILingPbCfg));
    DCInvalidateRange((void *)&cfg->ilingProfiles,
                      sizeof(SusamuneILingProfilesCfg));
    adoptPBs(cfg);
#endif
    reconcileRecordsPBs();
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
    volatile SusamuneILingProfilesCfg *profiles = &cfg->ilingProfiles;
#else
    volatile SusamuneILingProfilesCfg *profiles =
        &SUSAMUNE_CFG_PPC_PTR->ilingProfiles;
#endif
    memcpy((void *)profiles->values, sPbProfiles, sizeof(profiles->values));
    memcpy((void *)profiles->customNames, sCustomPbProfileNames,
           sizeof(profiles->customNames));
    profiles->magic = SUSAMUNE_ILING_PROFILE_MAGIC;
    profiles->version = SUSAMUNE_ILING_PROFILE_VERSION;
    profiles->profileCount = SUSAMUNE_ILING_PROFILE_COUNT;
    profiles->activeProfile = sActivePbProfile;
    profiles->slotCount = SUSAMUNE_ILING_PB_MAX_SLOTS;
    profiles->nameSize = SUSAMUNE_ILING_PROFILE_NAME_SIZE;
#if IS_EMULATOR
    sPbSaveSeq = EmulatorPersistence::commit();
#else
    DCStoreRange((void *)profiles->values,
                 sizeof(profiles->values) + sizeof(profiles->customNames));

    sPbSaveSeq++;
    profiles->saveSeq = sPbSaveSeq;
    DCStoreRange((void *)profiles, 32);
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
        volatile SusamuneILingProfilesCfg *profiles =
            &SUSAMUNE_CFG_PPC_PTR->ilingProfiles;
        DCInvalidateRange((void *)&profiles->ackSeq, 32);
        if (profiles->ackSeq == sPbSaveSeq) {
            sPbPending = false;
            sPbTimeoutNotified = false;
            sPbSaveWaitFrames = 0;
            if (profiles->status != 0 && gMenu) {
                char error[40];
                snprintf(error, sizeof(error), "PB save failed: %u",
                         profiles->status);
                gMenu->toast(error);
            }
            if (profiles->status != 0) {
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
    return entry.result == 119 || entry.result == 124
               ? FINISH_BOWSER
               : entry.flags >> 6;
}

int pbSlot(int entry) {
    const Entry &item = kEntries[entry];
    return item.flags & ENTRY_PB_OVERRIDE ? item.prerequisite : item.result;
}

bool acceptsAnySelectedOrigin(const Entry &item) {
    return item.result == 30 || item.result == 86;
}

bool sameDest(const LevelWarp::Dest &a, const LevelWarp::Dest &b) {
    return a.area == b.area && a.episode == b.episode &&
           a.gameInt3 == b.gameInt3;
}

bool acceptsSkipOrigin(const Entry &item) {
    const bool hidden = item.result == 29 || item.result == 59 ||
                        item.result == 69;
    const bool hundred = item.result >= 100 && item.result <= 107;
    if ((hidden || hundred) && sAttemptStart.area == item.start.area) {
        return true;
    }
    if (item.result == 27 && item.start.area == 4 && item.start.episode == 7) {
        return sAttemptStart.area == 4 && sAttemptStart.episode == 0;
    }
    if (item.result == 1 && item.start.area == 2 && item.start.episode == 1) {
        return sAttemptStart.area == 2 && sAttemptStart.episode == 0;
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
    if (validEntry(sSelectedEntry)) {
        const Entry &selected = kEntries[sSelectedEntry];
        if (acceptsAnySelectedOrigin(selected) &&
            entryFinish(selected) == FINISH_SHINE &&
            selected.result == result) {
            return sSelectedEntry;
        }
    }
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
    if (sTemporaryRocketActive) {
        if (TFlagManager::smInstance) {
            TFlagManager::smInstance->setFlag(0x40004,
                                              sSavedSecondNozzleFlag);
        }
        if (gpMarDirector &&
            gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING &&
            gpMarioOriginal && gpMarioOriginal->mFludd) {
            gpMarioOriginal->mFludd->mSecondNozzle = sSavedSecondNozzle;
        }
        sTemporaryRocketActive = false;
    }
    sRunning = false;
    sAttemptReady = false;
    sTransitionPending = false;
    sRecordsEligible = false;
    sNativeIgt = false;
    sSelectedEntry = -1;
    sRocketEquipPending = false;
    Records::onILAttemptEnded();
}

void armAttempt(const Entry &entry, int selected) {
    sRunning = true;
    sAttemptReady = false;
    sTransitionPending = false;
    sAttemptStart = entry.start;
    sFinishKind = entryFinish(entry);
    sSelectedEntry = selected;
    sAttemptSerial = gQFTTimer.attemptSerial();
    sRecordsEligible = !gSettings.getBool(SETTING_STAGE_INTRO_SKIP);
    sNativeIgt = false;
    Records::onILAttemptStarted((int)(&entry - kEntries));
    if (!sRecordsEligible) Records::invalidateAttempt();
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

bool pbRecordingEnabled() {
    return gSettings.getBool(SETTING_ILING_RECORDING) &&
           !gSettings.getBool(SETTING_STAGE_INTRO_SKIP);
}

bool attemptPBRecordingEnabled() {
    return gSettings.getBool(SETTING_ILING_RECORDING) && sRecordsEligible;
}

void recordPB(int entry, s32 qf) {
    if (!attemptPBRecordingEnabled()) {
        return;
    }

    const int slot = pbSlot(entry);
    s32 *pbs = activePBs();
    if (pbs[slot] >= 0 && qf >= pbs[slot]) {
        return;
    }

    const s32 previous = pbs[slot];
    pbs[slot] = qf;
    Records::onPBAccepted(entry, sActivePbProfile);
    reconcileRecordsPBs();
    markPBsDirty();
    if (gSettings.getBool(SETTING_ILING_POPUP)) {
        char time[20];
        char delta[20];
        ILing::formatTime(qf, time, sizeof(time));
        if (previous < 0) {
            snprintf(sBannerText, sizeof(sBannerText), "NEW PB: %s --", time);
        } else {
            formatDelta(previous - qf, delta, sizeof(delta));
            snprintf(sBannerText, sizeof(sBannerText), "NEW PB: %s %s", time, delta);
        }
        sBannerFrames = kBannerFrames;
    } else {
        sBannerFrames = 0;
    }

    if (!gSettings.getBool(SETTING_ILING_FANFARE)) {
        sFanfareDelay = 0;
    } else if (entryFinish(kEntries[entry]) == FINISH_SHINE) {
        sAchievementChimeBlockFrames = kBannerFrames;
        // The retail Shine Get request is submitted during this frame.
        sFanfareDelay = kShineFanfareDelay;
    } else {
        sAchievementChimeBlockFrames = kBannerFrames;
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
    const s32 igtCentis = sNativeIgt && gpMarDirector &&
                                  gpMarDirector->mGCConsole
                              ? gpMarDirector->mGCConsole->mTimerSecondsLeft
                              : -1;
    Records::onILResult(entry, (u8)pbSlot(entry), qf, igtCentis,
                        sRecordsEligible);
    recordPB(entry, qf);
}

}  // namespace

namespace ILing {

void formatTime(s32 qf, char *out, u32 size, const char *format) {
    const s32 millis = (qf * 1001) / 120;
    snprintf(out, size, format, (int)(millis / 60000),
             (int)((millis / 1000) % 60), (int)(millis % 1000));
}

void init() {
    memset(&sILingRuntime, 0, sizeof(sILingRuntime));
    resetPBProfiles();
    sSelectedEntry = -1;
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
        if (entry == kEntryPinnaEyg) return "Pinna Park EYG";
        if (entry == kEntryNoki3Inside) return "Noki 3";
        if (entry == kEntryNoki4Eel) return "Noki 4 (Eel Only)";
        if (entry == kEntryGelato4Inside) return "Gelato 4 (Inside)";
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
        sprintf(sGeneratedLabel, kRegularLabelFormats + formatOffset,
                regularGroupName(group), item.start.gameInt3 + 1, suffix);
        return sGeneratedLabel;
    }

    return PackedText::at(kLiteralEntryLabels, entry - kGeneratedLabelCount);
}

const char *shortLabel(int entry) {
    if (entry >= kGeneratedLabelCount) {
        return PackedText::at(kLiteralShortLabels,
                              entry - kGeneratedLabelCount);
    }

    const Entry &item = kEntries[entry];
    if (entry == kEntryPinnaEyg) return "PEYG";
    if (entry == kEntryNoki3Inside) return "NB3";
    if (entry == kEntryNoki4Eel) return "NB4i";
    if (entry == kEntryGelato4Inside) return "GB4i";
    const bool hundred = item.result >= 100;
    const int group = hundred ? item.result - 100 : item.result / 10;
    const char *prefix = kRegularShortGroupNames + group * 3;
    const u8 flags = item.flags & ENTRY_FLAG_MASK;
    if (hundred) {
        const bool longPrefix = group == GROUP_PINNA || group == GROUP_PIANTA;
        sGeneratedShortLabel[0] =
            longPrefix ? prefix[0] : kHundredGroupLetters[group];
        sGeneratedShortLabel[1] = longPrefix ? prefix[1] : '1';
        sGeneratedShortLabel[2] = longPrefix ? '1' : '0';
        sGeneratedShortLabel[3] = '0';
        sGeneratedShortLabel[4] = longPrefix ? '0' : '\0';
        sGeneratedShortLabel[5] = '\0';
    } else if (flags == ENTRY_NONE && item.result - group * 10 == 9) {
        sGeneratedShortLabel[0] = prefix[0];
        sGeneratedShortLabel[1] = 'H';
        sGeneratedShortLabel[2] = '\0';
    } else {
        sGeneratedShortLabel[0] = prefix[0];
        sGeneratedShortLabel[1] = prefix[1];
        sGeneratedShortLabel[2] = '1' + item.start.gameInt3;
        const int suffix = regularSuffixType(flags);
        sGeneratedShortLabel[3] = suffix == 2 ? 'F' : suffix == 3 ? 'S'
                                    : suffix ? 'R' : '\0';
        sGeneratedShortLabel[4] = '\0';
    }
    return sGeneratedShortLabel;
}

s32 pbQf(int entry) {
    return activePBs()[pbSlot(entry)];
}

bool anyPercentTheoryQf(s32 *out) {
    if (!out || sActivePbProfile != 0) return false;
    s32 total = 0;
    for (u32 i = 0; i < sizeof(kAnyPercentTheorySlots); i++) {
        const s32 qf = activePBs()[kAnyPercentTheorySlots[i]];
        if (qf < 0) return false;
        total += qf;
    }
    *out = total;
    return true;
}

int pbProfile() { return sActivePbProfile; }

const char *pbProfileName(int profile) {
    if (profile == 0) return "Any percent";
    if (profile == 1) return "120 Shines";
    if (profile >= 2 && profile < SUSAMUNE_ILING_PROFILE_COUNT) {
        return sCustomPbProfileNames[profile - 2];
    }
    return "";
}

bool pbProfileNameEditable(int profile) {
    return profile >= 2 && profile < SUSAMUNE_ILING_PROFILE_COUNT;
}

void cyclePbProfile(int direction) {
    sActivePbProfile = (u8)((sActivePbProfile +
        SUSAMUNE_ILING_PROFILE_COUNT + direction) %
        SUSAMUNE_ILING_PROFILE_COUNT);
    sBannerFrames = 0;
    reconcileRecordsPBs();
    markPBsDirty();
}

void setPbProfileName(int profile, const char *name) {
    if (!pbProfileNameEditable(profile)) return;
    copyProfileName(sCustomPbProfileNames[profile - 2], name,
                    defaultCustomProfileName(profile - 2));
    markPBsDirty();
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
    for (int group = 0; group < GROUP_COUNT; group++) {
        if (entry == kGroupFirst[group]) {
            return true;
        }
    }
    return false;
}

const char *groupName(int entry) {
    int group = 0;
    while (group + 1 < GROUP_COUNT && entry >= kGroupFirst[group + 1]) group++;
    return kMenuGroupNames + kMenuGroupOffsets[group];
}

int activeParentEpisode(u8 parentArea) {
    if (!sRunning) return -1;
    const u8 startParent = LevelWarp::parentArea(sAttemptStart.area);
    if (sAttemptStart.area != parentArea && startParent != parentArea)
        return -1;
    return sAttemptStart.gameInt3;
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
    activePBs()[slot] = -1;
    markPBsDirty();
    sBannerFrames = 0;
    reconcileRecordsPBs();
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

    if (sBowserNozzleShieldActive) {
        if (TFlagManager::smInstance) {
            TFlagManager::smInstance->setFlag(0x40004,
                                              sSavedBowserNozzleFlag);
        }
        sBowserNozzleShieldActive = false;
    }
    sBowserNozzleShieldPending = false;

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
        if (validEntry(sSelectedEntry) &&
            acceptsAnySelectedOrigin(kEntries[sSelectedEntry])) {
            // Their cutscene hops do not restart QFT, so keep the selected
            // attempt eligible across every intermediate scene.
            sAttemptReady = true;
            sAttemptSerial = gQFTTimer.attemptSerial();
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

    sRocketEquipPending = atStart && validEntry(sSelectedEntry) &&
        sSelectedEntry == kEntryGelato4Inside;
    sBowserNozzleShieldPending = atStart && validEntry(sSelectedEntry) &&
        sSelectedEntry == kEntryBowser;
}

void update() {
    servicePBSave();

    if (sRunning && sRecordsEligible &&
        gSettings.getBool(SETTING_STAGE_INTRO_SKIP)) {
        sRecordsEligible = false;
        Records::invalidateAttempt();
    }
    if (sRunning && gpMarDirector && gpMarDirector->mGCConsole &&
        gpMarDirector->mGCConsole->mIsTimerMoving) {
        sNativeIgt = true;
    }

    if (sBowserNozzleShieldPending && gpMarDirector &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING &&
        TFlagManager::smInstance) {
        sSavedBowserNozzleFlag =
            TFlagManager::smInstance->getFlag(0x40004);
        sBowserNozzleShieldActive = true;
        sBowserNozzleShieldPending = false;
    }

    if (sRocketEquipPending && gpMarDirector &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING &&
        gpMarioOriginal && gpMarioOriginal->mFludd &&
        TFlagManager::smInstance) {
        TWaterGun *fludd = gpMarioOriginal->mFludd;
        sSavedSecondNozzle = fludd->mSecondNozzle;
        sSavedSecondNozzleFlag =
            TFlagManager::smInstance->getFlag(0x40004);
        sTemporaryRocketActive = true;

        fludd->changeNozzle(TWaterGun::Spray, true);
        fludd->mSecondNozzle = TWaterGun::Rocket;
        fludd->mCurrentWater =
            fludd->mNozzleList[TWaterGun::Spray]->mEmitParams.mAmountMax.get();
        sRocketEquipPending = false;
    }

    // Sunshine saves the live backup nozzle into 0x40004 while departing.
    // Keep the IL-only Rocket from escaping into the next stage.
    if (sTemporaryRocketActive && TFlagManager::smInstance) {
        TFlagManager::smInstance->setFlag(0x40004,
                                          sSavedSecondNozzleFlag);
    }
    if (sBowserNozzleShieldActive && TFlagManager::smInstance) {
        TFlagManager::smInstance->setFlag(0x40004,
                                          sSavedBowserNozzleFlag);
    }

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

    if (!pbRecordingEnabled()) {
        sFanfareDelay = 0;
        sBannerFrames = 0;
    } else if (!gSettings.getBool(SETTING_ILING_POPUP)) {
        sBannerFrames = 0;
    }

    if (sFanfareDelay > 0 && --sFanfareDelay == 0 &&
        gSettings.getBool(SETTING_ILING_FANFARE)) {
        MSBgm::stopBGM(BGM_GET_SHINE, 0);
        startPbFanfare();
    }

    if (sBannerFrames > 0) {
        sBannerFrames--;
    }
    if (sAchievementChimeBlockFrames > 0) {
        sAchievementChimeBlockFrames--;
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
        if (sceneMatches(scene, sAttemptStart) ||
            (validEntry(sSelectedEntry) &&
             acceptsAnySelectedOrigin(kEntries[sSelectedEntry]))) {
            // A same-scene reset keeps its explicitly selected Secret/Reds mode.
            sAttemptSerial = serial;
            sRecordsEligible =
                !gSettings.getBool(SETTING_STAGE_INTRO_SKIP);
            sNativeIgt = false;
            const int entry = validEntry(sSelectedEntry)
                                  ? sSelectedEntry
                                  : entryForStartScene(scene);
            Records::onILAttemptStarted(entry);
            if (!sRecordsEligible) Records::invalidateAttempt();
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
        completedEntry = validEntry(sSelectedEntry)
                             ? sSelectedEntry
                             : sAttemptStart.area == TGameSequence::AREA_CORONABOSS
                                   ? kEntryBowser
                                   : kEntryCorona;
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
    sAchievementChimeBlockFrames = 0;
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

bool achievementChimeBlocked() {
    return sAchievementChimeBlockFrames > 0;
}

static void drawRecent(Menu *menu, bool preview) {
    if (!menu || (!preview && (!gSettings.getBool(SETTING_ILING_RECENT) ||
                              sRecentCount == 0))) {
        return;
    }

    const CreationStyle &style = gCreationExtras.recentIlStyle();
    const int scale = style.scale;
    const int x = style.x;
    const int y = style.y;
    const bool shortNames = gSettings.getBool(SETTING_ILING_SHORT_NAMES);
    const int padding = style.padding == 0xff ? 0 : style.padding;
    const int rows = sRecentCount ? sRecentCount : (preview ? kRecentCount : 0);
    const int contentW = ((shortNames ? 156 : 230) * scale + 50) / 100;
    const int w = contentW + padding * 2;
    const int lineH = (15 * scale + 50) / 100;
    const int headerH = (22 * scale + 50) / 100;
    const int h = headerH + lineH * rows;
    const int textSize = (12 * scale + 50) / 100;
    const int textX = x + padding;
    const u8 *rgb = gCreationExtras.recentIlTextRgb();
    const int brightness = style.textBrightness;
    u8 litRgb[3];
    for (int channel = 0; channel < 3; channel++) {
        const int value = (int)rgb[channel] * brightness / 100;
        litRgb[channel] = (u8)(value > 255 ? 255 : value);
    }
    const Color textColor(litRgb[0], litRgb[1], litRgb[2], style.textA);
    if (rows) {
        if (style.padding != 0xff) {
            menu->fillBox(x, y, w, h,
                          Color(style.bgR, style.bgG, style.bgB, style.bgA));
            menu->fillBox(x, y, (3 * scale + 50) / 100, h,
                          Color(80, 180, 255, style.bgA));
        }
        menu->drawText("Recent ILs", textX,
                       y + (5 * scale + 50) / 100, textSize, textSize,
                       textColor);

        static const char kPreviewShortNames[] =
            "BH3\0RH2R\0GB1S\0PP6F\0B100";
        static const char kPreviewLongNames[] =
            "Bianco 3\0Ricco 2 Reds\0Gelato 1 Secret\0"
            "Pinna 6 (Full)\0Bianco 100 (E2)";
        for (int row = 0; row < rows; row++) {
            char time[20];
            const char *name;
            if (sRecentCount) {
                int index = sRecentNext - 1 - row;
                if (index < 0) index += kRecentCount;
                formatTime(sRecentQf[index], time, sizeof(time));
                name = shortNames ? shortLabel(sRecentEntry[index])
                                  : label(sRecentEntry[index]);
            } else {
                snprintf(time, sizeof(time), "1:%02d.%03d", 12 + row,
                         345 + row * 111);
                name = PackedText::at(shortNames ? kPreviewShortNames
                                                 : kPreviewLongNames,
                                      row);
            }
            char line[32];
            snprintf(line, sizeof(line), "%s  %s", name, time);
            menu->drawText(line, textX, y + headerH + row * lineH,
                           textSize, textSize, textColor);
        }
    }
}

void drawRecentPreview(Menu *menu) {
    drawRecent(menu, true);
}

void draw(Menu *menu) {
    if (!menu || menu->shown()) {
        return;
    }

    drawRecent(menu, false);

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
