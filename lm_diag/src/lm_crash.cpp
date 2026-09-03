#include "lm_crash.hxx"

#include "Dolphin/OS.h"
#include "susamune/crash_report.h"
#include "susamune/mod_bin.h"

#if defined(SUSAMUNE_VERSION_LMJ)

namespace {

typedef void (*UserCallback)(u16, OSContext *, u32, u32);
typedef void (*SetCallbackFn)(UserCallback);
typedef void *(*GetCurrentThreadFn)();
typedef void (*CacheRangeFn)(void *, u32);

const u32 kSetPreUserCallbackAddr = 0x801D4124u;
const u32 kPreUserCallbackAddr = 0x804A2074u;
const u32 kOSGetCurrentThreadAddr = 0x801DAE58u;
const u32 kDCInvalidateRangeAddr = 0x801D5DF4u;
const u32 kDCStoreRangeAddr = 0x801D5E58u;

const u32 kRootHeapAddr = 0x804A0B90u;
const u32 kSystemHeapAddr = 0x804A0B94u;
const u32 kGameHeapAddr = 0x804A0B98u;
const u32 kCurrentHeapAddr = 0x804A1FF4u;
const u32 kScenePointerAddr = 0x80498B18u;
const u32 kSceneIdAddr = 0x804A0C20u;
const u32 kMapNumberAddr = 0x804A0C48u;
const u32 kGameModeAddr = 0x804A17B0u;
const u32 kGameModeCountAddr = 0x804A17B4u;
const u32 kMissionModeAddr = 0x804A17C8u;
const u32 kDvdStateAddr = 0x80391D98u;
const u32 kAramCommandListAddr = 0x804946F4u;
const u32 kAramPieceCommandListAddr = 0x80494724u;

const u32 kMem1Start = 0x80000000u;
const u32 kMem1End = 0x81800000u;
const u32 kMem2Start = 0x90000000u;
const u32 kMem2End = 0x94000000u;
const u32 kDvdStateSize = 0x28u;
const u32 kAramStateSize =
    kAramPieceCommandListAddr + 0x0Cu - kAramCommandListAddr;

static_assert(kDvdStateSize <= SUSAMUNE_CRASH_DIRECTOR_SIZE,
              "DVD state exceeds the crash-report window");
static_assert(kAramStateSize <= SUSAMUNE_CRASH_MARIO_SIZE,
              "ARAM state exceeds the crash-report window");

UserCallback sPreviousHandler;
bool sInstalled;
bool sCapturing;
u32 sPhaseSequence;

inline u32 readWord(u32 address) {
    return *reinterpret_cast<volatile u32 *>(address);
}

inline void invalidateRange(void *address, u32 size) {
    reinterpret_cast<CacheRangeFn>(kDCInvalidateRangeAddr)(address, size);
}

inline void storeRange(void *address, u32 size) {
    reinterpret_cast<CacheRangeFn>(kDCStoreRangeAddr)(address, size);
}

void readTimeBase(unsigned int *high, unsigned int *low) {
    unsigned int nextHigh;
    do {
        asm volatile("mftbu %0" : "=r"(*high));
        asm volatile("mftb %0" : "=r"(*low));
        asm volatile("mftbu %0" : "=r"(nextHigh));
    } while (*high != nextHigh);
}

bool readableRange(u32 address, u32 size) {
    if (size == 0 || address + size < address) {
        return false;
    }
    return (address >= kMem1Start && address + size <= kMem1End) ||
           (address >= kMem2Start && address + size <= kMem2End);
}

void clearBytes(void *destination, u32 size) {
    volatile u8 *out = static_cast<volatile u8 *>(destination);
    for (u32 i = 0; i < size; ++i) {
        out[i] = 0;
    }
}

bool copyReadable(void *destination, u32 address, u32 size) {
    if (!readableRange(address, size)) {
        return false;
    }
    volatile u8 *out = static_cast<volatile u8 *>(destination);
    const volatile u8 *in = reinterpret_cast<const volatile u8 *>(address);
    for (u32 i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return true;
}

u32 checksum(const SusamuneCrashReport *report) {
    const u8 *bytes = reinterpret_cast<const u8 *>(report);
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < sizeof(*report); ++i) {
        const bool checksumByte =
            i >= __builtin_offsetof(SusamuneCrashReport, checksum) &&
            i < __builtin_offsetof(SusamuneCrashReport, checksum) + 4u;
        crc ^= checksumByte ? 0u : bytes[i];
        for (u32 bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void captureBacktrace(SusamuneCrashReport *report, u32 stackPointer) {
    clearBytes(report->backtrace, sizeof(report->backtrace));
    u32 frame = stackPointer;
    for (u32 i = 0; i < SUSAMUNE_CRASH_BACKTRACE_COUNT; ++i) {
        if ((frame & 3u) != 0 || !readableRange(frame, 8u)) {
            break;
        }
        const volatile u32 *words =
            reinterpret_cast<const volatile u32 *>(frame);
        const u32 next = words[0];
        report->backtrace[i].stackPointer = frame;
        report->backtrace[i].returnAddress = words[1];
        if (next <= frame || next - frame > 0x00100000u) {
            break;
        }
        frame = next;
    }
}

void captureLmState(SusamuneCrashReport *report) {
    report->appAddress = readWord(kRootHeapAddr);
    report->appDirector = readWord(kSystemHeapAddr);
    report->appHeap = readWord(kGameHeapAddr);
    report->appContext = readWord(kCurrentHeapAddr);
    report->prevScene = readWord(kScenePointerAddr);
    report->currentScene = readWord(kSceneIdAddr);
    report->nextScene = readWord(kMapNumberAddr);
    report->cutSceneId = readWord(kGameModeAddr);
    report->marDirector = readWord(kMissionModeAddr);
    report->mario = readWord(kDvdStateAddr);
    report->camera = readWord(kDvdStateAddr + 4u);
    report->directorReady = readWord(kGameModeCountAddr);
    report->directorStateAreaEpisode = readWord(kDvdStateAddr + 8u);
    report->directorGameState = readWord(kAramCommandListAddr + 8u);
    report->directorDemoStates = readWord(kAramPieceCommandListAddr + 8u);
    report->directorCollectedShine = 0;

    report->directorWindowBase = kDvdStateAddr;
    report->directorWindowSize = 0;
    clearBytes(report->directorWindow, sizeof(report->directorWindow));
    if (copyReadable(report->directorWindow, kDvdStateAddr, kDvdStateSize)) {
        report->captureFlags |= SUSAMUNE_CRASH_FLAG_DIRECTOR;
        report->directorWindowSize = kDvdStateSize;
    }

    report->marioWindowBase = kAramCommandListAddr;
    report->marioWindowSize = 0;
    clearBytes(report->marioWindow, sizeof(report->marioWindow));
    if (copyReadable(report->marioWindow, kAramCommandListAddr,
                     kAramStateSize)) {
        report->captureFlags |= SUSAMUNE_CRASH_FLAG_MARIO;
        report->marioWindowSize = kAramStateSize;
    }
}

void captureException(u16 exception, OSContext *context, u32 dsisr, u32 dar) {
    SusamuneCrashReport *report = SUSAMUNE_CRASH_PPC_PTR;
    if (!sCapturing && context && report->magic == SUSAMUNE_CRASH_MAGIC &&
        report->version == SUSAMUNE_CRASH_VERSION &&
        report->reportSize == sizeof(*report) &&
        report->state == SUSAMUNE_CRASH_STATE_ARMED) {
        sCapturing = true;
        report->state = SUSAMUNE_CRASH_STATE_WRITING;
        storeRange(report, 32u);
        asm volatile("sync" ::: "memory");

        report->exception = exception;
        report->captureFlags = 0;
        report->dsisr = dsisr;
        report->dar = dar;
        readTimeBase(&report->timeBaseHigh, &report->timeBaseLow);
        for (u32 i = 0; i < 32u; ++i) {
            report->gpr[i] = context->mGPR[i];
        }
        report->cr = context->mCR;
        report->lr = context->mLR;
        report->ctr = context->mCTR;
        report->xer = context->mXER;
        report->srr0 = context->mSRR0;
        report->srr1 = context->mSRR1;
        report->contextMode = context->mMode;
        report->contextState = context->mState;
        report->currentThread = reinterpret_cast<u32>(
            reinterpret_cast<GetCurrentThreadFn>(kOSGetCurrentThreadAddr)());

        captureLmState(report);
        captureBacktrace(report, context->mGPR[1]);

        report->stackBase = context->mGPR[1];
        report->stackSize = 0;
        clearBytes(report->stack, sizeof(report->stack));
        if (copyReadable(report->stack, report->stackBase,
                         sizeof(report->stack))) {
            report->captureFlags |= SUSAMUNE_CRASH_FLAG_STACK;
            report->stackSize = sizeof(report->stack);
        }

        report->pcWindowBase = (context->mSRR0 - 32u) & ~3u;
        report->pcWindowSize = 0;
        clearBytes(report->pcWindow, sizeof(report->pcWindow));
        if (context->mSRR0 >= 32u &&
            copyReadable(report->pcWindow, report->pcWindowBase,
                         sizeof(report->pcWindow))) {
            report->captureFlags |= SUSAMUNE_CRASH_FLAG_PC_WINDOW;
            report->pcWindowSize = sizeof(report->pcWindow);
        }

        report->lrWindowBase = (context->mLR - 32u) & ~3u;
        report->lrWindowSize = 0;
        clearBytes(report->lrWindow, sizeof(report->lrWindow));
        if (context->mLR >= 32u &&
            copyReadable(report->lrWindow, report->lrWindowBase,
                         sizeof(report->lrWindow))) {
            report->captureFlags |= SUSAMUNE_CRASH_FLAG_LR_WINDOW;
            report->lrWindowSize = sizeof(report->lrWindow);
        }

        report->state = SUSAMUNE_CRASH_STATE_READY;
        report->checksum = 0;
        report->checksum = checksum(report);
        storeRange(reinterpret_cast<u8 *>(report) + 32u,
                   sizeof(*report) - 32u);
        asm volatile("sync" ::: "memory");
        storeRange(report, 32u);
        asm volatile("sync" ::: "memory");
    }

    if (sPreviousHandler && sPreviousHandler != captureException) {
        sPreviousHandler(exception, context, dsisr, dar);
    }
}

}  // namespace

namespace LMCrash {

void init() {
    if (sInstalled) {
        return;
    }

    SusamuneCrashReport *report = SUSAMUNE_CRASH_PPC_PTR;
    invalidateRange(report, sizeof(*report));
    if (report->magic != SUSAMUNE_CRASH_MAGIC ||
        report->version != SUSAMUNE_CRASH_VERSION ||
        report->reportSize != sizeof(*report) ||
        report->state != SUSAMUNE_CRASH_STATE_ARMED ||
        report->gameId != SUSAMUNE_MOD_GAME_ID_LMJ) {
        return;
    }

    sPreviousHandler =
        reinterpret_cast<UserCallback>(readWord(kPreUserCallbackAddr));
    reinterpret_cast<SetCallbackFn>(kSetPreUserCallbackAddr)(captureException);
    sInstalled = true;
    note(SUSAMUNE_CRASH_EVENT_APP_INIT, SUSAMUNE_MOD_GAME_ID_LMJ,
         reinterpret_cast<u32>(sPreviousHandler));
}

void note(u32 event, u32 arg0, u32 arg1) {
    SusamuneCrashReport *report = SUSAMUNE_CRASH_PPC_PTR;
    if (report->magic != SUSAMUNE_CRASH_MAGIC ||
        report->version != SUSAMUNE_CRASH_VERSION ||
        report->reportSize != sizeof(*report) ||
        report->state != SUSAMUNE_CRASH_STATE_ARMED) {
        return;
    }

    const u32 sequence = report->breadcrumbSeq;
    SusamuneCrashBreadcrumb &entry =
        report->breadcrumbs[sequence % SUSAMUNE_CRASH_BREADCRUMB_COUNT];
    entry.event = event;
    readTimeBase(&entry.timeBaseHigh, &entry.timeBaseLow);
    entry.arg0 = arg0;
    entry.arg1 = arg1;
    report->breadcrumbSeq = sequence + 1u;
    if (report->breadcrumbCount < SUSAMUNE_CRASH_BREADCRUMB_COUNT) {
        ++report->breadcrumbCount;
    }
}

void phase(u32 action, u32 phaseValue, u32 arg0, u32 arg1) {
    SusamuneCrashReport *report = SUSAMUNE_CRASH_PPC_PTR;
    if (report->magic != SUSAMUNE_CRASH_MAGIC ||
        report->version != SUSAMUNE_CRASH_VERSION ||
        report->reportSize != sizeof(*report) ||
        report->state != SUSAMUNE_CRASH_STATE_ARMED) {
        return;
    }

    SusamunePhaseTrace *trace = SUSAMUNE_PHASE_TRACE_PPC_PTR;
    const u32 sequence = (sPhaseSequence += 2u);

    // Publish an invalid odd generation first. The ARM never accepts a cache
    // line caught between the two stores, even if the PPC locks immediately.
    trace->magic = SUSAMUNE_PHASE_TRACE_MAGIC;
    trace->sequenceBegin = sequence - 1u;
    trace->sequenceEnd = sequence - 1u;
    storeRange(trace, sizeof(*trace));
    asm volatile("sync" ::: "memory");

    trace->action = action;
    trace->phase = phaseValue;
    trace->phaseInverse = ~phaseValue;
    trace->arg0 = arg0;
    trace->arg1 = arg1;
    trace->sequenceBegin = sequence;
    trace->sequenceEnd = sequence;
    storeRange(trace, sizeof(*trace));
    asm volatile("sync" ::: "memory");
}

}  // namespace LMCrash

#endif
