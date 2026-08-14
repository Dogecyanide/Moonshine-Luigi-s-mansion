#include "susamune/records_persistence.hxx"

#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "susamune/addresses.hxx"
#include "susamune/susamune_cfg.h"

namespace {

const u32 kCheckpointFrames = 30u * 60u;
const u32 kRetryFrames = 30u * 10u;
const u32 kSaveTimeoutFrames = 30u * 15u;

#if defined(SUSAMUNE_VERSION_JP)
const u8 kRegion = SUSAMUNE_PROGRESS_REGION_JP;
#elif defined(SUSAMUNE_VERSION_US)
const u8 kRegion = SUSAMUNE_PROGRESS_REGION_US;
#else
const u8 kRegion = SUSAMUNE_PROGRESS_REGION_PAL;
#endif

typedef u32 RegionalStats[SUSAMUNE_PROGRESS_REGION_COUNT - 1]
                         [Records::STAT_CAPACITY];
RegionalStats &sOtherRegional = *reinterpret_cast<RegionalStats *>(
    SUSAMUNE_MEM2_REGIONAL_RUNTIME_PPC_BASE);
static_assert(sizeof(RegionalStats) <= SUSAMUNE_REGIONAL_RUNTIME_SIZE,
              "regional stats exceed their MEM2 runtime window");
u32 sSaveSeq;
u32 sWaitFrames;
u32 sSaveDelay;
u32 sLastError;
bool sPersistent;
bool sWritable;
bool sPending;
bool sDirty;
bool sUrgent;
#if !IS_EMULATOR
u8 sResetWritesRemaining;
bool sPendingReset;
#endif

u8 otherIndex(u8 region) { return region < kRegion ? region : region - 1; }

bool validMailbox(const volatile SusamuneProgressCfg *progress) {
    return progress->magic == SUSAMUNE_PROGRESS_MAGIC &&
           progress->version == SUSAMUNE_PROGRESS_VERSION &&
           progress->achievementBytes == SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES &&
           progress->statCount == SUSAMUNE_PROGRESS_STAT_COUNT &&
           progress->regionCount == SUSAMUNE_PROGRESS_REGION_COUNT;
}

void absorbRecords() {
    if (!Records::dirty()) return;

    const bool achievementChanged = Records::achievementDirty();
    Records::clearDirty();

    if (!sDirty) sSaveDelay = kCheckpointFrames;
    sDirty = true;
    if (achievementChanged) {
        sUrgent = true;
        sSaveDelay = 0;
    }
}

#if !IS_EMULATOR
void beginSave() {
    volatile SusamuneProgressCfg *progress = SUSAMUNE_PROGRESS_PPC_PTR;

    Records::stageInto((u8 *)progress->achievements,
                       (u32 *)progress->stats[kRegion]);
    for (u8 region = 0; region < SUSAMUNE_PROGRESS_REGION_COUNT; region++) {
        if (region == kRegion) continue;
        memcpy((void *)progress->stats[region], sOtherRegional[otherIndex(region)],
               sizeof(progress->stats[region]));
    }
    DCStoreRange((void *)progress->achievements,
                 sizeof(progress->achievements) + sizeof(progress->stats));

    sSaveSeq++;
    progress->saveSeq = sSaveSeq;
    DCStoreRange((void *)progress, 32);

    // The mailbox payload now belongs to this request. Live mutations remain
    // in the BSS copy and queue another complete save after acknowledgement.
    sPending = true;
    sPendingReset = sResetWritesRemaining != 0;
    sDirty = false;
    sUrgent = false;
    sWaitFrames = 0;
    sLastError = 0;
}

void pollSave() {
    if (!sPending) return;

    volatile SusamuneProgressCfg *progress = SUSAMUNE_PROGRESS_PPC_PTR;
    DCInvalidateRange((void *)&progress->ackSeq, 32);
    if (progress->ackSeq == sSaveSeq) {
        sPending = false;
        sLastError = progress->status;
        sWaitFrames = 0;
        if (sLastError != 0) {
            sDirty = true;
            sSaveDelay = kRetryFrames;
        } else if (sPendingReset && sResetWritesRemaining != 0) {
            sResetWritesRemaining--;
            if (sResetWritesRemaining != 0) {
                // Commit both A/B generations so fallback cannot resurrect
                // the pre-reset Records state.
                sDirty = true;
                sUrgent = true;
                sSaveDelay = 0;
            }
        }
        sPendingReset = false;
    } else if (++sWaitFrames > kSaveTimeoutFrames) {
        // Keep waiting for the same immutable request. The kernel may be busy
        // streaming the disc; staging another payload here would race it.
        sLastError = 0xffffffffu;
    }
}
#endif

}  // namespace

namespace RecordsPersistence {

void init() {
    memset(sOtherRegional, 0, sizeof(sOtherRegional));
    sSaveSeq = 0;
    sWaitFrames = 0;
    sSaveDelay = 0;
    sLastError = 0;
    sPersistent = false;
    sWritable = false;
    sPending = false;
    sDirty = false;
    sUrgent = false;
#if !IS_EMULATOR
    sResetWritesRemaining = 0;
    sPendingReset = false;
#endif

#if !IS_EMULATOR
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    DCInvalidateRange((void *)cfg, 32);
    if (cfg->magic == SUSAMUNE_CFG_MAGIC &&
        cfg->version == SUSAMUNE_CFG_VERSION &&
        (cfg->flags & SUSAMUNE_CFG_FLAG_PROGRESS)) {
        volatile SusamuneProgressCfg *progress = SUSAMUNE_PROGRESS_PPC_PTR;
        DCInvalidateRange((void *)progress, sizeof(*progress));
        if (validMailbox(progress)) {
            for (u8 region = 0; region < SUSAMUNE_PROGRESS_REGION_COUNT;
                 region++) {
                if (region == kRegion) continue;
                memcpy(sOtherRegional[otherIndex(region)],
                       (const void *)progress->stats[region],
                       sizeof(progress->stats[region]));
            }
            Records::adopt((const u8 *)progress->achievements,
                           (const u32 *)progress->stats[kRegion]);
            Records::reconcileRegionalStats(
                (const u32 (*)[Records::STAT_CAPACITY])progress->stats,
                SUSAMUNE_PROGRESS_REGION_COUNT);
            sSaveSeq = progress->saveSeq;
            sPersistent = true;
            sWritable =
                (progress->flags & SUSAMUNE_PROGRESS_FLAG_WRITABLE) != 0;
        }
    }
#endif

}

void update() {
#if !IS_EMULATOR
    pollSave();
#endif
    absorbRecords();

#if !IS_EMULATOR
    if (!sPersistent || !sWritable || sPending || !sDirty) return;
    if (!sUrgent && sSaveDelay != 0) {
        sSaveDelay--;
        return;
    }
    beginSave();
#endif
}

void checkpoint() {
    absorbRecords();
    if (!sDirty) return;
    sUrgent = true;
    sSaveDelay = 0;
}

void resetAll() {
    Records::resetProgress();
    memset(sOtherRegional, 0, sizeof(sOtherRegional));
    absorbRecords();
    sUrgent = true;
    sSaveDelay = 0;
#if !IS_EMULATOR
    sResetWritesRemaining = 2;
    // An older immutable request must not count toward this reset.
    sPendingReset = false;
#endif
}

u32 stat(Scope scope, Records::StatId id) {
    if (id >= Records::STAT_COUNT) return 0;
    if (scope == SCOPE_GLOBAL) {
        u32 total = Records::stat(id);
        for (u8 i = 0; i < SUSAMUNE_PROGRESS_REGION_COUNT - 1; i++) {
            const u32 amount = sOtherRegional[i][id];
            if (amount > 0xffffffffu - total) return 0xffffffffu;
            total += amount;
        }
        return total;
    }
    if (scope >= SCOPE_JP && scope <= SCOPE_PAL) {
        const u8 region = scope - SCOPE_JP;
        return region == kRegion ? Records::stat(id)
                                 : sOtherRegional[otherIndex(region)][id];
    }
    return 0;
}

const char *scopeName(Scope scope) {
    static const char names[] = "Global\0JP\0US\0PAL";
    static const u8 offsets[] = {0, 7, 10, 13};
    return scope < SCOPE_COUNT ? names + offsets[scope] : "";
}

Scope currentRegionScope() { return (Scope)(SCOPE_JP + kRegion); }

bool persistent() { return sPersistent; }
bool writable() { return sWritable; }
bool pending() { return sPending; }
bool dirty() { return sDirty || sPending; }
u32 lastError() { return sLastError; }

}  // namespace RecordsPersistence
