#include "susamune/emulator_persistence.hxx"

#if IS_EMULATOR

#include <Dolphin/CARD.h>
#include <Dolphin/DVD.h>
#include <Dolphin/mem.h>
#include <Dolphin/OS.h>
#include <JSystem/JKernel/JKRHeap.hxx>
#include <SMS/System/Application.hxx>
#include <SMS/System/CardManager.hxx>
#include "susamune/addresses.hxx"

namespace EmulatorPersistence {
namespace {

constexpr u32 kRecordMagic = 0x53554346u;  // 'SUCF'
constexpr u16 kRecordVersion = 1;
constexpr u32 kSectorSize = 0x2000;
constexpr u32 kFileSize = kSectorSize * 2;
constexpr char kFileName[] = "susamune_settings";

struct Record {
    u32 magic;
    u16 version;
    u16 payloadSize;
    u32 generation;
    u32 checksum;
    u32 gameVersion;
    u8 reserved[12];
    SusamuneCfg cfg;
    u8 padding[kSectorSize - 32 - sizeof(SusamuneCfg)];
};
static_assert(sizeof(Record) == kSectorSize, "card record must fill one sector");

// Only diskID is needed. The offset and stride come from the decomp's complete
// CARDControl definition; keep this view tied to its 0x110-byte retail layout.
struct CardControlIdentity {
    u8 pad[0x10c];
    DVDDiskID *diskID;
};
static_assert(sizeof(CardControlIdentity) == 0x110,
              "CARDControl identity view changed");

struct State {
    OSMutex mutex;
    SusamuneCfg cfg;
    DVDDiskID diskID;
    u32 requested;
    u32 completed;
    u32 generation;
    s32 completedStatus;
    s8 activeRecord;
    bool initialSave;
    bool idleObserved;
};

State sStateStorage;
State *sState;
InitResult sInitResult = INIT_WAITING;
u32 sInitError;

constexpr u32 kErrorAllocation = 0x100u;

u32 errorCode(s32 result) {
    return result < 0 ? static_cast<u32>(-result) : static_cast<u32>(result);
}

u8 *align32(u8 *p) {
    return reinterpret_cast<u8 *>((reinterpret_cast<u32>(p) + 31u) & ~31u);
}

void initBlank(SusamuneCfg *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = SUSAMUNE_CFG_MAGIC;
    cfg->version = SUSAMUNE_CFG_VERSION;
    cfg->flags = SUSAMUNE_CFG_FLAG_INPUT_DISPLAY |
                 SUSAMUNE_CFG_FLAG_METADATA_DISPLAY |
                 SUSAMUNE_CFG_FLAG_ILING_PBS;
    cfg->ilingPbs.magic = SUSAMUNE_ILING_PB_MAGIC;
    cfg->ilingPbs.version = SUSAMUNE_ILING_PB_VERSION;
    cfg->ilingPbs.count = SUSAMUNE_ILING_PB_SLOT_COUNT;
    for (u32 i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++) {
        cfg->ilingPbs.values[i] = SUSAMUNE_ILING_PB_UNSET;
    }
}

u32 checksum(Record *record) {
    const u32 saved = record->checksum;
    record->checksum = 0;
    const u8 *bytes = reinterpret_cast<const u8 *>(record);
    u32 hash = 2166136261u;
    for (u32 i = 0; i < sizeof(*record); i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    record->checksum = saved;
    return hash;
}

bool valid(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic &&
           record->version == kRecordVersion &&
           record->payloadSize == sizeof(SusamuneCfg) &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool newer(u32 a, u32 b) { return static_cast<s32>(a - b) > 0; }

s32 probe() {
    s32 sectorSize = 0;
    s32 result;
    do {
        result = CARDProbeEx(CARD_SLOTB, nullptr, &sectorSize);
        if (result == CARD_ERROR_BUSY) OSYieldThread();
    } while (result == CARD_ERROR_BUSY);
    if (result == CARD_ERROR_READY && sectorSize != static_cast<s32>(kSectorSize)) {
        return CARD_ERROR_WRONGDEVICE;
    }
    return result;
}

s32 mount(void *mountWork) {
    s32 result = probe();
    if (result != CARD_ERROR_READY) return result;
    volatile u16 *encoding = reinterpret_cast<volatile u16 *>(
        SUSAMUNE_ADDR_FONT_ENCODING);
    const u16 originalEncoding = *encoding;
    result = CARDMount(CARD_SLOTB, mountWork, nullptr);
    *encoding = originalEncoding;
    if (result != CARD_ERROR_READY) {
        CARDUnmount(CARD_SLOTB);
        return result;
    }
    result = CARDCheck(CARD_SLOTB);
    if (result != CARD_ERROR_READY) CARDUnmount(CARD_SLOTB);
    return result;
}

void unmount() { CARDUnmount(CARD_SLOTB); }

s32 openOrCreate(CARDFileInfo *file) {
    s32 result = CARDOpen(CARD_SLOTB, kFileName, file);
    if (result == CARD_ERROR_NOFILE) {
        result = CARDCreate(CARD_SLOTB, kFileName, kFileSize, file);
    }
    return result;
}

s32 writeRecordLocked() {
    // Saves happen only after the boot option payload has been consumed. The
    // manager refills its sector buffer before every later game operation.
    // service() owns its mutex here, so the slot-A worker cannot touch either
    // borrowed buffer until the slot-B write is finished.
    gpCardManager->unmount();
    void *mountWork = gpCardManager->mCardWorkArea;
    Record *record = reinterpret_cast<Record *>(gpCardManager->mCARDBlock);

    OSLockMutex(&sState->mutex);
    const s8 target = sState->activeRecord == 0 ? 1 : 0;
    const u32 generation = sState->generation + 1;
    memset(record, 0, sizeof(*record));
    record->magic = kRecordMagic;
    record->version = kRecordVersion;
    record->payloadSize = sizeof(SusamuneCfg);
    record->generation = generation;
    record->gameVersion = SUSAMUNE_GAME_VERSION;
    memcpy(&record->cfg, &sState->cfg, sizeof(sState->cfg));
    record->checksum = checksum(record);
    OSUnlockMutex(&sState->mutex);

    s32 result = mount(mountWork);
    if (result != CARD_ERROR_READY) {
        return result;
    }

    CARDFileInfo file;
    result = openOrCreate(&file);
    if (result == CARD_ERROR_READY) {
        result = CARDWrite(&file, record, sizeof(*record),
                           target * kSectorSize);
        const s32 closeResult = CARDClose(&file);
        if (result == CARD_ERROR_READY) result = closeResult;
    }
    unmount();

    if (result == CARD_ERROR_READY) {
        OSLockMutex(&sState->mutex);
        sState->activeRecord = target;
        sState->generation = generation;
        sState->initialSave = false;
        OSUnlockMutex(&sState->mutex);
    }
    return result;
}

void initState() {
    sState = &sStateStorage;
    memset(sState, 0, sizeof(*sState));
    sState->activeRecord = -1;
    OSInitMutex(&sState->mutex);
    initBlank(&sState->cfg);
}

void setIdentity() {
#if defined(SUSAMUNE_VERSION_JP)
    const char region = 'J';
#elif defined(SUSAMUNE_VERSION_US)
    const char region = 'E';
#else
    const char region = 'P';
#endif
    sState->diskID.mName[0] = 'G';
    sState->diskID.mName[1] = 'M';
    sState->diskID.mName[2] = 'S';
    sState->diskID.mName[3] = region;
    sState->diskID.mCompany[0] = 'S';
    sState->diskID.mCompany[1] = 'U';

    CardControlIdentity *cards = reinterpret_cast<CardControlIdentity *>(
        SUSAMUNE_ADDR_CARD_BLOCKS);
    cards[CARD_SLOTB].diskID = &sState->diskID;
}

s32 loadRecords(void *mountWork, Record *record) {
    OSLockMutex(&gpCardManager->mMutex);
    s32 result = mount(mountWork);
    if (result != CARD_ERROR_READY) {
        OSUnlockMutex(&gpCardManager->mMutex);
        return result;
    }

    CARDFileInfo file;
    result = CARDOpen(CARD_SLOTB, kFileName, &file);
    if (result == CARD_ERROR_NOFILE) {
        sState->initialSave = true;
        unmount();
        OSUnlockMutex(&gpCardManager->mMutex);
        return CARD_ERROR_READY;
    }
    if (result != CARD_ERROR_READY) {
        unmount();
        OSUnlockMutex(&gpCardManager->mMutex);
        return result;
    }

    bool haveRecord = false;
    u32 bestGeneration = 0;
    for (s8 slot = 0; slot < 2; slot++) {
        result = CARDRead(&file, record, sizeof(*record),
                          slot * kSectorSize);
        if (result != CARD_ERROR_READY) break;
        if (valid(record) &&
            (!haveRecord || newer(record->generation, bestGeneration))) {
            memcpy(&sState->cfg, &record->cfg, sizeof(sState->cfg));
            bestGeneration = record->generation;
            sState->activeRecord = slot;
            haveRecord = true;
        }
    }
    const s32 closeResult = CARDClose(&file);
    unmount();
    OSUnlockMutex(&gpCardManager->mMutex);
    if (result == CARD_ERROR_READY) result = closeResult;
    if (result != CARD_ERROR_READY) return result;

    if (haveRecord) {
        sState->generation = bestGeneration;
    } else {
        sState->initialSave = true;
    }
    return CARD_ERROR_READY;
}

}  // namespace

InitResult init() {
    if (sInitResult != INIT_WAITING) return sInitResult;
    if (!gpCardManager) return INIT_WAITING;
    const s32 probeResult = probe();
    if (probeResult != CARD_ERROR_READY) {
        sInitError = errorCode(probeResult);
        sInitResult = INIT_UNAVAILABLE;
        return sInitResult;
    }
    initState();

    // During the boot state this is an ordinary expandable 5 MiB heap. The
    // CARD buffers are needed only for this synchronous read and are released
    // before the logo director runs.
    const u32 temporarySize = CARD_WORKAREA + sizeof(Record) + 62;
    u8 *temporary = static_cast<u8 *>(
        JKRHeap::alloc(temporarySize, 32, gpApplication.mCurrentHeap));
    if (!temporary) {
        sInitError = kErrorAllocation;
        sInitResult = INIT_UNAVAILABLE;
        return sInitResult;
    }
    void *mountWork = align32(temporary);
    Record *record = reinterpret_cast<Record *>(
        align32(reinterpret_cast<u8 *>(mountWork) + CARD_WORKAREA));

    setIdentity();
    const s32 loadResult = loadRecords(mountWork, record);
    JKRHeap::free(temporary, gpApplication.mCurrentHeap);
    if (loadResult != CARD_ERROR_READY) {
        sInitError = errorCode(loadResult);
        sInitResult = INIT_UNAVAILABLE;
        return sInitResult;
    }

    sInitResult = INIT_READY;
    return sInitResult;
}

void service() {
    if (sInitResult != INIT_READY) return;

    OSLockMutex(&sState->mutex);
    const u32 ticket = sState->requested;
    const bool pending = ticket != sState->completed;
    OSUnlockMutex(&sState->mutex);
    if (!pending) {
        sState->idleObserved = false;
        return;
    }

    // Never wait behind Sunshine's worker. Its status and completed-read
    // payload remain untouched until the director has seen an idle frame.
    if (!OSTryLockMutex(&gpCardManager->mMutex)) {
        sState->idleObserved = false;
        return;
    }
    if (gpCardManager->mLastStatus == CARD_ERROR_BUSY) {
        sState->idleObserved = false;
        OSUnlockMutex(&gpCardManager->mMutex);
        return;
    }
    if (!sState->idleObserved) {
        sState->idleObserved = true;
        OSUnlockMutex(&gpCardManager->mMutex);
        return;
    }
    sState->idleObserved = false;

    // unmount() normally replaces this with CARDUnmount's result. Keep the
    // result Sunshine's state machine is waiting to observe.
    const s32 gameStatus = gpCardManager->mLastStatus;
    const s32 result = writeRecordLocked();
    gpCardManager->mLastStatus = gameStatus;
    OSUnlockMutex(&gpCardManager->mMutex);

    OSLockMutex(&sState->mutex);
    sState->completedStatus = result;
    sState->completed = ticket;
    OSUnlockMutex(&sState->mutex);
}

SusamuneCfg *lock() {
    if (sInitResult != INIT_READY) return nullptr;
    OSLockMutex(&sState->mutex);
    return &sState->cfg;
}

void unlock() {
    if (sInitResult == INIT_READY) OSUnlockMutex(&sState->mutex);
}

u32 commit() {
    if (sInitResult != INIT_READY) return 0;
    const u32 ticket = ++sState->requested;
    OSUnlockMutex(&sState->mutex);
    return ticket;
}

SaveResult poll(u32 ticket, u32 *error) {
    if (sInitResult != INIT_READY || ticket == 0) return SAVE_ERROR;
    OSLockMutex(&sState->mutex);
    const bool done = static_cast<s32>(sState->completed - ticket) >= 0;
    const s32 status = sState->completedStatus;
    OSUnlockMutex(&sState->mutex);
    if (!done) return SAVE_PENDING;
    if (status == CARD_ERROR_READY) return SAVE_OK;
    if (error) *error = static_cast<u32>(-status);
    return SAVE_ERROR;
}

bool needsInitialSave() {
    if (sInitResult != INIT_READY) return false;
    OSLockMutex(&sState->mutex);
    const bool needed = sState->initialSave;
    OSUnlockMutex(&sState->mutex);
    return needed;
}

u32 initError() { return sInitError; }

}  // namespace EmulatorPersistence

#endif  // IS_EMULATOR
