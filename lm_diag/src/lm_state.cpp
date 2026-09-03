#if defined(SUSAMUNE_VERSION_LMJ)

#include "lm_state.hxx"

#include "lm_crash.hxx"
#include "susamune/crash_report.h"
#include "susamune/mem2_map.h"
#include "susamune/mod_bin.h"

// The Kuribo build is deliberately freestanding. Clang may still lower a
// small aggregate operations to memcpy/memcmp at -Oz, so keep those runtime
// helpers inside the injected image rather than depending on a retail libc.
extern "C" void *memcpy(void *destination, const void *source, u32 size) {
    volatile u8 *out = static_cast<volatile u8 *>(destination);
    const volatile u8 *in = static_cast<const volatile u8 *>(source);
    for (u32 i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return destination;
}

extern "C" int memcmp(const void *left, const void *right, u32 size) {
    const volatile u8 *a = static_cast<const volatile u8 *>(left);
    const volatile u8 *b = static_cast<const volatile u8 *>(right);
    for (u32 i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            return static_cast<int>(a[i]) - static_cast<int>(b[i]);
        }
    }
    return 0;
}

namespace {

// Clean GLMJ01 retail anchors recovered from the Japanese DOL.
constexpr u32 kRootHeapGlobal = 0x804A0B90u;
constexpr u32 kSystemHeapGlobal = 0x804A0B94u;
constexpr u32 kGameHeapGlobal = 0x804A0B98u;
constexpr u32 kCurrentHeapGlobal = 0x804A1FF4u;
constexpr u32 kCurrentHeapGroupGlobal = 0x80498AE8u;
constexpr u32 kRandomStateGlobal = 0x804A0B30u;
constexpr u32 kSceneValueGlobal = 0x804A0C20u;
constexpr u32 kMapValueGlobal = 0x804A0C48u;
constexpr u32 kCurrentSceneGlobal = 0x80498B18u;
constexpr u32 kMainLoopStateBase = 0x80398A40u;
constexpr u32 kMainLoopModeGlobal = kMainLoopStateBase;
constexpr u32 kMainLoopPendingSceneGlobal = kMainLoopStateBase + 4u;
constexpr u32 kMainLoopSceneGlobal = 0x804A0C20u;
constexpr u32 kMainDrawStateGlobal = 0x804A0C44u;
constexpr u32 kMainLoopExitGlobal = 0x804A0C28u;
constexpr u32 kGameModeGlobal = 0x804A17B0u;
constexpr u32 kGameModeCountGlobal = 0x804A17B4u;
constexpr u32 kMatrixArrayGlobal = 0x804A17B8u;
constexpr u32 kBooleanArrayGlobal = 0x804A17BCu;
constexpr u32 kMissionModeGlobal = 0x804A17C8u;
constexpr u32 kSimpleModelerGlobal = 0x804A17D0u;
constexpr u32 kMapColGlobal = 0x804A17D8u;
constexpr u32 kEnTypesManagerGlobal = 0x804A17E8u;
constexpr u32 kGameStaticRootGlobals[] = {
    kMatrixArrayGlobal,
    kBooleanArrayGlobal,
    kMissionModeGlobal,
    kSimpleModelerGlobal,
    kMapColGlobal,
    kEnTypesManagerGlobal,
};
constexpr u32 kVolumeListGlobal = 0x80494754u;
constexpr u32 kCurrentVolumeGlobal = 0x804A2038u;
constexpr u32 kCurrentDirIdGlobal = 0x804A2040u;
constexpr u32 kPadStatusGlobal = 0x80494778u;
constexpr u32 kDvdOutstandingGlobal = 0x80391D98u;
constexpr u32 kAramList0Global = 0x804946F4u;
constexpr u32 kAramList1Global = 0x80494724u;
constexpr u32 kCardBlockGlobal = 0x80495960u;
constexpr u32 kCardControlStride = 0x108u;
constexpr u32 kCardResultOffset = 0x04u;
constexpr u32 kCardResultBusy = 0xFFFFFFFFu;
constexpr u32 kAudioObjectGlobal = 0x804A03A8u;
constexpr u32 kAudioBasicGlobal = 0x804A1DD0u;
constexpr u32 kAudioStaticObject = 0x803E3CF8u;
constexpr u32 kAudioVtable = 0x80383FB0u;
constexpr u32 kAudioSceneOffset = 0x50u;
constexpr u32 kAudioBootstrapHandleOffset = 0x64u;
constexpr u32 kAudioBootstrapSoundId = 0x80000800u;

constexpr u32 kDvdBusyPredicateAddr = 0x80006A5Cu;
constexpr u32 kExpHeapCheckAddr = 0x801CA61Cu;
constexpr u32 kDCInvalidateRangeAddr = 0x801D5DF4u;
constexpr u32 kDCStoreRangeAddr = 0x801D5E58u;
constexpr u32 kOSDisableInterruptsAddr = 0x801D85B0u;
constexpr u32 kOSRestoreInterruptsAddr = 0x801D85D8u;
constexpr u32 kOSDisableSchedulerAddr = 0x801DAE98u;
constexpr u32 kOSEnableSchedulerAddr = 0x801DAED8u;
constexpr u32 kGXInvalidateTexAllAddr = 0x801F1C10u;
constexpr u32 kAudioChangeSoundSceneAddr = 0x8018D4E4u;

constexpr u32 kExpHeapVtable = 0x8038886Cu;
constexpr u32 kMemArchiveVtable = 0x80388D5Cu;
constexpr u32 kRarcMagic = 0x52415243u;  // 'RARC'
constexpr u32 kMem1Start = 0x80000000u;
constexpr u32 kMem1End = 0x81800000u;
constexpr u32 kSnapshotBase = SUSAMUNE_MEM2_SNAPSHOT_PPC_BASE;
constexpr u32 kSnapshotCapacity = SUSAMUNE_MEM2_SNAPSHOT_SIZE;
constexpr u32 kSnapshotMagic = 0x4C4D5354u;  // 'LMST'
constexpr u32 kSnapshotVersion = 6u;
constexpr u32 kHeaderSize = 0x100u;
constexpr u32 kHeapMetadataStart = 0x3Cu;
constexpr u32 kHeapMetadataEnd = 0x84u;
constexpr u32 kExpHeapAlignment = 16u;
constexpr u32 kHeapModeOffset = 0x68u;
constexpr u32 kHeapGroupOffset = 0x69u;
constexpr u32 kHeapMetadataSize = kHeapMetadataEnd - kHeapMetadataStart;
constexpr u32 kHeapMetadataOffset = kHeaderSize;
// LM's camera/viewport object and its four scalar draw-state words live in
// BSS below the game-static window. Stop before the following live display
// object, which owns boot-allocated double-buffer pointers.
constexpr u32 kRendererStateStart = 0x80398770u;
constexpr u32 kRendererStateEnd = 0x803989E0u;
constexpr u32 kInGameFlagsBase = 0x803C7CA0u;
constexpr u32 kInGameFlagsOffset = 0x659u;
constexpr u32 kInGameFlagsSize = 0x20u;
// The grain nodes are game-heap allocations, but both circular-list sentinels
// live in these adjacent BSS managers and must rewind with their node links.
constexpr u32 kGrainManagerStateStart = 0x803CBAF0u;
constexpr u32 kGrainManagerStateEnd = 0x803CC460u;
constexpr u32 kMainLoopStateSize = 0x08u;
// Leave the live heap-group byte, fixed render-mode pointers, and sCurScene
// outside the copy. They are exact epoch gates, not state to rewind.
constexpr u32 kGameSdata0Start = 0x80498AF8u;
constexpr u32 kGameSdata0End = 0x80498B18u;
constexpr u32 kGameSdata1Start = 0x80498B20u;
constexpr u32 kGameSdata1End = 0x804A03A8u;
// 0x804A0BF8 is a live JUTGamePad pointer; 0x804A1D10 begins an audio list.
constexpr u32 kGameSbss0Start = 0x804A0C00u;
constexpr u32 kGameSbss0End = 0x804A0C90u;
// BootScene owns 0x804A0C90-0x804A0CB0, including live picture/archive
// pointers. MissionMode snapshots never need any part of that block.
constexpr u32 kGameSbss1Start = 0x804A0CB0u;
constexpr u32 kGameSbss1End = 0x804A1D10u;

struct StaticRange {
    u32 address;
    u32 size;
};

// These audited GLMJ01 ranges exclude identified live OS, JSystem, BootScene,
// and audio state. Only the first two words of lbl_80398A40 are scalars;
// +0x08 begins an OSMessageQueue.
constexpr StaticRange kStateStaticRanges[] = {
    {kRendererStateStart, kRendererStateEnd - kRendererStateStart},
    {kInGameFlagsBase + kInGameFlagsOffset, kInGameFlagsSize},
    {kGrainManagerStateStart,
     kGrainManagerStateEnd - kGrainManagerStateStart},
    {kMainLoopStateBase, kMainLoopStateSize},
    {kGameSdata0Start, kGameSdata0End - kGameSdata0Start},
    {kGameSdata1Start, kGameSdata1End - kGameSdata1Start},
    {kGameSbss0Start, kGameSbss0End - kGameSbss0Start},
    {kGameSbss1Start, kGameSbss1End - kGameSbss1Start},
};
constexpr u32 kStateStaticRangeCount =
    sizeof(kStateStaticRanges) / sizeof(kStateStaticRanges[0]);
constexpr u32 kStateStaticsOffset =
    kHeapMetadataOffset + kHeapMetadataSize;
constexpr u32 kStateStaticsSize =
    (kRendererStateEnd - kRendererStateStart) + kInGameFlagsSize +
    (kGrainManagerStateEnd - kGrainManagerStateStart) + kMainLoopStateSize +
    (kGameSdata0End - kGameSdata0Start) +
    (kGameSdata1End - kGameSdata1Start) +
    (kGameSbss0End - kGameSbss0Start) +
    (kGameSbss1End - kGameSbss1Start);
constexpr u32 kHeapDataOffset =
    (kStateStaticsOffset + kStateStaticsSize + 31u) & ~31u;
constexpr u16 kDPadLeft = 0x0001u;
constexpr u16 kDPadRight = 0x0002u;
constexpr u32 kRequiredStableFrames = 3u;
constexpr u32 kPostLoadTraceFrameLimit = 8u;
constexpr u32 kMaxVolumes = 32u;
constexpr u32 kVolumeNameBytes = 16u;
constexpr u32 kVolumeNameHashBytes = 32u;
constexpr u32 kVolumeNameValid = 1u << 0;
constexpr u32 kVolumeArchiveValid = 1u << 1;
constexpr u32 kVolumeRarcValid = 1u << 2;
constexpr u32 kVolumeMounted = 1u << 8;
constexpr u32 kVolumeModeShift = 16u;
constexpr u32 kVolumeDirectionShift = 20u;
constexpr u32 kVolumeOpen = 1u << 24;
constexpr u32 kVolumeObjectOwnerShift = 0u;
constexpr u32 kVolumeArchiveOwnerShift = 4u;
constexpr u32 kVolumeObjectLocationShift = 8u;
constexpr u32 kVolumeBackingLocationShift = 12u;
constexpr u32 kVolumeOwnerMask = 0xFu;

enum VolumeFault : u32 {
    kVolumeFaultNone = 0u,
    kVolumeFaultCapacity,
    kVolumeFaultEmpty,
    kVolumeFaultEndpoint,
    kVolumeFaultNode,
    kVolumeFaultList,
    kVolumeFaultObject,
    kVolumeFaultEmbeddedLink,
    kVolumeFaultPrevious,
    kVolumeFaultDuplicate,
    kVolumeFaultTail,
    kVolumeFaultEnd,
    kVolumeFaultChanged,
};

enum VolumeOwner : u32 {
    kVolumeOwnerOther = 0u,
    kVolumeOwnerGame = 1u,
    kVolumeOwnerSystem = 2u,
    kVolumeOwnerRoot = 3u,
};
constexpr u32 kEventStateSave = 0x100u;
constexpr u32 kEventStateLoad = 0x101u;
constexpr u32 kEventStateReject = 0x10Fu;
constexpr u32 kEventStateSavePhase = 0x110u;
constexpr u32 kEventStateLoadPhase = 0x111u;

typedef bool (*BoolFn)();
typedef bool (*HeapCheckFn)(void *);
typedef bool (*DisableInterruptsFn)();
typedef void (*RestoreInterruptsFn)(bool);
typedef s32 (*SchedulerFn)();
typedef void (*VoidFn)();
typedef void (*CacheRangeFn)(void *, u32);
typedef void (*AudioChangeSoundSceneFn)(void *, u32);

struct LiveIdentity {
    u32 heap;
    u32 heapStart;
    u32 heapEnd;
    u32 heapSize;
    u32 heapMode;
    u32 heapGroup;
    u32 heapFreeHead;
    u32 heapFreeTail;
    u32 heapUsedHead;
    u32 heapUsedTail;
    u32 rootHeap;
    u32 rootHeapStart;
    u32 rootHeapEnd;
    u32 rootHeapSize;
    u32 rootHeapMode;
    u32 rootHeapGroup;
    u32 rootFreeHead;
    u32 rootFreeTail;
    u32 rootUsedHead;
    u32 rootUsedTail;
    u32 systemHeap;
    u32 systemHeapStart;
    u32 systemHeapEnd;
    u32 systemHeapSize;
    u32 systemHeapMode;
    u32 systemHeapGroup;
    u32 systemFreeHead;
    u32 systemFreeTail;
    u32 systemUsedHead;
    u32 systemUsedTail;
    u32 currentHeap;
    u32 missionMode;
    u32 mapArchive;
    u32 volume[3];
    u32 mapValue;
    u32 sceneValue;
    u32 currentScene;
    u32 gameMode;
    u32 gameModeCount;
    u32 simpleModeler;
    u32 mapCol;
    u32 enTypesManager;
    u32 currentHeapGroup;
    u32 audioBasic;
    u32 audioScene;
    u32 mainLoopMode;
    u32 mainLoopPendingScene;
    u32 mainLoopScene;
    u32 mainDrawState;
    u32 mainLoopExit;
};

struct SnapshotHeader {
    u32 magic;
    u32 version;
    u32 headerSize;
    u32 gameId;
    u32 totalSize;
    u32 checksum;
    u32 generation;
    u32 heap;
    u32 heapStart;
    u32 heapEnd;
    u32 heapSize;
    u32 heapMetadataOffset;
    u32 heapMetadataSize;
    u32 stateStaticsOffset;
    u32 stateStaticsSize;
    u32 heapDataOffset;
    u32 heapDataSize;
    u32 rootHeap;
    u32 rootHeapStart;
    u32 rootHeapEnd;
    u32 rootHeapSize;
    u32 rootHeapMode;
    u32 rootHeapGroup;
    u32 rootFreeHead;
    u32 rootFreeTail;
    u32 rootUsedHead;
    u32 rootUsedTail;
    u32 systemHeap;
    u32 systemHeapStart;
    u32 systemHeapEnd;
    u32 systemHeapSize;
    u32 currentHeap;
    u32 missionMode;
    u32 mapArchive;
    u32 volume[3];
    u32 mapValue;
    u32 sceneValue;
    u32 currentScene;
    u32 gameMode;
    u32 gameModeCount;
    u32 heapMode;
    u32 heapGroup;
    u32 systemHeapMode;
    u32 systemHeapGroup;
    u32 systemFreeHead;
    u32 systemFreeTail;
    u32 systemUsedHead;
    u32 systemUsedTail;
    u32 currentHeapGroup;
    u32 randomState;
    u32 freeHead;
    u32 freeTail;
    u32 usedHead;
    u32 usedTail;
    u32 mainLoopMode;
    u32 mainLoopPendingScene;
    u32 mainDrawState;
    u32 simpleModeler;
    u32 mapCol;
    u32 enTypesManager;
    u32 audioBasic;
    u32 audioScene;
};

static_assert(sizeof(SnapshotHeader) == kHeaderSize,
              "LM snapshot header must remain one cache-aligned page");
static_assert(kSnapshotBase + kSnapshotCapacity ==
                  SUSAMUNE_MEM2_CFG_PPC_BASE,
              "LM state must end before the config/crash mailboxes");
static_assert((kHeapDataOffset & 31u) == 0,
              "LM heap payload must be cache-line aligned");
static_assert(kStateStaticsSize == 0x95A0u,
              "LM static manifest size drifted");
static_assert(kHeapDataOffset == 0x9700u,
              "LM static manifest packing drifted");
static_assert(kRendererStateEnd - kRendererStateStart == 0x270u,
              "LM renderer snapshot boundary drifted");
static_assert(kGrainManagerStateEnd - kGrainManagerStateStart == 0x970u,
              "LM grain-manager snapshot boundary drifted");
static_assert(kGameSdata0End == kCurrentSceneGlobal &&
                  kGameSdata1Start == kCurrentSceneGlobal + 8u,
              "LM sCurScene must remain an uncaptured epoch gate");
static_assert(kGameSdata1End == kAudioObjectGlobal,
              "LM game sdata must stop before live audio state");
static_assert(kGameSbss1End < kAudioBasicGlobal,
              "LM game sbss must stop before live audio state");
static_assert(kMainLoopSceneGlobal == kSceneValueGlobal,
              "LM loop scene must match the captured scene identity");
static_assert(kMainDrawStateGlobal >= kGameSbss0Start &&
                  kMainDrawStateGlobal + sizeof(u32) <= kGameSbss0End,
              "LM draw state must remain inside the captured game sbss");

struct FreezeState {
    bool interruptsWereEnabled;
    bool dmaWasEnabled;
};

enum class Gate : u32 {
    Ready,
    Boot,
    GameHeap,
    RootHeap,
    SystemHeap,
    Size,
    Distinct,
    SystemNest,
    GameNest,
    Overlap,
    CurrentHeap,
    MissionNull,
    MissionRange,
    ModeMismatch,
    ModeCount,
    GameRoot,
    Scene,
    LoopMode,
    LoopExit,
    LoopScene,
    DrawState,
    DvdPredicate,
    DvdCount,
    Aram0,
    Aram1,
    Card0,
    Card1,
    Audio,
};

struct EpochMismatch {
    u32 mask;
    u32 saved;
    u32 live;
};

struct VolumeDescriptor {
    u32 node;
    u32 object;
    u32 previous;
    u32 next;
    u32 vtable;
    u32 objectOwnerHeap;
    u32 archiveHeap;
    u32 namePointer;
    u32 nameHash;
    u32 type;
    u32 stateFlags;
    u32 mountCount;
    u32 mountSource;
    u32 archiveInfo;
    u32 archiveHeader;
    u32 archiveData;
    u32 fileLength;
    u32 dataLength;
    u32 ownerFlags;
    char name[kVolumeNameBytes];
    u32 contentSignature;
};

struct VolumeCensus {
    u32 generation;
    u32 valid;
    u32 fault;
    u32 count;
    u32 head;
    u32 tail;
    u32 currentVolume;
    u32 currentDirId;
    u32 signature;
    u32 stableFrames;
    VolumeDescriptor entries[kMaxVolumes];
};

struct VolumeDiff {
    u32 ready;
    u32 savedValid;
    u32 liveValid;
    u32 savedCount;
    u32 liveCount;
    u32 removedCount;
    u32 addedCount;
    u32 commonOrder;
    u32 headOnly;
    u32 currentChanged;
    u32 removedIndices[2];
    u32 addedIndices[2];
};

static_assert(sizeof(VolumeDescriptor) == 0x60u,
              "LM volume descriptor layout drifted");

LMState::Status sStatus = LMState::Status::Empty;
LiveIdentity sLastIdentity = {};
Gate sGate = Gate::Boot;
EpochMismatch sEpochMismatch = {};
VolumeCensus sSavedVolumeCensus = {};
VolumeCensus sLiveVolumeCensus = {};
VolumeDiff sVolumeDiff = {};
bool sHaveIdentity;
bool sSlotInitialized;
u32 sStableFrames;
u32 sSnapshotSize;
u32 sGeneration;
u16 sPreviousButtons;
u32 sGateValue;
u32 sPostLoadTraceState;
u32 sPostLoadTraceFrame;

void traceSavePhase(u32 phase, u32 detail) {
    LMCrash::note(kEventStateSavePhase, phase, detail);
    LMCrash::phase(SUSAMUNE_PHASE_ACTION_SAVE, phase, detail, sStableFrames);
}

void traceLoadPhase(u32 phase, u32 detail) {
    LMCrash::note(kEventStateLoadPhase, phase, detail);
    LMCrash::phase(SUSAMUNE_PHASE_ACTION_LOAD, phase, detail, sStableFrames);
}

void tracePostLoadPhase(u32 phase, u32 detail = 0u) {
    LMCrash::phase(SUSAMUNE_PHASE_ACTION_POST_LOAD, phase, detail,
                   sStableFrames);
}

bool gateFailure(Gate gate, u32 value, bool report) {
    if (report) {
        sGate = gate;
        sGateValue = value;
    }
    return false;
}

void gateReady(bool report) {
    if (report) {
        sGate = Gate::Ready;
        sGateValue = 0u;
    }
}

inline u32 readWord(u32 address) {
    return *reinterpret_cast<volatile u32 *>(address);
}

inline u16 readHalf(u32 address) {
    return *reinterpret_cast<volatile u16 *>(address);
}

inline u8 readByte(u32 address) {
    return *reinterpret_cast<volatile u8 *>(address);
}

inline void writeWord(u32 address, u32 value) {
    *reinterpret_cast<volatile u32 *>(address) = value;
}

inline bool isMem1Range(u32 address, u32 size) {
    return size <= kMem1End - kMem1Start && address >= kMem1Start &&
           address <= kMem1End - size && (address & 3u) == 0;
}

inline bool isMem1ByteRange(u32 address, u32 size) {
    return size <= kMem1End - kMem1Start && address >= kMem1Start &&
           address <= kMem1End - size;
}

bool isExpHeap(u32 heap) {
    if (!isMem1Range(heap, kHeapMetadataEnd) ||
        readWord(heap) != kExpHeapVtable) {
        return false;
    }
    const u32 start = readWord(heap + 0x30u);
    const u32 end = readWord(heap + 0x34u);
    const u32 size = readWord(heap + 0x38u);
    return start >= kMem1Start && start <= end && end <= kMem1End &&
           ((start | end) & (kExpHeapAlignment - 1u)) == 0u &&
           size == end - start;
}

bool rangeInside(u32 childStart, u32 childEnd, u32 parentStart,
                 u32 parentEnd) {
    return childStart >= parentStart && childStart < childEnd &&
           childEnd <= parentEnd;
}

bool validCurrentHeap(u32 currentHeap, const LiveIdentity &identity) {
    if (currentHeap == identity.rootHeap ||
        currentHeap == identity.systemHeap || currentHeap == identity.heap) {
        return true;
    }
    if (currentHeap < identity.heapStart ||
        currentHeap > identity.heapEnd - kHeapMetadataEnd ||
        (currentHeap & 3u) != 0u || !isExpHeap(currentHeap)) {
        return false;
    }
    return rangeInside(readWord(currentHeap + 0x30u),
                       readWord(currentHeap + 0x34u), identity.heapStart,
                       identity.heapEnd);
}

bool validAudioBootstrap(u32 basic) {
    if (!isMem1Range(basic, kAudioBootstrapHandleOffset + sizeof(u32))) {
        return false;
    }
    const u32 slot = basic + kAudioBootstrapHandleOffset;
    const u32 handle = readWord(slot);
    return isMem1Range(handle, 0x34u) &&
           readWord(handle + 8u) == kAudioBootstrapSoundId &&
           readWord(handle + 0x30u) == slot;
}

bool sameIdentity(const LiveIdentity &a, const LiveIdentity &b) {
    const u32 *left = reinterpret_cast<const u32 *>(&a);
    const u32 *right = reinterpret_cast<const u32 *>(&b);
    for (u32 i = 0; i < sizeof(LiveIdentity) / sizeof(u32); ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

bool buildIdentity(LiveIdentity *identity, bool report = false) {
    identity->heap = readWord(kGameHeapGlobal);
    identity->rootHeap = readWord(kRootHeapGlobal);
    identity->systemHeap = readWord(kSystemHeapGlobal);
    if (!isExpHeap(identity->heap)) {
        identity->heapStart = 0;
        identity->heapEnd = 0;
        identity->heapSize = 0;
        return gateFailure(Gate::GameHeap, identity->heap, report);
    }
    if (!isExpHeap(identity->rootHeap)) {
        return gateFailure(Gate::RootHeap, identity->rootHeap, report);
    }
    if (!isExpHeap(identity->systemHeap)) {
        return gateFailure(Gate::SystemHeap, identity->systemHeap, report);
    }
    identity->heapStart = readWord(identity->heap + 0x30u);
    identity->heapEnd = readWord(identity->heap + 0x34u);
    identity->heapSize = readWord(identity->heap + 0x38u);
    identity->heapMode = readByte(identity->heap + kHeapModeOffset);
    identity->heapGroup = readByte(identity->heap + kHeapGroupOffset);
    identity->heapFreeHead = readWord(identity->heap + 0x74u);
    identity->heapFreeTail = readWord(identity->heap + 0x78u);
    identity->heapUsedHead = readWord(identity->heap + 0x7Cu);
    identity->heapUsedTail = readWord(identity->heap + 0x80u);
    identity->rootHeapStart = readWord(identity->rootHeap + 0x30u);
    identity->rootHeapEnd = readWord(identity->rootHeap + 0x34u);
    identity->rootHeapSize = readWord(identity->rootHeap + 0x38u);
    identity->rootHeapMode = readByte(identity->rootHeap + kHeapModeOffset);
    identity->rootHeapGroup = readByte(identity->rootHeap + kHeapGroupOffset);
    identity->rootFreeHead = readWord(identity->rootHeap + 0x74u);
    identity->rootFreeTail = readWord(identity->rootHeap + 0x78u);
    identity->rootUsedHead = readWord(identity->rootHeap + 0x7Cu);
    identity->rootUsedTail = readWord(identity->rootHeap + 0x80u);
    identity->systemHeapStart = readWord(identity->systemHeap + 0x30u);
    identity->systemHeapEnd = readWord(identity->systemHeap + 0x34u);
    identity->systemHeapSize = readWord(identity->systemHeap + 0x38u);
    identity->systemHeapMode =
        readByte(identity->systemHeap + kHeapModeOffset);
    identity->systemHeapGroup =
        readByte(identity->systemHeap + kHeapGroupOffset);
    identity->systemFreeHead = readWord(identity->systemHeap + 0x74u);
    identity->systemFreeTail = readWord(identity->systemHeap + 0x78u);
    identity->systemUsedHead = readWord(identity->systemHeap + 0x7Cu);
    identity->systemUsedTail = readWord(identity->systemHeap + 0x80u);
    identity->currentHeap = readWord(kCurrentHeapGlobal);
    identity->missionMode = readWord(kMissionModeGlobal);
    identity->mapArchive =
        isMem1Range(identity->missionMode, 0x1Cu)
            ? readWord(identity->missionMode + 0x18u)
            : 0u;
    identity->volume[0] = readWord(kVolumeListGlobal);
    identity->volume[1] = readWord(kVolumeListGlobal + 4u);
    identity->volume[2] = readWord(kVolumeListGlobal + 8u);
    identity->mapValue = readWord(kMapValueGlobal);
    identity->sceneValue = readWord(kSceneValueGlobal);
    identity->currentScene = readWord(kCurrentSceneGlobal);
    identity->gameMode = readWord(kGameModeGlobal);
    identity->gameModeCount = readWord(kGameModeCountGlobal);
    identity->simpleModeler = readWord(kSimpleModelerGlobal);
    identity->mapCol = readWord(kMapColGlobal);
    identity->enTypesManager = readWord(kEnTypesManagerGlobal);
    identity->currentHeapGroup = readByte(kCurrentHeapGroupGlobal);
    identity->audioBasic = readWord(kAudioObjectGlobal);
    identity->audioScene = 0u;
    identity->mainLoopMode = readWord(kMainLoopModeGlobal);
    identity->mainLoopPendingScene =
        readWord(kMainLoopPendingSceneGlobal);
    identity->mainLoopScene = readWord(kMainLoopSceneGlobal);
    identity->mainDrawState = readWord(kMainDrawStateGlobal);
    identity->mainLoopExit = readWord(kMainLoopExitGlobal);

    const bool distinctHeaps = identity->rootHeap != identity->systemHeap &&
        identity->rootHeap != identity->heap &&
        identity->systemHeap != identity->heap;
    if (identity->heapSize > kSnapshotCapacity - kHeapDataOffset) {
        return gateFailure(Gate::Size, identity->heapSize, report);
    }
    if (!distinctHeaps) {
        return gateFailure(Gate::Distinct, identity->heap, report);
    }
    if (!rangeInside(identity->systemHeapStart, identity->systemHeapEnd,
                     identity->rootHeapStart, identity->rootHeapEnd)) {
        return gateFailure(Gate::SystemNest, identity->systemHeapStart,
                           report);
    }
    if (!rangeInside(identity->heapStart, identity->heapEnd,
                     identity->rootHeapStart, identity->rootHeapEnd)) {
        return gateFailure(Gate::GameNest, identity->heapStart, report);
    }
    if (identity->systemHeapEnd > identity->heapStart &&
        identity->heapEnd > identity->systemHeapStart) {
        return gateFailure(Gate::Overlap, identity->systemHeapEnd, report);
    }
    if (!validCurrentHeap(identity->currentHeap, *identity)) {
        return gateFailure(Gate::CurrentHeap, identity->currentHeap, report);
    }
    if (identity->missionMode == 0u) {
        return gateFailure(Gate::MissionNull, 0u, report);
    }
    if (identity->missionMode < identity->heapStart ||
        identity->missionMode > identity->heapEnd - 0x1Cu) {
        return gateFailure(Gate::MissionRange, identity->missionMode, report);
    }
    if (identity->gameMode != identity->missionMode) {
        return gateFailure(Gate::ModeMismatch, identity->gameMode, report);
    }
    if (identity->gameModeCount != 1u) {
        return gateFailure(Gate::ModeCount, identity->gameModeCount, report);
    }
    for (u32 i = 0;
         i < sizeof(kGameStaticRootGlobals) /
                 sizeof(kGameStaticRootGlobals[0]);
         ++i) {
        const u32 root = readWord(kGameStaticRootGlobals[i]);
        if (root != 0u &&
            (root < identity->heapStart || root >= identity->heapEnd ||
             (root & 3u) != 0u)) {
            return gateFailure(Gate::GameRoot, root, report);
        }
    }
    if (!isMem1Range(identity->currentScene, sizeof(u32))) {
        return gateFailure(Gate::Scene, identity->currentScene, report);
    }
    if (identity->mainLoopMode != 2u) {
        return gateFailure(Gate::LoopMode, identity->mainLoopMode, report);
    }
    if (identity->mainLoopExit != 0u) {
        return gateFailure(Gate::LoopExit, identity->mainLoopExit, report);
    }
    if (identity->mainLoopPendingScene != identity->mainLoopScene) {
        return gateFailure(Gate::LoopScene,
                           identity->mainLoopPendingScene, report);
    }
    if (identity->mainDrawState > 7u) {
        return gateFailure(Gate::DrawState, identity->mainDrawState, report);
    }
    if (identity->audioBasic != kAudioStaticObject ||
        readWord(kAudioBasicGlobal) != identity->audioBasic ||
        !isMem1Range(identity->audioBasic, kAudioSceneOffset + sizeof(u32)) ||
        readWord(identity->audioBasic + 8u) != kAudioVtable ||
        !validAudioBootstrap(identity->audioBasic)) {
        const u32 handle =
            isMem1Range(identity->audioBasic,
                        kAudioBootstrapHandleOffset + sizeof(u32))
                ? readWord(identity->audioBasic +
                           kAudioBootstrapHandleOffset)
                : identity->audioBasic;
        return gateFailure(Gate::Audio, handle, report);
    }
    identity->audioScene =
        readWord(identity->audioBasic + kAudioSceneOffset);
    gateReady(report);
    return true;
}

bool ioIdle(bool report = false) {
    const bool predicateBusy =
        reinterpret_cast<BoolFn>(kDvdBusyPredicateAddr)();
    const u32 dvdOutstanding = readWord(kDvdOutstandingGlobal);
    const u32 aram0 = readWord(kAramList0Global + 8u);
    const u32 aram1 = readWord(kAramList1Global + 8u);
    const u32 card0 = readWord(kCardBlockGlobal + kCardResultOffset);
    const u32 card1 = readWord(kCardBlockGlobal + kCardControlStride +
                               kCardResultOffset);
    if (predicateBusy) {
        return gateFailure(Gate::DvdPredicate, dvdOutstanding, report);
    }
    if (dvdOutstanding != 0u) {
        return gateFailure(Gate::DvdCount, dvdOutstanding, report);
    }
    if (aram0 != 0u) {
        return gateFailure(Gate::Aram0, aram0, report);
    }
    if (aram1 != 0u) {
        return gateFailure(Gate::Aram1, aram1, report);
    }
    if (card0 == kCardResultBusy) {
        return gateFailure(Gate::Card0, card0, report);
    }
    if (card1 == kCardResultBusy) {
        return gateFailure(Gate::Card1, card1, report);
    }
    gateReady(report);
    return true;
}

bool heapsHealthy(const LiveIdentity &identity) {
    HeapCheckFn check = reinterpret_cast<HeapCheckFn>(kExpHeapCheckAddr);
    return isExpHeap(identity.rootHeap) && isExpHeap(identity.systemHeap) &&
           isExpHeap(identity.heap) &&
           check(reinterpret_cast<void *>(identity.rootHeap)) &&
           check(reinterpret_cast<void *>(identity.systemHeap)) &&
           check(reinterpret_cast<void *>(identity.heap));
}

void copyWords(void *destination, const void *source, u32 size) {
    volatile u32 *out = reinterpret_cast<volatile u32 *>(destination);
    const volatile u32 *in = reinterpret_cast<const volatile u32 *>(source);
    for (u32 i = 0; i < size / sizeof(u32); ++i) {
        out[i] = in[i];
    }
}

void copyBytes(void *destination, const void *source, u32 size) {
    volatile u8 *out = reinterpret_cast<volatile u8 *>(destination);
    const volatile u8 *in = reinterpret_cast<const volatile u8 *>(source);
    for (u32 i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

void captureStaticRanges() {
    u32 offset = kStateStaticsOffset;
    for (u32 i = 0; i < kStateStaticRangeCount; ++i) {
        const StaticRange &range = kStateStaticRanges[i];
        copyBytes(reinterpret_cast<void *>(kSnapshotBase + offset),
                  reinterpret_cast<void *>(range.address), range.size);
        offset += range.size;
    }
}

void restoreStaticRanges() {
    u32 offset = kStateStaticsOffset;
    for (u32 i = 0; i < kStateStaticRangeCount; ++i) {
        const StaticRange &range = kStateStaticRanges[i];
        copyBytes(reinterpret_cast<void *>(range.address),
                  reinterpret_cast<void *>(kSnapshotBase + offset),
                  range.size);
        offset += range.size;
    }
}

void storeStaticRanges() {
    for (u32 i = 0; i < kStateStaticRangeCount; ++i) {
        const StaticRange &range = kStateStaticRanges[i];
        reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
            reinterpret_cast<void *>(range.address), range.size);
    }
}

void clearWords(void *destination, u32 size) {
    volatile u32 *out = reinterpret_cast<volatile u32 *>(destination);
    for (u32 i = 0; i < size / sizeof(u32); ++i) {
        out[i] = 0;
    }
}

u8 lowerAscii(u8 value) {
    return value >= 'A' && value <= 'Z'
               ? static_cast<u8>(value + ('a' - 'A'))
               : value;
}

u32 hashVolumeWord(u32 hash, u32 value) {
    hash ^= value;
    return hash * 16777619u;
}

u32 classifyVolumeHeap(u32 heap, const LiveIdentity &identity) {
    if (heap == identity.heap) return kVolumeOwnerGame;
    if (heap == identity.systemHeap) return kVolumeOwnerSystem;
    if (heap == identity.rootHeap) return kVolumeOwnerRoot;
    return kVolumeOwnerOther;
}

u32 classifyVolumeRange(u32 address, u32 size,
                        const LiveIdentity &identity) {
    if (size == 0u || !isMem1ByteRange(address, size)) {
        return kVolumeOwnerOther;
    }
    const u32 end = address + size;
    if (address >= identity.heapStart && end <= identity.heapEnd) {
        return kVolumeOwnerGame;
    }
    if (address >= identity.systemHeapStart && end <= identity.systemHeapEnd) {
        return kVolumeOwnerSystem;
    }
    if (address >= identity.rootHeapStart && end <= identity.rootHeapEnd) {
        return kVolumeOwnerRoot;
    }
    return kVolumeOwnerOther;
}

void captureVolumeName(VolumeDescriptor *entry) {
    entry->name[0] = '?';
    entry->name[1] = '\0';
    if (!isMem1ByteRange(entry->namePointer, 1u)) {
        return;
    }

    u32 hash = 2166136261u;
    for (u32 i = 0; i < kVolumeNameHashBytes; ++i) {
        if (!isMem1ByteRange(entry->namePointer + i, 1u)) {
            return;
        }
        const u8 value = readByte(entry->namePointer + i);
        if (value == 0u) {
            entry->nameHash = hash;
            entry->stateFlags |= kVolumeNameValid;
            if (i == 0u) {
                entry->name[0] = '/';
                entry->name[1] = '\0';
            }
            return;
        }
        hash ^= lowerAscii(value);
        hash *= 16777619u;
        if (i + 1u < kVolumeNameBytes) {
            entry->name[i] = value >= 0x20u && value <= 0x7Eu
                                 ? static_cast<char>(value)
                                 : '.';
            entry->name[i + 1u] = '\0';
        }
    }
}

void failVolumeCensus(VolumeCensus *census, u32 fault) {
    census->fault = fault;
    census->valid = 0u;
}

bool captureVolumeCensus(VolumeCensus *census,
                         const LiveIdentity &identity) {
    clearWords(census, sizeof(*census));
    census->count = identity.volume[2];
    census->head = identity.volume[0];
    census->tail = identity.volume[1];
    census->currentVolume = readWord(kCurrentVolumeGlobal);
    census->currentDirId = readWord(kCurrentDirIdGlobal);
    census->stableFrames = sStableFrames;

    if (census->count > kMaxVolumes) {
        failVolumeCensus(census, kVolumeFaultCapacity);
        return false;
    }
    if (census->count == 0u) {
        if (census->head != 0u || census->tail != 0u) {
            failVolumeCensus(census, kVolumeFaultEmpty);
            return false;
        }
        census->signature = 2166136261u;
        census->valid = 1u;
        return true;
    }
    if (!isMem1Range(census->head, 0x10u) ||
        !isMem1Range(census->tail, 0x10u)) {
        failVolumeCensus(census, kVolumeFaultEndpoint);
        return false;
    }

    u32 node = census->head;
    u32 previous = 0u;
    u32 signature = 2166136261u;
    for (u32 i = 0; i < census->count; ++i) {
        if (!isMem1Range(node, 0x10u)) {
            failVolumeCensus(census, kVolumeFaultNode);
            return false;
        }
        for (u32 prior = 0; prior < i; ++prior) {
            if (census->entries[prior].node == node) {
                failVolumeCensus(census, kVolumeFaultDuplicate);
                return false;
            }
        }

        VolumeDescriptor &entry = census->entries[i];
        entry.node = node;
        entry.object = readWord(node);
        entry.previous = readWord(node + 8u);
        entry.next = readWord(node + 0xCu);
        if (readWord(node + 4u) != kVolumeListGlobal) {
            failVolumeCensus(census, kVolumeFaultList);
            return false;
        }
        if (!isMem1Range(entry.object, 0x68u)) {
            failVolumeCensus(census, kVolumeFaultObject);
            return false;
        }
        if (entry.object + 0x18u != node ||
            readWord(entry.object + 0x18u) != entry.object) {
            failVolumeCensus(census, kVolumeFaultEmbeddedLink);
            return false;
        }
        if (entry.previous != previous) {
            failVolumeCensus(census, kVolumeFaultPrevious);
            return false;
        }
        for (u32 prior = 0; prior < i; ++prior) {
            if (census->entries[prior].object == entry.object) {
                failVolumeCensus(census, kVolumeFaultDuplicate);
                return false;
            }
        }

        entry.vtable = readWord(entry.object);
        entry.objectOwnerHeap = readWord(entry.object + 4u);
        entry.namePointer = readWord(entry.object + 0x28u);
        entry.type = readWord(entry.object + 0x2Cu);
        entry.mountCount = readWord(entry.object + 0x34u);
        if (readByte(entry.object + 0x30u) != 0u) {
            entry.stateFlags |= kVolumeMounted;
        }
        captureVolumeName(&entry);

        if (entry.vtable == kMemArchiveVtable) {
            entry.archiveHeap = readWord(entry.object + 0x38u);
            entry.mountSource = readWord(entry.object + 0x40u);
            entry.archiveInfo = readWord(entry.object + 0x44u);
            entry.archiveHeader = readWord(entry.object + 0x5Cu);
            entry.archiveData = readWord(entry.object + 0x60u);
            entry.stateFlags |= kVolumeArchiveValid;
            entry.stateFlags |=
                (static_cast<u32>(readByte(entry.object + 0x3Cu)) & 0xFu)
                << kVolumeModeShift;
            entry.stateFlags |=
                (readWord(entry.object + 0x58u) & 0xFu)
                << kVolumeDirectionShift;
            if (readByte(entry.object + 0x64u) != 0u) {
                entry.stateFlags |= kVolumeOpen;
            }
            if (isMem1Range(entry.archiveHeader, 0x20u) &&
                readWord(entry.archiveHeader) == kRarcMagic) {
                const u32 fileLength = readWord(entry.archiveHeader + 4u);
                const u32 dataLength = readWord(entry.archiveHeader + 0x10u);
                if (fileLength >= 0x20u && dataLength <= fileLength &&
                    isMem1ByteRange(entry.archiveHeader, fileLength)) {
                    entry.fileLength = fileLength;
                    entry.dataLength = dataLength;
                    entry.stateFlags |= kVolumeRarcValid;
                    u32 content = 2166136261u;
                    for (u32 offset = 0u; offset < 0x20u; offset += 4u) {
                        content = hashVolumeWord(
                            content, readWord(entry.archiveHeader + offset));
                    }
                    const u32 backingEnd = entry.archiveHeader + fileLength;
                    if (entry.archiveInfo >= entry.archiveHeader &&
                        entry.archiveInfo <= backingEnd - 0x20u &&
                        isMem1Range(entry.archiveInfo, 0x20u)) {
                        for (u32 offset = 0u; offset < 0x20u; offset += 4u) {
                            content = hashVolumeWord(
                                content,
                                readWord(entry.archiveInfo + offset));
                        }
                    }
                    entry.contentSignature = content;
                }
            }
        }

        const u32 backingSize =
            (entry.stateFlags & kVolumeRarcValid) != 0u
                ? entry.fileLength
                : (entry.archiveHeader != 0u ? sizeof(u32) : 0u);
        entry.ownerFlags =
            classifyVolumeHeap(entry.objectOwnerHeap, identity)
                << kVolumeObjectOwnerShift;
        entry.ownerFlags |=
            classifyVolumeHeap(entry.archiveHeap, identity)
            << kVolumeArchiveOwnerShift;
        entry.ownerFlags |= classifyVolumeRange(entry.object, 0x68u, identity)
                            << kVolumeObjectLocationShift;
        entry.ownerFlags |=
            classifyVolumeRange(entry.archiveHeader, backingSize, identity)
            << kVolumeBackingLocationShift;

        signature = hashVolumeWord(signature, entry.object);
        signature = hashVolumeWord(signature, entry.nameHash);
        signature = hashVolumeWord(signature, entry.type);
        previous = node;
        node = entry.next;
    }

    if (previous != census->tail) {
        failVolumeCensus(census, kVolumeFaultTail);
        return false;
    }
    if (node != 0u) {
        failVolumeCensus(census, kVolumeFaultEnd);
        return false;
    }
    if (readWord(kVolumeListGlobal) != census->head ||
        readWord(kVolumeListGlobal + 4u) != census->tail ||
        readWord(kVolumeListGlobal + 8u) != census->count) {
        failVolumeCensus(census, kVolumeFaultChanged);
        return false;
    }
    census->signature = signature;
    census->valid = 1u;
    return true;
}

bool sameVolumeDescriptor(const VolumeDescriptor &saved,
                          const VolumeDescriptor &live) {
    return saved.node == live.node && saved.object == live.object &&
           saved.vtable == live.vtable && saved.nameHash == live.nameHash &&
           saved.type == live.type &&
           saved.archiveHeader == live.archiveHeader &&
           saved.fileLength == live.fileLength &&
           saved.dataLength == live.dataLength &&
           saved.contentSignature == live.contentSignature;
}

s32 findVolume(const VolumeCensus &census,
               const VolumeDescriptor &entry) {
    for (u32 i = 0; i < census.count; ++i) {
        if (sameVolumeDescriptor(entry, census.entries[i])) {
            return static_cast<s32>(i);
        }
    }
    return -1;
}

void clearVolumeDiff() {
    clearWords(&sVolumeDiff, sizeof(sVolumeDiff));
    sVolumeDiff.removedIndices[0] = 0xFFFFFFFFu;
    sVolumeDiff.removedIndices[1] = 0xFFFFFFFFu;
    sVolumeDiff.addedIndices[0] = 0xFFFFFFFFu;
    sVolumeDiff.addedIndices[1] = 0xFFFFFFFFu;
}

void diffVolumeCensus(const VolumeCensus &saved,
                      const VolumeCensus &live) {
    clearVolumeDiff();
    sVolumeDiff.ready = 1u;
    sVolumeDiff.savedValid = saved.valid;
    sVolumeDiff.liveValid = live.valid;
    sVolumeDiff.savedCount = saved.count;
    sVolumeDiff.liveCount = live.count;
    sVolumeDiff.currentChanged =
        saved.currentVolume != live.currentVolume ||
                saved.currentDirId != live.currentDirId
            ? 1u
            : 0u;
    if (!saved.valid || !live.valid) {
        return;
    }

    for (u32 i = 0; i < saved.count; ++i) {
        if (findVolume(live, saved.entries[i]) < 0) {
            if (sVolumeDiff.removedCount < 2u) {
                sVolumeDiff.removedIndices[sVolumeDiff.removedCount] = i;
            }
            ++sVolumeDiff.removedCount;
        }
    }
    for (u32 i = 0; i < live.count; ++i) {
        if (findVolume(saved, live.entries[i]) < 0) {
            if (sVolumeDiff.addedCount < 2u) {
                sVolumeDiff.addedIndices[sVolumeDiff.addedCount] = i;
            }
            ++sVolumeDiff.addedCount;
        }
    }

    bool ordered = true;
    u32 nextLiveIndex = 0u;
    for (u32 i = 0; i < saved.count; ++i) {
        const s32 index = findVolume(live, saved.entries[i]);
        if (index >= 0) {
            if (static_cast<u32>(index) < nextLiveIndex) {
                ordered = false;
                break;
            }
            nextLiveIndex = static_cast<u32>(index) + 1u;
        }
    }
    sVolumeDiff.commonOrder = ordered ? 1u : 0u;

    if (sVolumeDiff.addedCount == 0u &&
        saved.count == live.count + sVolumeDiff.removedCount) {
        bool suffix = true;
        for (u32 i = 0; i < live.count; ++i) {
            if (!sameVolumeDescriptor(
                    saved.entries[sVolumeDiff.removedCount + i],
                    live.entries[i])) {
                suffix = false;
                break;
            }
        }
        sVolumeDiff.headOnly = suffix ? 1u : 0u;
    }
}

void commitSavedVolumeCensus(u32 generation) {
    sSavedVolumeCensus.generation = 0u;
    copyBytes(&sSavedVolumeCensus, &sLiveVolumeCensus,
              sizeof(sSavedVolumeCensus));
    sSavedVolumeCensus.generation = generation;
}

void diagnoseVolumeEpoch(const SnapshotHeader *header,
                         const LiveIdentity &live, u32 mask) {
    clearVolumeDiff();
    const u32 volumeMask = SUSAMUNE_LM_EPOCH_VOLUME_COUNT |
                           SUSAMUNE_LM_EPOCH_VOLUME_HEAD |
                           SUSAMUNE_LM_EPOCH_VOLUME_TAIL;
    if ((mask & volumeMask) == 0u ||
        sSavedVolumeCensus.generation != header->generation) {
        return;
    }
    captureVolumeCensus(&sLiveVolumeCensus, live);
    diffVolumeCensus(sSavedVolumeCensus, sLiveVolumeCensus);
}

u32 crcByte(u32 crc, u8 byte) {
    crc ^= byte;
    for (u32 bit = 0; bit < 8u; ++bit) {
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

u32 snapshotChecksum(const SnapshotHeader *header) {
    const volatile u8 *bytes = reinterpret_cast<const volatile u8 *>(header);
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < header->totalSize; ++i) {
        const bool ignored = i < sizeof(header->magic) ||
            (i >= __builtin_offsetof(SnapshotHeader, checksum) &&
             i < __builtin_offsetof(SnapshotHeader, checksum) +
                     sizeof(header->checksum));
        crc = crcByte(crc, ignored ? 0u : bytes[i]);
    }
    return crc ^ 0xFFFFFFFFu;
}

FreezeState freezeBegin() {
    FreezeState state;
    state.interruptsWereEnabled =
        reinterpret_cast<DisableInterruptsFn>(kOSDisableInterruptsAddr)();
    reinterpret_cast<SchedulerFn>(kOSDisableSchedulerAddr)();
    volatile u16 *dmaControl = reinterpret_cast<volatile u16 *>(0xCC005036u);
    state.dmaWasEnabled = (*dmaControl & 0x8000u) != 0u;
    *dmaControl = static_cast<u16>(*dmaControl & ~0x8000u);
    asm volatile("sync" ::: "memory");
    return state;
}

void freezeEnd(const FreezeState &state, bool journalLoad = false) {
    asm volatile("sync" ::: "memory");
    volatile u16 *dmaControl = reinterpret_cast<volatile u16 *>(0xCC005036u);
    const u16 liveControl = *dmaControl;
    *dmaControl = state.dmaWasEnabled
                      ? static_cast<u16>(liveControl | 0x8000u)
                      : static_cast<u16>(liveControl & ~0x8000u);
    asm volatile("sync" ::: "memory");
    if (journalLoad) {
        traceLoadPhase(0x71u, liveControl);
    }
    reinterpret_cast<SchedulerFn>(kOSEnableSchedulerAddr)();
    if (journalLoad) {
        traceLoadPhase(0x72u, state.interruptsWereEnabled ? 1u : 0u);
    }
    reinterpret_cast<RestoreInterruptsFn>(kOSRestoreInterruptsAddr)(
        state.interruptsWereEnabled);
    if (journalLoad) {
        traceLoadPhase(0x73u, state.interruptsWereEnabled ? 1u : 0u);
    }
}

bool quiesceAudio(const LiveIdentity &identity) {
    // LM's scene switch drains prior SE/sequence/stream handles and rebuilds
    // its required bootstrap sequence. Passing the live scene avoids a
    // resource-bank change; the replacement handle must remain engine-owned.
    if (!validAudioBootstrap(identity.audioBasic)) {
        return gateFailure(
            Gate::Audio,
            readWord(identity.audioBasic + kAudioBootstrapHandleOffset),
            true);
    }
    reinterpret_cast<AudioChangeSoundSceneFn>(kAudioChangeSoundSceneAddr)(
        reinterpret_cast<void *>(identity.audioBasic), identity.audioScene);
    if (!validAudioBootstrap(identity.audioBasic)) {
        return gateFailure(
            Gate::Audio,
            readWord(identity.audioBasic + kAudioBootstrapHandleOffset),
            true);
    }
    return true;
}

void setReject(LMState::Status status, u32 detail) {
    sStatus = status;
    LMCrash::note(kEventStateReject, static_cast<u32>(status), detail);
}

void clearEpochMismatch(EpochMismatch *mismatch) {
    mismatch->mask = 0u;
    mismatch->saved = 0u;
    mismatch->live = 0u;
}

void clearEpochMismatch() {
    clearEpochMismatch(&sEpochMismatch);
}

void addEpochMismatch(EpochMismatch *mismatch, u32 field, u32 saved,
                      u32 live) {
    if (saved == live) {
        return;
    }
    if (mismatch->mask == 0u) {
        mismatch->saved = saved;
        mismatch->live = live;
    }
    mismatch->mask |= field;
}

void collectPreflightEpochMismatch(EpochMismatch *mismatch,
                                   const SnapshotHeader *header,
                                   const LiveIdentity &live) {
    clearEpochMismatch(mismatch);
    // The order is intentional: the first saved/live pair should describe the
    // most useful high-level cause while the mask still reports every change.
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_MAP_VALUE,
                     header->mapValue, live.mapValue);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_SCENE_VALUE,
                     header->sceneValue, live.sceneValue);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_CURRENT_SCENE,
                     header->currentScene, live.currentScene);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_PENDING_SCENE,
                     header->mainLoopPendingScene,
                     live.mainLoopPendingScene);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_LOOP_MODE,
                     header->mainLoopMode, live.mainLoopMode);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_AUDIO_SCENE,
                     header->audioScene, live.audioScene);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_MAP_ARCHIVE,
                     header->mapArchive, live.mapArchive);
    // JKRFileLoader::sVolumeList is {head, tail, count}. Cardinality is the
    // clearest first clue, followed by endpoint replacement or reordering.
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_VOLUME_COUNT,
                     header->volume[2], live.volume[2]);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_VOLUME_HEAD,
                     header->volume[0], live.volume[0]);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_VOLUME_TAIL,
                     header->volume[1], live.volume[1]);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_MISSION_MODE,
                     header->missionMode, live.missionMode);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_GAME_MODE,
                     header->gameMode, live.gameMode);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_SIMPLE_MODELER,
                     header->simpleModeler, live.simpleModeler);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_MAP_COL,
                     header->mapCol, live.mapCol);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_EN_TYPES,
                     header->enTypesManager, live.enTypesManager);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_GAME_HEAP,
                     header->heap, live.heap);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_GAME_HEAP_START,
                     header->heapStart, live.heapStart);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_GAME_HEAP_END,
                     header->heapEnd, live.heapEnd);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_ROOT_HEAP,
                     header->rootHeap, live.rootHeap);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_SYSTEM_HEAP,
                     header->systemHeap, live.systemHeap);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_AUDIO_BASIC,
                     header->audioBasic, live.audioBasic);
    addEpochMismatch(mismatch, SUSAMUNE_LM_EPOCH_DRAW_STATE,
                     header->mainDrawState, live.mainDrawState);
}

void rejectEpoch(const EpochMismatch &mismatch, const SnapshotHeader *header,
                 const LiveIdentity &live) {
    diagnoseVolumeEpoch(header, live, mismatch.mask);
    sEpochMismatch = mismatch;
    LMCrash::phase(SUSAMUNE_PHASE_ACTION_LOAD,
                   SUSAMUNE_LM_EPOCH_PHASE_FLAG | mismatch.mask,
                   mismatch.saved, mismatch.live);
    setReject(LMState::Status::Epoch, mismatch.mask);
}

bool basicHeaderValid(const SnapshotHeader *header) {
    if (header->magic != kSnapshotMagic ||
        header->version != kSnapshotVersion ||
        header->headerSize != kHeaderSize ||
        header->gameId != SUSAMUNE_MOD_GAME_ID_LMJ ||
        header->heapMetadataOffset != kHeapMetadataOffset ||
        header->heapMetadataSize != kHeapMetadataSize ||
        header->stateStaticsOffset != kStateStaticsOffset ||
        header->stateStaticsSize != kStateStaticsSize ||
        header->heapDataOffset != kHeapDataOffset ||
        header->heapDataSize != header->heapSize || header->heapSize == 0u ||
        header->heapSize > kSnapshotCapacity - kHeapDataOffset ||
        header->totalSize != kHeapDataOffset + header->heapSize ||
        header->totalSize > kSnapshotCapacity) {
        return false;
    }
    const bool plausibleCurrentHeap =
        header->currentHeap == header->rootHeap ||
        header->currentHeap == header->systemHeap ||
        header->currentHeap == header->heap ||
        (header->currentHeap >= header->heapStart &&
         header->currentHeap <= header->heapEnd - kHeapMetadataEnd &&
         (header->currentHeap & 3u) == 0u);
    const bool childRangesValid =
        rangeInside(header->systemHeapStart, header->systemHeapEnd,
                    header->rootHeapStart, header->rootHeapEnd) &&
        rangeInside(header->heapStart, header->heapEnd,
                    header->rootHeapStart, header->rootHeapEnd) &&
        (header->systemHeapEnd <= header->heapStart ||
         header->heapEnd <= header->systemHeapStart);
    return header->heapStart < header->heapEnd &&
           header->heapSize == header->heapEnd - header->heapStart &&
           ((header->heapStart | header->heapEnd) &
            (kExpHeapAlignment - 1u)) == 0u &&
           isMem1Range(header->heapStart, header->heapSize) &&
           header->rootHeapStart < header->rootHeapEnd &&
           header->rootHeapSize ==
               header->rootHeapEnd - header->rootHeapStart &&
           ((header->rootHeapStart | header->rootHeapEnd) &
            (kExpHeapAlignment - 1u)) == 0u &&
           isMem1Range(header->rootHeapStart, header->rootHeapSize) &&
           header->systemHeapStart < header->systemHeapEnd &&
           header->systemHeapSize ==
               header->systemHeapEnd - header->systemHeapStart &&
           ((header->systemHeapStart | header->systemHeapEnd) &
            (kExpHeapAlignment - 1u)) == 0u &&
           isMem1Range(header->systemHeapStart, header->systemHeapSize) &&
           isMem1Range(header->heap, kHeapMetadataEnd) &&
           isMem1Range(header->rootHeap, kHeapMetadataEnd) &&
           isMem1Range(header->systemHeap, kHeapMetadataEnd) &&
           header->rootHeap != header->systemHeap &&
           header->rootHeap != header->heap &&
           header->systemHeap != header->heap && childRangesValid &&
           plausibleCurrentHeap && header->missionMode != 0u &&
           header->missionMode >= header->heapStart &&
           header->missionMode <= header->heapEnd - 0x1Cu &&
           header->gameMode == header->missionMode &&
           header->gameModeCount == 1u &&
           header->mainLoopMode == 2u &&
           header->mainLoopPendingScene == header->sceneValue &&
           header->mainDrawState <= 7u &&
           isMem1Range(header->currentScene, sizeof(u32)) &&
           header->audioBasic == kAudioStaticObject &&
           header->heapMode <= 0xFFu && header->heapGroup <= 0xFFu &&
           header->rootHeapMode <= 0xFFu &&
           header->rootHeapGroup <= 0xFFu &&
           header->systemHeapMode <= 0xFFu &&
           header->systemHeapGroup <= 0xFFu &&
           header->currentHeapGroup <= 0xFFu;
}

void initializeSlot() {
    if (sSlotInitialized) {
        return;
    }
    // MEM2 is not a persistent-state format. Invalidate the commit word once
    // per injected payload so a valid-looking slot left by an earlier game
    // session can never be loaded into a fresh process.
    SnapshotHeader *header =
        reinterpret_cast<SnapshotHeader *>(kSnapshotBase);
    header->magic = 0u;
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(header, 32u);
    asm volatile("sync" ::: "memory");
    clearWords(&sSavedVolumeCensus, sizeof(sSavedVolumeCensus));
    clearWords(&sLiveVolumeCensus, sizeof(sLiveVolumeCensus));
    clearVolumeDiff();
    sSlotInitialized = true;
}

bool headerMatchesLive(const SnapshotHeader *header,
                       const LiveIdentity &live) {
    return header->heap == live.heap &&
           header->heapStart == live.heapStart &&
           header->heapEnd == live.heapEnd &&
           header->heapSize == live.heapSize &&
           header->rootHeap == live.rootHeap &&
           header->rootHeapStart == live.rootHeapStart &&
           header->rootHeapEnd == live.rootHeapEnd &&
           header->rootHeapSize == live.rootHeapSize &&
           header->rootHeapMode == live.rootHeapMode &&
           header->rootHeapGroup == live.rootHeapGroup &&
           header->rootFreeHead == live.rootFreeHead &&
           header->rootFreeTail == live.rootFreeTail &&
           header->rootUsedHead == live.rootUsedHead &&
           header->rootUsedTail == live.rootUsedTail &&
           header->systemHeap == live.systemHeap &&
           header->systemHeapStart == live.systemHeapStart &&
           header->systemHeapEnd == live.systemHeapEnd &&
           header->systemHeapSize == live.systemHeapSize &&
           header->currentHeap == live.currentHeap &&
           header->missionMode == live.missionMode &&
           header->mapArchive == live.mapArchive &&
           header->mapValue == live.mapValue &&
           header->sceneValue == live.sceneValue &&
           header->currentScene == live.currentScene &&
           header->gameMode == live.gameMode &&
           header->gameModeCount == live.gameModeCount &&
           header->heapMode == live.heapMode &&
           header->heapGroup == live.heapGroup &&
           header->systemHeapMode == live.systemHeapMode &&
           header->systemHeapGroup == live.systemHeapGroup &&
           header->systemFreeHead == live.systemFreeHead &&
           header->systemFreeTail == live.systemFreeTail &&
           header->systemUsedHead == live.systemUsedHead &&
           header->systemUsedTail == live.systemUsedTail &&
           header->currentHeapGroup == live.currentHeapGroup &&
           header->audioBasic == live.audioBasic &&
           header->audioScene == live.audioScene &&
           header->mainLoopMode == live.mainLoopMode &&
           header->mainLoopPendingScene == live.mainLoopPendingScene &&
           header->mainDrawState == live.mainDrawState &&
           header->simpleModeler == live.simpleModeler &&
           header->mapCol == live.mapCol &&
           header->enTypesManager == live.enTypesManager &&
           header->volume[0] == live.volume[0] &&
           header->volume[1] == live.volume[1] &&
           header->volume[2] == live.volume[2];
}

bool pointerInSavedHeap(u32 pointer, const SnapshotHeader *header) {
    return pointer >= header->heapStart && pointer < header->heapEnd &&
           (pointer & 3u) == 0u;
}

bool savedPointerCompatible(u32 saved, u32 current,
                            const SnapshotHeader *header) {
    // Heap-owned roots are rewound below. A root owned by the fixed system
    // heap is safe only when it is still exactly the same live object.
    return saved == 0u || pointerInSavedHeap(saved, header) ||
           saved == current;
}

void saveState() {
    clearEpochMismatch();
    clearVolumeDiff();
    traceSavePhase(0x01u, sStableFrames);
    LiveIdentity preflight;
    if (sStableFrames < kRequiredStableFrames ||
        !buildIdentity(&preflight) ||
        !sameIdentity(preflight, sLastIdentity) || !ioIdle()) {
        setReject(LMState::Status::Busy, sStableFrames);
        return;
    }
    if (!heapsHealthy(preflight)) {
        setReject(LMState::Status::BadHeap, preflight.heap);
        return;
    }

    // Drain prior live handles through LM's own scene-change path. Its new
    // bootstrap handle remains in uncaptured system audio state.
    traceSavePhase(0x20u, preflight.audioBasic);
    if (!quiesceAudio(preflight)) {
        setReject(LMState::Status::Busy, preflight.audioBasic);
        return;
    }
    traceSavePhase(0x21u, preflight.audioBasic);
    LiveIdentity before;
    if (!buildIdentity(&before) || !ioIdle() || !heapsHealthy(before)) {
        setReject(LMState::Status::Busy, preflight.heap);
        return;
    }
    const u32 totalSize = kHeapDataOffset + before.heapSize;
    if (totalSize > kSnapshotCapacity) {
        setReject(LMState::Status::TooLarge, totalSize);
        return;
    }

    traceSavePhase(0x40u, before.heap);
    const FreezeState freeze = freezeBegin();
    traceSavePhase(0x43u, before.heap);
    LiveIdentity live;
    if (!buildIdentity(&live) || !sameIdentity(before, live)) {
        freezeEnd(freeze);
        setReject(LMState::Status::Busy, live.heap);
        return;
    }
    captureVolumeCensus(&sLiveVolumeCensus, live);

    traceSavePhase(0x60u, live.heap);
    SnapshotHeader *header =
        reinterpret_cast<SnapshotHeader *>(kSnapshotBase);
    header->magic = 0u;
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(header, 32u);
    asm volatile("sync" ::: "memory");

    clearWords(header, kHeapDataOffset);
    header->version = kSnapshotVersion;
    header->headerSize = kHeaderSize;
    header->gameId = SUSAMUNE_MOD_GAME_ID_LMJ;
    header->totalSize = totalSize;
    header->generation = ++sGeneration;
    header->heap = live.heap;
    header->heapStart = live.heapStart;
    header->heapEnd = live.heapEnd;
    header->heapSize = live.heapSize;
    header->heapMetadataOffset = kHeapMetadataOffset;
    header->heapMetadataSize = kHeapMetadataSize;
    header->stateStaticsOffset = kStateStaticsOffset;
    header->stateStaticsSize = kStateStaticsSize;
    header->heapDataOffset = kHeapDataOffset;
    header->heapDataSize = live.heapSize;
    header->rootHeap = live.rootHeap;
    header->rootHeapStart = live.rootHeapStart;
    header->rootHeapEnd = live.rootHeapEnd;
    header->rootHeapSize = live.rootHeapSize;
    header->rootHeapMode = live.rootHeapMode;
    header->rootHeapGroup = live.rootHeapGroup;
    header->rootFreeHead = live.rootFreeHead;
    header->rootFreeTail = live.rootFreeTail;
    header->rootUsedHead = live.rootUsedHead;
    header->rootUsedTail = live.rootUsedTail;
    header->systemHeap = live.systemHeap;
    header->systemHeapStart = live.systemHeapStart;
    header->systemHeapEnd = live.systemHeapEnd;
    header->systemHeapSize = live.systemHeapSize;
    header->currentHeap = live.currentHeap;
    header->missionMode = live.missionMode;
    header->mapArchive = live.mapArchive;
    header->volume[0] = live.volume[0];
    header->volume[1] = live.volume[1];
    header->volume[2] = live.volume[2];
    header->mapValue = live.mapValue;
    header->sceneValue = live.sceneValue;
    header->currentScene = live.currentScene;
    header->gameMode = live.gameMode;
    header->gameModeCount = live.gameModeCount;
    header->heapMode = live.heapMode;
    header->heapGroup = live.heapGroup;
    header->systemHeapMode = live.systemHeapMode;
    header->systemHeapGroup = live.systemHeapGroup;
    header->systemFreeHead = live.systemFreeHead;
    header->systemFreeTail = live.systemFreeTail;
    header->systemUsedHead = live.systemUsedHead;
    header->systemUsedTail = live.systemUsedTail;
    header->currentHeapGroup = live.currentHeapGroup;
    header->randomState = readWord(kRandomStateGlobal);
    header->freeHead = live.heapFreeHead;
    header->freeTail = live.heapFreeTail;
    header->usedHead = live.heapUsedHead;
    header->usedTail = live.heapUsedTail;
    header->mainLoopMode = live.mainLoopMode;
    header->mainLoopPendingScene = live.mainLoopPendingScene;
    header->mainDrawState = live.mainDrawState;
    header->simpleModeler = live.simpleModeler;
    header->mapCol = live.mapCol;
    header->enTypesManager = live.enTypesManager;
    header->audioBasic = live.audioBasic;
    header->audioScene = live.audioScene;

    copyWords(reinterpret_cast<void *>(kSnapshotBase + kHeapMetadataOffset),
              reinterpret_cast<void *>(live.heap + kHeapMetadataStart),
              kHeapMetadataSize);
    captureStaticRanges();
    traceSavePhase(0x63u, kStateStaticsSize);
    copyWords(reinterpret_cast<void *>(kSnapshotBase + kHeapDataOffset),
              reinterpret_cast<void *>(live.heapStart), live.heapSize);
    traceSavePhase(0x64u, totalSize);
    header->checksum = snapshotChecksum(header);

    traceSavePhase(0x65u, totalSize);
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(header, totalSize);
    asm volatile("sync" ::: "memory");
    header->magic = kSnapshotMagic;
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(header, 32u);
    asm volatile("sync" ::: "memory");
    commitSavedVolumeCensus(header->generation);
    traceSavePhase(0x70u, live.heap);
    freezeEnd(freeze);

    sSnapshotSize = totalSize;
    sStatus = LMState::Status::Saved;
    traceSavePhase(0x7Fu, totalSize);
    LMCrash::note(kEventStateSave, live.heap, live.heapSize);
}

void loadState() {
    clearEpochMismatch();
    clearVolumeDiff();
    traceLoadPhase(0x01u, sStableFrames);
    SnapshotHeader *header =
        reinterpret_cast<SnapshotHeader *>(kSnapshotBase);
    traceLoadPhase(0x02u, kHeaderSize);
    reinterpret_cast<CacheRangeFn>(kDCInvalidateRangeAddr)(header,
                                                            kHeaderSize);
    if (header->magic != kSnapshotMagic) {
        setReject(LMState::Status::Empty, header->magic);
        return;
    }
    if (!basicHeaderValid(header)) {
        setReject(LMState::Status::BadCrc, header->version);
        return;
    }
    traceLoadPhase(0x03u, header->totalSize);
    reinterpret_cast<CacheRangeFn>(kDCInvalidateRangeAddr)(header,
                                                            header->totalSize);
    if (!basicHeaderValid(header) ||
        snapshotChecksum(header) != header->checksum) {
        setReject(LMState::Status::BadCrc, header->checksum);
        return;
    }
    traceLoadPhase(0x05u, header->checksum);

    LiveIdentity preflight;
    if (sStableFrames < kRequiredStableFrames ||
        !buildIdentity(&preflight) ||
        !sameIdentity(preflight, sLastIdentity) || !ioIdle()) {
        setReject(LMState::Status::Busy, sStableFrames);
        return;
    }
    EpochMismatch mismatch;
    collectPreflightEpochMismatch(&mismatch, header, preflight);
    if (mismatch.mask != 0u) {
        rejectEpoch(mismatch, header, preflight);
        return;
    }
    const u32 liveCurrentScene = readWord(kCurrentSceneGlobal);
    const u32 liveGameMode = readWord(kGameModeGlobal);
    clearEpochMismatch(&mismatch);
    if (!savedPointerCompatible(header->currentScene, liveCurrentScene,
                                header)) {
        addEpochMismatch(&mismatch, SUSAMUNE_LM_EPOCH_CURRENT_SCENE,
                         header->currentScene, liveCurrentScene);
    }
    if (!savedPointerCompatible(header->gameMode, liveGameMode, header)) {
        addEpochMismatch(&mismatch, SUSAMUNE_LM_EPOCH_GAME_MODE,
                         header->gameMode, liveGameMode);
    }
    if (mismatch.mask != 0u) {
        rejectEpoch(mismatch, header, preflight);
        return;
    }
    if (!heapsHealthy(preflight)) {
        setReject(LMState::Status::BadHeap, preflight.heap);
        return;
    }

    traceLoadPhase(0x20u, preflight.audioBasic);
    if (!quiesceAudio(preflight)) {
        setReject(LMState::Status::Busy, preflight.audioBasic);
        return;
    }
    traceLoadPhase(0x21u, preflight.audioBasic);
    LiveIdentity before;
    if (!buildIdentity(&before) || !ioIdle()) {
        setReject(LMState::Status::Epoch, preflight.heap);
        return;
    }
    if (!headerMatchesLive(header, before)) {
        collectPreflightEpochMismatch(&mismatch, header, before);
        if (mismatch.mask != 0u) {
            rejectEpoch(mismatch, header, before);
        } else {
            setReject(LMState::Status::Epoch, preflight.heap);
        }
        return;
    }
    if (!heapsHealthy(before)) {
        setReject(LMState::Status::Epoch, preflight.heap);
        return;
    }

    traceLoadPhase(0x40u, before.heap);
    const FreezeState freeze = freezeBegin();
    traceLoadPhase(0x43u, before.heap);
    LiveIdentity live;
    if (!buildIdentity(&live) || !sameIdentity(before, live) ||
        !headerMatchesLive(header, live)) {
        freezeEnd(freeze);
        setReject(LMState::Status::Busy, live.heap);
        return;
    }

    traceLoadPhase(0x60u, live.heapSize);
    copyWords(reinterpret_cast<void *>(live.heap + kHeapMetadataStart),
              reinterpret_cast<void *>(kSnapshotBase + kHeapMetadataOffset),
              kHeapMetadataSize);
    traceLoadPhase(0x61u, kHeapMetadataSize);
    copyWords(reinterpret_cast<void *>(live.heapStart),
              reinterpret_cast<void *>(kSnapshotBase + kHeapDataOffset),
              live.heapSize);
    traceLoadPhase(0x62u, live.heapSize);

    // Restore game-owned statics but leave JAudio, JSystem, SDK, and allocator
    // globals live. Their queues and hardware-facing state cannot be rewound.
    restoreStaticRanges();
    writeWord(kRandomStateGlobal, header->randomState);
    traceLoadPhase(0x63u, kStateStaticsSize);

    // MissionMode is captured with the game heap and mounted volumes remain an
    // exact epoch gate because their resource backing is not rewound.
    traceLoadPhase(0x64u, header->currentScene);

    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(live.heap + kHeapMetadataStart),
        kHeapMetadataSize);
    traceLoadPhase(0x65u, kHeapMetadataSize);
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(live.heapStart), live.heapSize);
    traceLoadPhase(0x66u, live.heapSize);
    storeStaticRanges();
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(kRandomStateGlobal), sizeof(u32));
    traceLoadPhase(0x67u, kStateStaticsSize);
    traceLoadPhase(0x68u, live.heap);
    reinterpret_cast<VoidFn>(kGXInvalidateTexAllAddr)();
    asm volatile("sync" ::: "memory");
    traceLoadPhase(0x69u, live.heap);

    traceLoadPhase(0x70u, live.heap);
    freezeEnd(freeze, true);
    traceLoadPhase(0x74u, live.heap);

    HeapCheckFn check = reinterpret_cast<HeapCheckFn>(kExpHeapCheckAddr);
    traceLoadPhase(0x75u, live.rootHeap);
    bool healthyAfter = isExpHeap(live.rootHeap) &&
                        check(reinterpret_cast<void *>(live.rootHeap));
    traceLoadPhase(0x76u, healthyAfter ? 1u : 0u);
    if (healthyAfter) {
        traceLoadPhase(0x77u, live.systemHeap);
        healthyAfter = isExpHeap(live.systemHeap) &&
                       check(reinterpret_cast<void *>(live.systemHeap));
        traceLoadPhase(0x78u, healthyAfter ? 1u : 0u);
    }
    if (healthyAfter) {
        traceLoadPhase(0x79u, live.heap);
        healthyAfter = isExpHeap(live.heap) &&
                       check(reinterpret_cast<void *>(live.heap));
        traceLoadPhase(0x7Au, healthyAfter ? 1u : 0u);
    }

    sSnapshotSize = header->totalSize;
    sStableFrames = 0u;
    if (!healthyAfter) {
        setReject(LMState::Status::BadHeap, live.heap);
        return;
    }
    sStatus = LMState::Status::Loaded;
    sPostLoadTraceFrame = 0u;
    sPostLoadTraceState = 1u;
    traceLoadPhase(0x7Fu, header->totalSize);
    LMCrash::note(kEventStateLoad, live.heap, live.heapSize);
}

void updateStability() {
    LiveIdentity live;
    if (!buildIdentity(&live, true) || !ioIdle(true)) {
        sHaveIdentity = false;
        sStableFrames = 0u;
        return;
    }
    if (sHaveIdentity && sameIdentity(live, sLastIdentity)) {
        if (sStableFrames < 999u) {
            ++sStableFrames;
        }
    } else {
        sLastIdentity = live;
        sHaveIdentity = true;
        sStableFrames = 1u;
    }
}

const VolumeDescriptor *volumeChangeEntry(u32 displayIndex, bool *added) {
    *added = false;
    if (!sVolumeDiff.ready || displayIndex >= 2u) {
        return nullptr;
    }
    const u32 shownRemoved =
        sVolumeDiff.removedCount < 2u ? sVolumeDiff.removedCount : 2u;
    if (displayIndex < shownRemoved) {
        const u32 index = sVolumeDiff.removedIndices[displayIndex];
        return index < sSavedVolumeCensus.count
                   ? &sSavedVolumeCensus.entries[index]
                   : nullptr;
    }
    const u32 addedIndex = displayIndex - shownRemoved;
    if (addedIndex < sVolumeDiff.addedCount && addedIndex < 2u) {
        const u32 index = sVolumeDiff.addedIndices[addedIndex];
        if (index < sLiveVolumeCensus.count) {
            *added = true;
            return &sLiveVolumeCensus.entries[index];
        }
    }
    return nullptr;
}

const char *volumeOwnerText(u32 owner) {
    switch (owner & kVolumeOwnerMask) {
    case kVolumeOwnerGame:
        return "G";
    case kVolumeOwnerSystem:
        return "S";
    case kVolumeOwnerRoot:
        return "R";
    default:
        return "?";
    }
}

}  // namespace

namespace LMState {

void postLoadMilestone(u32 phase) {
    if (sPostLoadTraceState != 0u) {
        tracePostLoadPhase(phase, sPostLoadTraceFrame);
    }
}

void postLoadDetail(u32 phase, u32 arg0, u32 arg1) {
    if (sPostLoadTraceState != 0u) {
        LMCrash::phase(SUSAMUNE_PHASE_ACTION_POST_LOAD, phase, arg0, arg1);
    }
}

void presenterEnter() {
    if (sPostLoadTraceState == 1u) {
        if (sPostLoadTraceFrame >= kPostLoadTraceFrameLimit) {
            sPostLoadTraceState = 0u;
            return;
        }
        ++sPostLoadTraceFrame;
        tracePostLoadPhase(0x81u, sPostLoadTraceFrame);
        sPostLoadTraceState = 2u;
    }
}

void presenterAfterSample() {
    if (sPostLoadTraceState == 2u) {
        tracePostLoadPhase(0x82u, sPostLoadTraceFrame);
    }
}

void presenterAfterDrawDone() {
    if (sPostLoadTraceState == 2u) {
        tracePostLoadPhase(0x83u, sPostLoadTraceFrame);
    }
}

void presenterAfterRetail() {
    if (sPostLoadTraceState == 2u) {
        tracePostLoadPhase(0x84u, sPostLoadTraceFrame);
    }
}

void presenterBeforeTick() {
    if (sPostLoadTraceState == 2u) {
        tracePostLoadPhase(0x85u, sPostLoadTraceFrame);
    }
}

void presenterAfterTick() {
    if (sPostLoadTraceState == 1u) {
        // The load returned at the true post-presenter transaction boundary.
        tracePostLoadPhase(0x80u, sPostLoadTraceFrame);
    } else if (sPostLoadTraceState == 2u) {
        tracePostLoadPhase(0x86u, sPostLoadTraceFrame);
        // Keep the final frame's following loop tail visible. The next
        // presenter entry retires tracing before a ninth frame is recorded.
        sPostLoadTraceState = 1u;
    }
}

void tick() {
    initializeSlot();
    updateStability();
    const u16 buttons = readHalf(kPadStatusGlobal);
    const bool leftEdge = buttons == kDPadLeft && sPreviousButtons != kDPadLeft;
    const bool rightEdge =
        buttons == kDPadRight && sPreviousButtons != kDPadRight;
    sPreviousButtons = buttons;

    if (leftEdge) {
        saveState();
    } else if (rightEdge) {
        loadState();
    }
}

Status status() {
    return sStatus;
}

const char *statusText() {
    switch (sStatus) {
    case Status::Empty:
        return "EMPTY";
    case Status::Saved:
        return "SAVED";
    case Status::Loaded:
        return "LOADED";
    case Status::Busy:
        return "BUSY";
    case Status::BadCrc:
        return "BADCRC";
    case Status::BadHeap:
        return "BADHEAP";
    case Status::Epoch:
        return "EPOCH";
    case Status::TooLarge:
        return "TOOBIG";
    }
    return "UNKNOWN";
}

u32 snapshotKiB() {
    return sSnapshotSize >> 10;
}

u32 stableFrames() {
    return sStableFrames;
}

const char *gateText() {
    switch (sGate) {
    case Gate::Ready:
        return "OK";
    case Gate::Boot:
        return "BOOT";
    case Gate::GameHeap:
        return "GAME";
    case Gate::RootHeap:
        return "ROOT";
    case Gate::SystemHeap:
        return "SYS";
    case Gate::Size:
        return "SIZE";
    case Gate::Distinct:
        return "DIST";
    case Gate::SystemNest:
        return "SNEST";
    case Gate::GameNest:
        return "GNEST";
    case Gate::Overlap:
        return "OVER";
    case Gate::CurrentHeap:
        return "CUR";
    case Gate::MissionNull:
        return "MNUL";
    case Gate::MissionRange:
        return "MRNG";
    case Gate::ModeMismatch:
        return "MODE";
    case Gate::ModeCount:
        return "MCNT";
    case Gate::GameRoot:
        return "GROOT";
    case Gate::Scene:
        return "SCENE";
    case Gate::LoopMode:
        return "LOOP";
    case Gate::LoopExit:
        return "EXIT";
    case Gate::LoopScene:
        return "PEND";
    case Gate::DrawState:
        return "DRAW";
    case Gate::DvdPredicate:
        return "DVDP";
    case Gate::DvdCount:
        return "DVDC";
    case Gate::Aram0:
        return "AR0";
    case Gate::Aram1:
        return "AR1";
    case Gate::Card0:
        return "CARD0";
    case Gate::Card1:
        return "CARD1";
    case Gate::Audio:
        return "AUDIO";
    }
    return "?";
}

u32 gateValue() {
    return sGateValue;
}

const char *epochText() {
    const u32 mask = sEpochMismatch.mask;
    if (mask & SUSAMUNE_LM_EPOCH_MAP_VALUE)
        return "MAPV";
    if (mask & SUSAMUNE_LM_EPOCH_SCENE_VALUE)
        return "SCNV";
    if (mask & SUSAMUNE_LM_EPOCH_CURRENT_SCENE)
        return "SCNP";
    if (mask & SUSAMUNE_LM_EPOCH_PENDING_SCENE)
        return "PEND";
    if (mask & SUSAMUNE_LM_EPOCH_LOOP_MODE)
        return "LOOP";
    if (mask & SUSAMUNE_LM_EPOCH_AUDIO_SCENE)
        return "AUDS";
    if (mask & SUSAMUNE_LM_EPOCH_MAP_ARCHIVE)
        return "MARC";
    if (mask & SUSAMUNE_LM_EPOCH_VOLUME_COUNT)
        return "VOLN";
    if (mask & SUSAMUNE_LM_EPOCH_VOLUME_HEAD)
        return "VOLH";
    if (mask & SUSAMUNE_LM_EPOCH_VOLUME_TAIL)
        return "VOLT";
    if (mask & SUSAMUNE_LM_EPOCH_MISSION_MODE)
        return "MISS";
    if (mask & SUSAMUNE_LM_EPOCH_GAME_MODE)
        return "GMOD";
    if (mask & SUSAMUNE_LM_EPOCH_SIMPLE_MODELER)
        return "SIMP";
    if (mask & SUSAMUNE_LM_EPOCH_MAP_COL)
        return "MCOL";
    if (mask & SUSAMUNE_LM_EPOCH_EN_TYPES)
        return "ENTY";
    if (mask & SUSAMUNE_LM_EPOCH_GAME_HEAP)
        return "HEAP";
    if (mask & SUSAMUNE_LM_EPOCH_GAME_HEAP_START)
        return "HBEG";
    if (mask & SUSAMUNE_LM_EPOCH_GAME_HEAP_END)
        return "HEND";
    if (mask & SUSAMUNE_LM_EPOCH_ROOT_HEAP)
        return "ROOT";
    if (mask & SUSAMUNE_LM_EPOCH_SYSTEM_HEAP)
        return "SYSP";
    if (mask & SUSAMUNE_LM_EPOCH_AUDIO_BASIC)
        return "AUDO";
    if (mask & SUSAMUNE_LM_EPOCH_DRAW_STATE)
        return "DRAW";
    return "NONE";
}

u32 epochMask() {
    return sEpochMismatch.mask;
}

u32 epochSaved() {
    return sEpochMismatch.saved;
}

u32 epochLive() {
    return sEpochMismatch.live;
}

const char *volumeTopologyText() {
    if (!sVolumeDiff.ready) return "WAIT";
    if (!sVolumeDiff.savedValid) return "SBAD";
    if (!sVolumeDiff.liveValid) return "LBAD";
    if (sVolumeDiff.removedCount == 0u && sVolumeDiff.addedCount == 0u) {
        return sVolumeDiff.commonOrder ? "SAME" : "MIX";
    }
    if (sVolumeDiff.headOnly) {
        if (sVolumeDiff.removedCount == 1u) return "HEAD1";
        if (sVolumeDiff.removedCount == 2u) return "HEAD2";
        return "HEADN";
    }
    return sVolumeDiff.commonOrder ? "ORDER" : "MIX";
}

u32 volumeSavedCount() {
    return sVolumeDiff.savedCount;
}

u32 volumeLiveCount() {
    return sVolumeDiff.liveCount;
}

u32 volumeRemovedCount() {
    return sVolumeDiff.removedCount;
}

u32 volumeAddedCount() {
    return sVolumeDiff.addedCount;
}

u32 volumeSavedFault() {
    return sVolumeDiff.ready ? sSavedVolumeCensus.fault : 0u;
}

u32 volumeLiveFault() {
    return sVolumeDiff.ready ? sLiveVolumeCensus.fault : 0u;
}

u32 volumeSavedCurrent() {
    return sVolumeDiff.ready ? sSavedVolumeCensus.currentVolume : 0u;
}

u32 volumeLiveCurrent() {
    return sVolumeDiff.ready ? sLiveVolumeCensus.currentVolume : 0u;
}

u32 volumeSavedDir() {
    return sVolumeDiff.ready ? sSavedVolumeCensus.currentDirId : 0u;
}

u32 volumeLiveDir() {
    return sVolumeDiff.ready ? sLiveVolumeCensus.currentDirId : 0u;
}

const char *volumeChangeKind(u32 index) {
    bool added;
    const VolumeDescriptor *entry = volumeChangeEntry(index, &added);
    return entry ? (added ? "+" : "-") : " ";
}

const char *volumeChangeName(u32 index) {
    bool added;
    const VolumeDescriptor *entry = volumeChangeEntry(index, &added);
    return entry && (entry->stateFlags & kVolumeNameValid) != 0u
               ? entry->name
               : "--";
}

const char *volumeChangeObjectOwnerText(u32 index) {
    bool added;
    const VolumeDescriptor *entry = volumeChangeEntry(index, &added);
    if (!entry) return "?";
    u32 owner = entry->ownerFlags >> kVolumeObjectOwnerShift;
    owner &= kVolumeOwnerMask;
    if (owner == kVolumeOwnerOther) {
        owner = entry->ownerFlags >> kVolumeObjectLocationShift;
    }
    return volumeOwnerText(owner);
}

const char *volumeChangeBackingOwnerText(u32 index) {
    bool added;
    const VolumeDescriptor *entry = volumeChangeEntry(index, &added);
    if (!entry) return "?";
    const u32 owner =
        (entry->ownerFlags >> kVolumeBackingLocationShift) & kVolumeOwnerMask;
    return volumeOwnerText(owner);
}

u32 volumeChangeObject(u32 index) {
    bool added;
    const VolumeDescriptor *entry = volumeChangeEntry(index, &added);
    return entry ? entry->object : 0u;
}

}  // namespace LMState

#endif  // defined(SUSAMUNE_VERSION_LMJ)
