#if defined(SUSAMUNE_VERSION_LMJ)

#include "lm_state.hxx"

#include "lm_crash.hxx"
#include "susamune/mem2_map.h"
#include "susamune/mod_bin.h"

// The Kuribo build is deliberately freestanding. Clang may still lower a
// small aggregate assignment to memcpy at -Oz, so keep the one runtime helper
// it is allowed to request inside the injected image.
extern "C" void *memcpy(void *destination, const void *source, u32 size) {
    volatile u8 *out = static_cast<volatile u8 *>(destination);
    const volatile u8 *in = static_cast<const volatile u8 *>(source);
    for (u32 i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return destination;
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
constexpr u32 kGameModeGlobal = 0x804A17B0u;
constexpr u32 kGameModeCountGlobal = 0x804A17B4u;
constexpr u32 kMissionModeGlobal = 0x804A17C8u;
constexpr u32 kVolumeListGlobal = 0x80494754u;
constexpr u32 kPadStatusGlobal = 0x80494778u;
constexpr u32 kDvdOutstandingGlobal = 0x80391D98u;
constexpr u32 kAramList0Global = 0x804946F4u;
constexpr u32 kAramList1Global = 0x80494724u;
constexpr u32 kCardBlockGlobal = 0x80495960u;
constexpr u32 kCardControlStride = 0x108u;
constexpr u32 kCardResultOffset = 0x04u;
constexpr u32 kCardResultBusy = 0xFFFFFFFFu;

constexpr u32 kDvdBusyPredicateAddr = 0x80006A5Cu;
constexpr u32 kExpHeapCheckAddr = 0x801CA61Cu;
constexpr u32 kDCInvalidateRangeAddr = 0x801D5DF4u;
constexpr u32 kDCStoreRangeAddr = 0x801D5E58u;
constexpr u32 kOSDisableInterruptsAddr = 0x801D85B0u;
constexpr u32 kOSRestoreInterruptsAddr = 0x801D85D8u;
constexpr u32 kGXInvalidateTexAllAddr = 0x801F1C10u;

constexpr u32 kExpHeapVtable = 0x8038886Cu;
constexpr u32 kMem1Start = 0x80000000u;
constexpr u32 kMem1End = 0x81800000u;
constexpr u32 kSnapshotBase = SUSAMUNE_MEM2_SNAPSHOT_PPC_BASE;
constexpr u32 kSnapshotCapacity = SUSAMUNE_MEM2_SNAPSHOT_SIZE;
constexpr u32 kSnapshotMagic = 0x4C4D5354u;  // 'LMST'
constexpr u32 kSnapshotVersion = 2u;
constexpr u32 kHeaderSize = 0x100u;
constexpr u32 kHeapMetadataStart = 0x3Cu;
constexpr u32 kHeapMetadataEnd = 0x84u;
constexpr u32 kHeapModeOffset = 0x68u;
constexpr u32 kHeapGroupOffset = 0x69u;
constexpr u32 kHeapMetadataSize = kHeapMetadataEnd - kHeapMetadataStart;
constexpr u32 kHeapMetadataOffset = kHeaderSize;
constexpr u32 kInGameFlagsBase = 0x803C7CA0u;
constexpr u32 kInGameFlagsOffset = 0x659u;
constexpr u32 kInGameFlagsSize = 0x20u;
constexpr u32 kStateStaticsOffset =
    kHeapMetadataOffset + kHeapMetadataSize;
constexpr u32 kStateStaticsSize = kInGameFlagsSize;
constexpr u32 kHeapDataOffset =
    (kStateStaticsOffset + kStateStaticsSize + 31u) & ~31u;
constexpr u16 kDPadLeft = 0x0001u;
constexpr u16 kDPadRight = 0x0002u;
constexpr u32 kRequiredStableFrames = 3u;
constexpr u32 kEventStateSave = 0x100u;
constexpr u32 kEventStateLoad = 0x101u;
constexpr u32 kEventStateReject = 0x10Fu;

typedef bool (*BoolFn)();
typedef bool (*HeapCheckFn)(void *);
typedef bool (*DisableInterruptsFn)();
typedef void (*RestoreInterruptsFn)(bool);
typedef void (*VoidFn)();
typedef void (*CacheRangeFn)(void *, u32);

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
    u32 currentHeapGroup;
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
    u32 aramCount0;
    u32 aramCount1;
    u32 dvdOutstanding;
    u32 reserved[5];
};

static_assert(sizeof(SnapshotHeader) == kHeaderSize,
              "LM snapshot header must remain one cache-aligned page");
static_assert(kSnapshotBase + kSnapshotCapacity ==
                  SUSAMUNE_MEM2_CFG_PPC_BASE,
              "LM state must end before the config/crash mailboxes");
static_assert((kHeapDataOffset & 31u) == 0,
              "LM heap payload must be cache-line aligned");

struct FreezeState {
    bool interruptsWereEnabled;
    bool dmaWasEnabled;
};

LMState::Status sStatus = LMState::Status::Empty;
LiveIdentity sLastIdentity = {};
bool sHaveIdentity;
bool sSlotInitialized;
u32 sStableFrames;
u32 sSnapshotSize;
u32 sGeneration;
u16 sPreviousButtons;

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

bool isExpHeap(u32 heap) {
    if (!isMem1Range(heap, kHeapMetadataEnd) ||
        readWord(heap) != kExpHeapVtable) {
        return false;
    }
    const u32 start = readWord(heap + 0x30u);
    const u32 end = readWord(heap + 0x34u);
    const u32 size = readWord(heap + 0x38u);
    return start >= kMem1Start && start <= end && end <= kMem1End &&
           (start & 31u) == 0 && (end & 31u) == 0 && size == end - start;
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

bool buildIdentity(LiveIdentity *identity) {
    identity->heap = readWord(kGameHeapGlobal);
    identity->rootHeap = readWord(kRootHeapGlobal);
    identity->systemHeap = readWord(kSystemHeapGlobal);
    if (!isExpHeap(identity->heap) || !isExpHeap(identity->rootHeap) ||
        !isExpHeap(identity->systemHeap)) {
        identity->heapStart = 0;
        identity->heapEnd = 0;
        identity->heapSize = 0;
        return false;
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
    identity->currentHeapGroup = readByte(kCurrentHeapGroupGlobal);

    const bool distinctHeaps = identity->rootHeap != identity->systemHeap &&
        identity->rootHeap != identity->heap &&
        identity->systemHeap != identity->heap;
    const bool childrenInsideRoot =
        rangeInside(identity->systemHeapStart, identity->systemHeapEnd,
                    identity->rootHeapStart, identity->rootHeapEnd) &&
        rangeInside(identity->heapStart, identity->heapEnd,
                    identity->rootHeapStart, identity->rootHeapEnd);
    const bool childRangesDisjoint =
        identity->systemHeapEnd <= identity->heapStart ||
        identity->heapEnd <= identity->systemHeapStart;
    const bool activeMission = identity->missionMode != 0u &&
        identity->gameMode == identity->missionMode &&
        identity->gameModeCount == 1u &&
        identity->missionMode >= identity->heapStart &&
        identity->missionMode <= identity->heapEnd - 0x1Cu;
    return identity->heapSize <= kSnapshotCapacity - kHeapDataOffset &&
           distinctHeaps && childrenInsideRoot && childRangesDisjoint &&
           validCurrentHeap(identity->currentHeap, *identity) &&
           activeMission && isMem1Range(identity->currentScene, sizeof(u32));
}

bool ioIdle() {
    const bool predicateBusy =
        reinterpret_cast<BoolFn>(kDvdBusyPredicateAddr)();
    return !predicateBusy && readWord(kDvdOutstandingGlobal) == 0u &&
           readWord(kAramList0Global + 8u) == 0u &&
           readWord(kAramList1Global + 8u) == 0u &&
           readWord(kCardBlockGlobal + kCardResultOffset) !=
               kCardResultBusy &&
           readWord(kCardBlockGlobal + kCardControlStride +
                    kCardResultOffset) != kCardResultBusy;
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

void clearWords(void *destination, u32 size) {
    volatile u32 *out = reinterpret_cast<volatile u32 *>(destination);
    for (u32 i = 0; i < size / sizeof(u32); ++i) {
        out[i] = 0;
    }
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
    volatile u16 *dmaControl = reinterpret_cast<volatile u16 *>(0xCC005036u);
    state.dmaWasEnabled = (*dmaControl & 0x8000u) != 0u;
    *dmaControl = static_cast<u16>(*dmaControl & ~0x8000u);
    asm volatile("sync" ::: "memory");
    return state;
}

void freezeEnd(const FreezeState &state) {
    asm volatile("sync" ::: "memory");
    volatile u16 *dmaControl = reinterpret_cast<volatile u16 *>(0xCC005036u);
    const u16 liveControl = *dmaControl;
    *dmaControl = state.dmaWasEnabled
                      ? static_cast<u16>(liveControl | 0x8000u)
                      : static_cast<u16>(liveControl & ~0x8000u);
    asm volatile("sync" ::: "memory");
    reinterpret_cast<RestoreInterruptsFn>(kOSRestoreInterruptsAddr)(
        state.interruptsWereEnabled);
}

void setReject(LMState::Status status, u32 detail) {
    sStatus = status;
    LMCrash::note(kEventStateReject, static_cast<u32>(status), detail);
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
           ((header->heapStart | header->heapEnd) & 31u) == 0u &&
           isMem1Range(header->heapStart, header->heapSize) &&
           header->rootHeapStart < header->rootHeapEnd &&
           header->rootHeapSize ==
               header->rootHeapEnd - header->rootHeapStart &&
           ((header->rootHeapStart | header->rootHeapEnd) & 31u) == 0u &&
           isMem1Range(header->rootHeapStart, header->rootHeapSize) &&
           header->systemHeapStart < header->systemHeapEnd &&
           header->systemHeapSize ==
               header->systemHeapEnd - header->systemHeapStart &&
           ((header->systemHeapStart | header->systemHeapEnd) & 31u) == 0u &&
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
           isMem1Range(header->currentScene, sizeof(u32)) &&
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
    LiveIdentity before;
    if (sStableFrames < kRequiredStableFrames || !buildIdentity(&before) ||
        !sameIdentity(before, sLastIdentity) || !ioIdle()) {
        setReject(LMState::Status::Busy, sStableFrames);
        return;
    }
    const u32 totalSize = kHeapDataOffset + before.heapSize;
    if (totalSize > kSnapshotCapacity) {
        setReject(LMState::Status::TooLarge, totalSize);
        return;
    }
    if (!heapsHealthy(before)) {
        setReject(LMState::Status::BadHeap, before.heap);
        return;
    }

    const FreezeState freeze = freezeBegin();
    LiveIdentity live;
    if (!buildIdentity(&live) || !sameIdentity(before, live) || !ioIdle() ||
        !heapsHealthy(live)) {
        freezeEnd(freeze);
        setReject(LMState::Status::Busy, live.heap);
        return;
    }

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
    header->aramCount0 = readWord(kAramList0Global + 8u);
    header->aramCount1 = readWord(kAramList1Global + 8u);
    header->dvdOutstanding = readWord(kDvdOutstandingGlobal);

    copyWords(reinterpret_cast<void *>(kSnapshotBase + kHeapMetadataOffset),
              reinterpret_cast<void *>(live.heap + kHeapMetadataStart),
              kHeapMetadataSize);
    copyBytes(reinterpret_cast<void *>(kSnapshotBase + kStateStaticsOffset),
              reinterpret_cast<void *>(kInGameFlagsBase +
                                       kInGameFlagsOffset),
              kStateStaticsSize);
    copyWords(reinterpret_cast<void *>(kSnapshotBase + kHeapDataOffset),
              reinterpret_cast<void *>(live.heapStart), live.heapSize);
    header->checksum = snapshotChecksum(header);

    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(header, totalSize);
    asm volatile("sync" ::: "memory");
    header->magic = kSnapshotMagic;
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(header, 32u);
    asm volatile("sync" ::: "memory");
    freezeEnd(freeze);

    sSnapshotSize = totalSize;
    sStatus = LMState::Status::Saved;
    LMCrash::note(kEventStateSave, live.heap, live.heapSize);
}

void loadState() {
    SnapshotHeader *header =
        reinterpret_cast<SnapshotHeader *>(kSnapshotBase);
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
    reinterpret_cast<CacheRangeFn>(kDCInvalidateRangeAddr)(header,
                                                            header->totalSize);
    if (!basicHeaderValid(header) ||
        snapshotChecksum(header) != header->checksum) {
        setReject(LMState::Status::BadCrc, header->checksum);
        return;
    }

    LiveIdentity before;
    if (sStableFrames < kRequiredStableFrames || !buildIdentity(&before) ||
        !sameIdentity(before, sLastIdentity) || !ioIdle()) {
        setReject(LMState::Status::Busy, sStableFrames);
        return;
    }
    if (!headerMatchesLive(header, before)) {
        setReject(LMState::Status::Epoch, before.heap);
        return;
    }
    if (!savedPointerCompatible(header->currentScene,
                                readWord(kCurrentSceneGlobal), header) ||
        !savedPointerCompatible(header->gameMode, readWord(kGameModeGlobal),
                                header)) {
        setReject(LMState::Status::Epoch, header->currentScene);
        return;
    }
    if (!heapsHealthy(before)) {
        setReject(LMState::Status::BadHeap, before.heap);
        return;
    }

    const FreezeState freeze = freezeBegin();
    LiveIdentity live;
    if (!buildIdentity(&live) || !sameIdentity(before, live) || !ioIdle() ||
        !headerMatchesLive(header, live) || !heapsHealthy(live)) {
        freezeEnd(freeze);
        setReject(LMState::Status::Busy, live.heap);
        return;
    }

    copyWords(reinterpret_cast<void *>(live.heap + kHeapMetadataStart),
              reinterpret_cast<void *>(kSnapshotBase + kHeapMetadataOffset),
              kHeapMetadataSize);
    copyWords(reinterpret_cast<void *>(live.heapStart),
              reinterpret_cast<void *>(kSnapshotBase + kHeapDataOffset),
              live.heapSize);

    // This is the first deliberately small static manifest: the verified
    // room/map flag bytes and libc RNG. Root/system allocator state stays live
    // and is an exact epoch gate above because those heaps are not rewound.
    copyBytes(reinterpret_cast<void *>(kInGameFlagsBase +
                                      kInGameFlagsOffset),
              reinterpret_cast<void *>(kSnapshotBase + kStateStaticsOffset),
              kStateStaticsSize);
    writeWord(kRandomStateGlobal, header->randomState);

    // These verified roots select the live scene inside the restored heap.
    // MissionMode and the mounted-volume list are intentionally not rewound;
    // they are the resource-epoch gate that makes cross-room attempts fail
    // closed when the old room's archives are no longer mounted.
    writeWord(kMapValueGlobal, header->mapValue);
    writeWord(kSceneValueGlobal, header->sceneValue);
    if (pointerInSavedHeap(header->currentScene, header) ||
        header->currentScene == 0u) {
        writeWord(kCurrentSceneGlobal, header->currentScene);
    }
    if (pointerInSavedHeap(header->gameMode, header) ||
        header->gameMode == 0u) {
        writeWord(kGameModeGlobal, header->gameMode);
        writeWord(kGameModeCountGlobal, header->gameModeCount);
    }

    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(live.heap + kHeapMetadataStart),
        kHeapMetadataSize);
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(live.heapStart), live.heapSize);
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(kInGameFlagsBase + kInGameFlagsOffset),
        kStateStaticsSize);
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(kRandomStateGlobal), sizeof(u32));
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(kSceneValueGlobal), sizeof(u32));
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(kMapValueGlobal), sizeof(u32));
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(kCurrentSceneGlobal), sizeof(u32));
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(
        reinterpret_cast<void *>(kGameModeGlobal), sizeof(u32) * 2u);
    reinterpret_cast<VoidFn>(kGXInvalidateTexAllAddr)();
    asm volatile("sync" ::: "memory");

    const bool healthyAfter = heapsHealthy(live);
    freezeEnd(freeze);

    sSnapshotSize = header->totalSize;
    sStableFrames = 0u;
    if (!healthyAfter) {
        setReject(LMState::Status::BadHeap, live.heap);
        return;
    }
    sStatus = LMState::Status::Loaded;
    LMCrash::note(kEventStateLoad, live.heap, live.heapSize);
}

void updateStability() {
    LiveIdentity live;
    if (!buildIdentity(&live) || !ioIdle()) {
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

}  // namespace

namespace LMState {

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

}  // namespace LMState

#endif  // defined(SUSAMUNE_VERSION_LMJ)
