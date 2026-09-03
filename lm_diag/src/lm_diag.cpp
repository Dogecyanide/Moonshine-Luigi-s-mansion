#if defined(SUSAMUNE_VERSION_LMJ)

#include "Dolphin/types.h"
#include "lm_crash.hxx"
#include "lm_state.hxx"
#include "susamune/mod_bin.h"

namespace {

// GLMJ01 retail addresses.  Keeping every call explicit makes this payload
// independent of the inherited Sunshine symbol maps and C++ object graph.
const u32 kOSArenaLoAddr = 0x804A0A18u;
const u32 kOSArenaHiAddr = 0x804A20B8u;

const u32 kLMRootHeapAddr = 0x804A0B90u;
const u32 kLMSystemHeapAddr = 0x804A0B94u;
const u32 kLMGameHeapAddr = 0x804A0B98u;
const u32 kJKRCurrentHeapAddr = 0x804A1FF4u;
const u32 kDirectPrintPtrAddr = 0x804A2088u;

const u32 kLMFrameBeginAddr = 0x800076D8u;
const u32 kLMChangeFrameBufferAddr = 0x800077E8u;
const u32 kLMConditionalTailAddr = 0x80007FB4u;
const u32 kLMGameLoopAddr = 0x8000B4E8u;
const u32 kLMLoopTailClockAddr = 0x80005E04u;
const u32 kLMLoopTailSyncAddr = 0x8000B378u;
const u32 kLMOuterCleanupAddr = 0x8000AC78u;
const u32 kLMOuterRestartAddr = 0x80006070u;
const u32 kLMPreMainUpdateAddr = 0x8000ACA4u;
const u32 kLMPostMainUpdateAddr = 0x80008004u;
const u32 kLMMainSceneStepAddr = 0x8000B248u;
const u32 kLMMainDrawStateAddr = 0x804A0C44u;
const u32 kLMDefaultOrthoViewAddr = 0x800078FCu;
const u32 kGXCopyDispAddr = 0x801F045Cu;
const u32 kGXDrawDoneAddr = 0x801EF5F0u;
const u32 kGXLoadPosMtxImmAddr = 0x801F4C30u;
const u32 kGXLoadNrmMtxImmAddr = 0x801F4C6Cu;
const u32 kDCInvalidateRangeAddr = 0x801D5DF4u;
const u32 kDCFlushRangeAddr = 0x801D5E24u;
const u32 kDirectPrintEraseAddr = 0x801D4294u;
const u32 kDirectPrintChangeFrameBufferAddr = 0x801D4830u;
const u32 kDirectPrintDrawStringAddr = 0x801D49F8u;
const u32 kExpHeapLargestFreeAddr = 0x801CA4D0u;
const u32 kExpHeapTotalFreeAddr = 0x801CA53Cu;
const u32 kExpHeapCheckAddr = 0x801CA61Cu;
const u32 kExpHeapVtable = 0x8038886Cu;

const u32 kModEnd = SUSAMUNE_MOD_BASE_LMJ + SUSAMUNE_MOD_REGION_SIZE;
const u32 kCanaryAddr = kModEnd - 0x10u;
const u32 kMem1Start = 0x80000000u;
const u32 kMem1End = 0x81800000u;
const u32 kXfbWidth = 640u;
const u32 kXfbHeight = 480u;
const u32 kXfbRowBytes = kXfbWidth * 2u;
const u32 kXfbSize = kXfbRowBytes * kXfbHeight;
const u16 kPanelTop = 8u;      // JUT logical rows: 16 physical XFB rows.
const u32 kHeartbeatTop = 16u; // Raw YUYV path uses physical XFB rows.
const u32 kCanary[4] = {
    0x474C4D4Au,  // GLMJ
    0x4D454D31u,  // MEM1
    0x43414E31u,  // CAN1
    0x43414E32u,  // CAN2
};

static_assert(SUSAMUNE_MOD_REGION_SIZE == 0x80000u,
              "GLMJ diagnostic expects Moonshine's 512 KiB window");
static_assert(SUSAMUNE_ARENA_RESERVE_SIZE == 0x82000u,
              "GLMJ arena reserve must include the retail debug stack");
static_assert(kCanaryAddr >=
                  SUSAMUNE_MOD_BASE_LMJ + SUSAMUNE_MOD_SCRATCH_OFFSET,
              "diagnostic canary must stay in the reserved scratch tail");
static_assert(kCanaryAddr + sizeof(kCanary) <= kModEnd,
              "diagnostic canary exceeds the reserved mod window");
static_assert(kHeartbeatTop + 16u <= kXfbHeight,
              "diagnostic heartbeat exceeds the framebuffer");

typedef void (*VoidFn)();
typedef void (*VoidPtrFn)(void *);
typedef void (*VoidU32Fn)(u32);
typedef f32 (*MatrixPtr)[4];
typedef void (*GXLoadMtxFn)(MatrixPtr, u32);
typedef void (*GXCopyDispFn)(void *, bool);
typedef void (*CacheRangeFn)(void *, u32);
typedef void (*DirectPrintEraseFn)(void *, u16, u16, u16, u16);
typedef void (*DirectPrintChangeFrameBufferFn)(void *, void *, u16, u16);
typedef void (*DirectPrintDrawStringFn)(void *, u16, u16, const char *, ...);
typedef u32 (*ExpHeapSizeFn)(void *);
typedef bool (*ExpHeapCheckFn)(void *);
typedef u32 (*RetailCall8Fn)(u32, u32, u32, u32, u32, u32, u32, u32);

struct HeapSample {
    u32 pointer;
    u32 largestFree;
    u32 totalFree;
    bool valid;
};

u32 sFrames;
u32 sSystemMinimum;
u32 sGameMinimum;
u32 sInitialArenaLo;
u32 sInitialArenaHi;
u32 sRaisedArenaLo;
bool sSystemSampled;
bool sGameSampled;
bool sArenaCaptured;
bool sFloorObserved;
bool sFloorOk;
bool sCanaryReady;
bool sCanaryOk;
bool sHeapCheckReady;
bool sHeapCheckOk;

inline u32 readWord(u32 address) {
    return *reinterpret_cast<volatile u32 *>(address);
}

inline u8 readByte(u32 address) {
    return *reinterpret_cast<volatile u8 *>(address);
}

inline bool isMem1Range(u32 address, u32 size) {
    return size <= kMem1End - kMem1Start && address >= kMem1Start &&
           address <= kMem1End - size && (address & 3u) == 0;
}

inline bool isMem1Pointer(u32 address) {
    return isMem1Range(address, sizeof(u32));
}

bool isExpHeapPointer(u32 address) {
    if (!isMem1Range(address, 0x84u) ||
        readWord(address) != kExpHeapVtable) {
        return false;
    }

    const u32 start = readWord(address + 0x30u);
    const u32 end = readWord(address + 0x34u);
    const u32 size = readWord(address + 0x38u);
    return start >= kMem1Start && start <= end && end <= kMem1End &&
           (start & 3u) == 0 && (end & 3u) == 0 && size == end - start;
}

inline volatile u32 *canaryWords() {
    return reinterpret_cast<volatile u32 *>(kCanaryAddr);
}

HeapSample sampleHeap(u32 globalAddress) {
    HeapSample sample = {readWord(globalAddress), 0, 0, false};
    if (!isExpHeapPointer(sample.pointer)) {
        return sample;
    }

    sample.valid = true;
    sample.largestFree =
        reinterpret_cast<ExpHeapSizeFn>(kExpHeapLargestFreeAddr)(
            reinterpret_cast<void *>(sample.pointer));
    sample.totalFree =
        reinterpret_cast<ExpHeapSizeFn>(kExpHeapTotalFreeAddr)(
            reinterpret_cast<void *>(sample.pointer));
    return sample;
}

void updateMinimum(u32 value, u32 *minimum, bool *sampled) {
    if (!*sampled || value < *minimum) {
        *minimum = value;
    }
    *sampled = true;
}

void sampleFloorAndCanary() {
    const u32 root = readWord(kLMRootHeapAddr);
    if (!root) {
        return;
    }

    bool floorNow = false;
    if (isExpHeapPointer(root)) {
        const u32 heapStart = readWord(root + 0x30u);
        const u32 heapEnd = readWord(root + 0x34u);
        floorNow = root >= kModEnd && heapStart >= kModEnd &&
                   heapStart <= heapEnd && heapEnd <= kMem1End;
    }

    if (!sFloorObserved) {
        sFloorObserved = true;
        sFloorOk = floorNow;
    } else if (!floorNow) {
        sFloorOk = false;
    }

    // A bad floor means this address may belong to a live retail heap.  Never
    // write a test pattern until both the root object and its managed range
    // prove that the entire Moonshine window is outside the heap.
    if (sFloorOk && !sCanaryReady) {
        canaryWords()[0] = kCanary[0];
        canaryWords()[1] = kCanary[1];
        canaryWords()[2] = kCanary[2];
        canaryWords()[3] = kCanary[3];
        sCanaryReady = true;
        sCanaryOk = true;
    }

    if (sCanaryReady &&
        (canaryWords()[0] != kCanary[0] ||
         canaryWords()[1] != kCanary[1] ||
         canaryWords()[2] != kCanary[2] ||
         canaryWords()[3] != kCanary[3])) {
        sCanaryOk = false;
    }
}

void sampleHeapChecks(const HeapSample &system, const HeapSample &game) {
    if (!system.valid || !game.valid) {
        // LM legitimately tears down and recreates its game heap at room
        // boundaries.  Preserve the last structural result during that gap;
        // only JKRExpHeap::check itself is allowed to latch corruption.
        return;
    }

    if (++sFrames < 60u) {
        return;
    }
    sFrames = 0;

    const bool systemOk =
        reinterpret_cast<ExpHeapCheckFn>(kExpHeapCheckAddr)(
            reinterpret_cast<void *>(system.pointer));
    const bool gameOk = reinterpret_cast<ExpHeapCheckFn>(kExpHeapCheckAddr)(
        reinterpret_cast<void *>(game.pointer));
    if (!sHeapCheckReady) {
        sHeapCheckReady = true;
        sHeapCheckOk = systemOk && gameOk;
    } else if (!systemOk || !gameOk) {
        sHeapCheckOk = false;
    }
}

const char *status(bool ready, bool ok) {
    return !ready ? "WAIT" : ok ? "OK" : "BAD";
}

u32 displayKiB(u32 bytes) {
    const u32 kib = bytes >> 10;
    // JUTDirectPrint's retail formatter owns a 256-byte buffer.  Valid LM
    // heaps are far below this cap; clamping also keeps a corrupted return
    // value from lengthening the diagnostic string beyond that buffer.
    return kib <= 99999u ? kib : 99999u;
}

void drawPanel(void *directPrint, void *xfb, const HeapSample &system,
               const HeapSample &game) {
    const u32 root = readWord(kLMRootHeapAddr);
    const bool rootReadable = isExpHeapPointer(root);
    const u32 rootStart = rootReadable ? readWord(root + 0x30u) : 0;
    const u32 rootEnd = rootReadable ? readWord(root + 0x34u) : 0;
    const u32 current = readWord(kJKRCurrentHeapAddr);
    const u32 group = isExpHeapPointer(current) ? readByte(current + 0x69u) : 0;

    // JUTDirectPrint writes its built-in 6x7 font straight into the copied
    // YUYV framebuffer.  It has no resource-font or heap dependency.  At a
    // 640-pixel XFB it treats these as 320x240 logical coordinates.
    reinterpret_cast<DirectPrintChangeFrameBufferFn>(
        kDirectPrintChangeFrameBufferAddr)(directPrint, xfb, kXfbWidth,
                                            kXfbHeight);
    reinterpret_cast<DirectPrintEraseFn>(kDirectPrintEraseAddr)(
        directPrint, 0, kPanelTop, 320, 82);
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, kPanelTop + 2u,
        "LM STATE X0.3.13 F:%s C:%s H:%s",
        status(sFloorObserved, sFloorOk), status(sCanaryReady, sCanaryOk),
        status(sHeapCheckReady, sHeapCheckOk));
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, kPanelTop + 9u,
        "S:%s ST%lu SZ%luK G:%s %08lX", LMState::statusText(),
        LMState::stableFrames(), displayKiB(LMState::snapshotKiB() << 10),
        LMState::gateText(), LMState::gateValue());
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, kPanelTop + 16u,
        "E:%s M%08lX %08lX>%08lX", LMState::epochText(),
        LMState::epochMask(), LMState::epochSaved(), LMState::epochLive());
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, kPanelTop + 23u,
        "V:%s S%lu>L%lu -%lu +%lu F%lu/%lu",
        LMState::volumeTopologyText(),
        LMState::volumeSavedCount(), LMState::volumeLiveCount(),
        LMState::volumeRemovedCount(), LMState::volumeAddedCount(),
        LMState::volumeSavedFault(), LMState::volumeLiveFault());
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, kPanelTop + 30u,
        "V%s%s O:%s B:%s %08lX", LMState::volumeChangeKind(0u),
        LMState::volumeChangeName(0u),
        LMState::volumeChangeObjectOwnerText(0u),
        LMState::volumeChangeBackingOwnerText(0u),
        LMState::volumeChangeObject(0u));
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, kPanelTop + 37u,
        "V%s%s O:%s B:%s %08lX", LMState::volumeChangeKind(1u),
        LMState::volumeChangeName(1u),
        LMState::volumeChangeObjectOwnerText(1u),
        LMState::volumeChangeBackingOwnerText(1u),
        LMState::volumeChangeObject(1u));
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, kPanelTop + 44u,
        "VC %08lX>%08lX D%08lX>%08lX", LMState::volumeSavedCurrent(),
        LMState::volumeLiveCurrent(), LMState::volumeSavedDir(),
        LMState::volumeLiveDir());
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, kPanelTop + 51u,
        "ROOT %08lX %08lX-%08lX\n"
        "SYS  %08lX L/T/M %lu/%lu/%luK\n"
        "GAME %08lX L/T/M %lu/%lu/%luK\n"
        "CUR %08lX G%lu A %08lX>%08lX H%08lX",
        root, rootStart, rootEnd, system.pointer,
        displayKiB(system.largestFree), displayKiB(system.totalFree),
        displayKiB(sSystemMinimum), game.pointer,
        displayKiB(game.largestFree), displayKiB(game.totalFree),
        displayKiB(sGameMinimum), current, group, sInitialArenaLo,
        sRaisedArenaLo, sInitialArenaHi);
}

void drawRawHeartbeat(void *xfb, bool directPrintReady) {
    // This block is deliberately independent of JUTDirectPrint.  If the text
    // path ever fails, a capture still proves that the post-copy hook ran.
    // The alternating neutral YUYV pairs are safe on every NTSC capture path.
    volatile u32 *const words = reinterpret_cast<volatile u32 *>(xfb);
    const u32 white = 0xEB80EB80u;
    const u32 black = 0x10801080u;
    const u32 xPair = (kXfbWidth / 2u) - 16u;
    const u32 stride = kXfbWidth / 2u;
    for (u32 y = 0; y < 16u; ++y) {
        for (u32 x = 0; x < 16u; ++x) {
            const bool checker = ((x >> 2) ^ (y >> 2)) & 1u;
            words[(kHeartbeatTop + y) * stride + xPair + x] =
                (checker == directPrintReady) ? white : black;
        }
    }
    void *const heartbeatStart = reinterpret_cast<u8 *>(xfb) +
                                 kHeartbeatTop * kXfbRowBytes;
    reinterpret_cast<CacheRangeFn>(kDCFlushRangeAddr)(heartbeatStart,
                                                       16u * kXfbRowBytes);
}

void sampleDiagnostic(HeapSample *system, HeapSample *game) {
    sampleFloorAndCanary();
    *system = sampleHeap(kLMSystemHeapAddr);
    *game = sampleHeap(kLMGameHeapAddr);
    if (system->valid) {
        updateMinimum(system->totalFree, &sSystemMinimum, &sSystemSampled);
    }
    if (game->valid) {
        updateMinimum(game->totalFree, &sGameMinimum, &sGameSampled);
    }
    sampleHeapChecks(*system, *game);
}

}  // namespace

// Replaces GLMJ01's two-instruction OSGetArenaLo getter.  Because patches.py
// installs a plain branch, returning here goes directly to the retail caller.
// The threshold makes repeated calls safe after createRoot consumes the arena.
extern "C" void *getArenaLo() {
    u32 arenaLo = readWord(kOSArenaLoAddr);
    const u32 rawArenaLo = arenaLo;
    if (arenaLo < kModEnd) {
        arenaLo += SUSAMUNE_ARENA_RESERVE_SIZE;
    }
    if (!sArenaCaptured) {
        sInitialArenaLo = rawArenaLo;
        sInitialArenaHi = readWord(kOSArenaHiAddr);
        sRaisedArenaLo = arenaLo;
        sArenaCaptured = true;
    }
    return reinterpret_cast<void *>(arenaLo);
}

// Services state requests only after the complete retail presenter returns.
// Moonshine uses the same kind of after-draw boundary: restoring from inside
// GXCopyDisp left LM's VI/retrace tail observing a mixture of two timelines.
extern "C" void diagnosticChangeFrameBuffer() {
    LMState::presenterEnter();
    reinterpret_cast<VoidFn>(kLMChangeFrameBufferAddr)();
    LMState::presenterAfterRetail();
    LMState::presenterBeforeTick();
    LMState::tick();
    LMState::presenterAfterTick();
}

// These main-loop calls are the first useful boundaries after a restore.
// Persistent entry/exit phases survive a hard lock without requiring an
// exception, so one hardware run can identify the first stalled game step.
extern "C" void diagnosticFrameBegin() {
    LMState::postLoadMilestone(0x88u);
    reinterpret_cast<VoidFn>(kLMFrameBeginAddr)();
    LMState::postLoadMilestone(0x89u);
}

extern "C" void diagnosticMainSceneStep() {
    LMState::postLoadMilestone(0x8Au);
    reinterpret_cast<VoidFn>(kLMMainSceneStepAddr)();
    LMState::postLoadMilestone(0x8Bu);
}

// Split LM's first restored draw into GX matrix setup, scene callback, and
// projection reset. These wrappers are always transparent outside tracing.
extern "C" void diagnosticFirstPosMatrix(MatrixPtr matrix, u32 index) {
    LMState::postLoadMilestone(0xA0u);
    reinterpret_cast<GXLoadMtxFn>(kGXLoadPosMtxImmAddr)(matrix, index);
    LMState::postLoadMilestone(0xA1u);
}

extern "C" void diagnosticLastNrmMatrix(MatrixPtr matrix, u32 index) {
    LMState::postLoadMilestone(0xA2u);
    reinterpret_cast<GXLoadMtxFn>(kGXLoadNrmMtxImmAddr)(matrix, index);
    LMState::postLoadMilestone(0xA3u);
}

extern "C" void diagnosticSceneDraw(void *scene) {
    const u32 callback = readWord(reinterpret_cast<u32>(scene) + 0x1Cu);
    LMState::postLoadDetail(0xA4u, readWord(kLMMainDrawStateAddr), callback);
    // Retail deliberately leaves sCurScene in r3 for this dynamic call.
    reinterpret_cast<VoidPtrFn>(callback)(scene);
    LMState::postLoadDetail(0xA5u, readWord(kLMMainDrawStateAddr), callback);
}

// The draw routines use ordinary EABI calls. Forwarding all eight volatile
// argument registers keeps each diagnostic wrapper transparent even where the
// exact retail prototype is unknown. The u32 result preserves r3 for the four
// calls whose return values are consumed.
#define DEFINE_DRAW_CALL(name, enter, leave, site, target)                  \
    extern "C" u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, \
                         u32 a6, u32 a7) {                                  \
        LMState::postLoadDetail(enter, site, target);                       \
        const u32 result = reinterpret_cast<RetailCall8Fn>(target)(         \
            a0, a1, a2, a3, a4, a5, a6, a7);                              \
        LMState::postLoadDetail(leave, site, target);                       \
        return result;                                                      \
    }

// Main Game draw dispatcher (0x8000BCEC).
DEFINE_DRAW_CALL(diagnosticMainDrawBD1C, 0xB0u, 0xB1u, 0x8000BD1Cu,
                 0x8000C700u)
DEFINE_DRAW_CALL(diagnosticMainDrawBD24, 0xB0u, 0xB1u, 0x8000BD24u,
                 0x8000C464u)
DEFINE_DRAW_CALL(diagnosticMainDrawBD2C, 0xB0u, 0xB1u, 0x8000BD2Cu,
                 0x8000C96Cu)
DEFINE_DRAW_CALL(diagnosticMainDrawBD34, 0xB0u, 0xB1u, 0x8000BD34u,
                 0x8000CBA8u)
DEFINE_DRAW_CALL(diagnosticMainDrawBD3C, 0xB0u, 0xB1u, 0x8000BD3Cu,
                 0x801853ACu)
DEFINE_DRAW_CALL(diagnosticMainDrawBD44, 0xB0u, 0xB1u, 0x8000BD44u,
                 0x80050C6Cu)
DEFINE_DRAW_CALL(diagnosticMainDrawBD58, 0xB0u, 0xB1u, 0x8000BD58u,
                 0x8000EEE8u)
DEFINE_DRAW_CALL(diagnosticMainDrawBD6C, 0xB0u, 0xB1u, 0x8000BD6Cu,
                 0x8000BBB4u)
DEFINE_DRAW_CALL(diagnosticMainDrawBDA4, 0xB0u, 0xB1u, 0x8000BDA4u,
                 0x8011325Cu)
DEFINE_DRAW_CALL(diagnosticMainDrawBDC4, 0xB0u, 0xB1u, 0x8000BDC4u,
                 0x80007D38u)
DEFINE_DRAW_CALL(diagnosticMainDrawBDC8, 0xB0u, 0xB1u, 0x8000BDC8u,
                 0x801132CCu)
DEFINE_DRAW_CALL(diagnosticMainDrawBDDC, 0xB0u, 0xB1u, 0x8000BDDCu,
                 0x8000B200u)
DEFINE_DRAW_CALL(diagnosticMainDrawBDE4, 0xB0u, 0xB1u, 0x8000BDE4u,
                 0x80007D38u)
DEFINE_DRAW_CALL(diagnosticMainDrawBDE8, 0xB0u, 0xB1u, 0x8000BDE8u,
                 0x80113838u)
DEFINE_DRAW_CALL(diagnosticMainDrawBDF0, 0xB0u, 0xB1u, 0x8000BDF0u,
                 0x80113474u)

// Normal-room renderer (0x8000BBB4). A final C0 record identifies the exact
// retail call which did not return; C1 proves that call completed.
DEFINE_DRAW_CALL(diagnosticNormalDrawBBC8, 0xC0u, 0xC1u, 0x8000BBC8u,
                 0x80009A58u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBBCC, 0xC0u, 0xC1u, 0x8000BBCCu,
                 0x80156B0Cu)
DEFINE_DRAW_CALL(diagnosticNormalDrawBBD4, 0xC0u, 0xC1u, 0x8000BBD4u,
                 0x80070FA8u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBBE4, 0xC0u, 0xC1u, 0x8000BBE4u,
                 0x8005F6B8u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBBF4, 0xC0u, 0xC1u, 0x8000BBF4u,
                 0x8005F6B8u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBBF8, 0xC0u, 0xC1u, 0x8000BBF8u,
                 0x8005CE78u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBBFC, 0xC0u, 0xC1u, 0x8000BBFCu,
                 0x8005E300u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC00, 0xC0u, 0xC1u, 0x8000BC00u,
                 0x8001138Cu)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC10, 0xC0u, 0xC1u, 0x8000BC10u,
                 0x80011468u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC14, 0xC0u, 0xC1u, 0x8000BC14u,
                 0x800114E4u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC18, 0xC0u, 0xC1u, 0x8000BC18u,
                 0x80060004u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC20, 0xC0u, 0xC1u, 0x8000BC20u,
                 0x800601ECu)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC2C, 0xC0u, 0xC1u, 0x8000BC2Cu,
                 0x800601ECu)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC30, 0xC0u, 0xC1u, 0x8000BC30u,
                 0x80060A0Cu)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC34, 0xC0u, 0xC1u, 0x8000BC34u,
                 0x80156BC4u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC3C, 0xC0u, 0xC1u, 0x8000BC3Cu,
                 0x80070FA8u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC44, 0xC0u, 0xC1u, 0x8000BC44u,
                 0x80070FA8u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC48, 0xC0u, 0xC1u, 0x8000BC48u,
                 0x80011410u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC64, 0xC0u, 0xC1u, 0x8000BC64u,
                 0x800112B8u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC6C, 0xC0u, 0xC1u, 0x8000BC6Cu,
                 0x80011468u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC74, 0xC0u, 0xC1u, 0x8000BC74u,
                 0x8005CF0Cu)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC7C, 0xC0u, 0xC1u, 0x8000BC7Cu,
                 0x8000BA64u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC88, 0xC0u, 0xC1u, 0x8000BC88u,
                 0x8005EC34u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC8C, 0xC0u, 0xC1u, 0x8000BC8Cu,
                 0x80009A58u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC90, 0xC0u, 0xC1u, 0x8000BC90u,
                 0x800078FCu)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC94, 0xC0u, 0xC1u, 0x8000BC94u,
                 0x8005D300u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBC9C, 0xC0u, 0xC1u, 0x8000BC9Cu,
                 0x8003E378u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBCA4, 0xC0u, 0xC1u, 0x8000BCA4u,
                 0x8000ACD4u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBCB4, 0xC0u, 0xC1u, 0x8000BCB4u,
                 0x800112B8u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBCB8, 0xC0u, 0xC1u, 0x8000BCB8u,
                 0x8005DD68u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBCBC, 0xC0u, 0xC1u, 0x8000BCBCu,
                 0x800461CCu)
DEFINE_DRAW_CALL(diagnosticNormalDrawBCC4, 0xC0u, 0xC1u, 0x8000BCC4u,
                 0x80070FA8u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBCC8, 0xC0u, 0xC1u, 0x8000BCC8u,
                 0x80043B58u)
DEFINE_DRAW_CALL(diagnosticNormalDrawBCD0, 0xC0u, 0xC1u, 0x8000BCD0u,
                 0x80070FA8u)

// Central per-view draw routine (0x8000BA64), reached from 0x8000BC7C.
DEFINE_DRAW_CALL(diagnosticPerViewDrawBA78, 0xD0u, 0xD1u, 0x8000BA78u,
                 0x8005D300u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBA7C, 0xD0u, 0xD1u, 0x8000BA7Cu,
                 0x80060004u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBA88, 0xD0u, 0xD1u, 0x8000BA88u,
                 0x801F378Cu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBA94, 0xD0u, 0xD1u, 0x8000BA94u,
                 0x800114E4u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAA4, 0xD0u, 0xD1u, 0x8000BAA4u,
                 0x80185274u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAA8, 0xD0u, 0xD1u, 0x8000BAA8u,
                 0x8005FE18u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAB0, 0xD0u, 0xD1u, 0x8000BAB0u,
                 0x800601ECu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAB4, 0xD0u, 0xD1u, 0x8000BAB4u,
                 0x80156BC4u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBABC, 0xD0u, 0xD1u, 0x8000BABCu,
                 0x80070FA8u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAC0, 0xD0u, 0xD1u, 0x8000BAC0u,
                 0x80037B64u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAC4, 0xD0u, 0xD1u, 0x8000BAC4u,
                 0x800B84A8u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAC8, 0xD0u, 0xD1u, 0x8000BAC8u,
                 0x80160DBCu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAD0, 0xD0u, 0xD1u, 0x8000BAD0u,
                 0x800114E4u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAD8, 0xD0u, 0xD1u, 0x8000BAD8u,
                 0x800601ECu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAE0, 0xD0u, 0xD1u, 0x8000BAE0u,
                 0x8005FFB0u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAE4, 0xD0u, 0xD1u, 0x8000BAE4u,
                 0x80009A58u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBAE8, 0xD0u, 0xD1u, 0x8000BAE8u,
                 0x8005D300u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB00, 0xD0u, 0xD1u, 0x8000BB00u,
                 0x8000852Cu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB04, 0xD0u, 0xD1u, 0x8000BB04u,
                 0x80160DE4u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB08, 0xD0u, 0xD1u, 0x8000BB08u,
                 0x80123220u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB0C, 0xD0u, 0xD1u, 0x8000BB0Cu,
                 0x8012EB0Cu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB10, 0xD0u, 0xD1u, 0x8000BB10u,
                 0x8012B1D0u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB20, 0xD0u, 0xD1u, 0x8000BB20u,
                 0x8015E7F8u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB24, 0xD0u, 0xD1u, 0x8000BB24u,
                 0x80060A0Cu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB2C, 0xD0u, 0xD1u, 0x8000BB2Cu,
                 0x800601ECu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB34, 0xD0u, 0xD1u, 0x8000BB34u,
                 0x80070FA8u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB38, 0xD0u, 0xD1u, 0x8000BB38u,
                 0x8000C19Cu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB3C, 0xD0u, 0xD1u, 0x8000BB3Cu,
                 0x8005D300u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB44, 0xD0u, 0xD1u, 0x8000BB44u,
                 0x801F3544u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB54, 0xD0u, 0xD1u, 0x8000BB54u,
                 0x801F3584u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB58, 0xD0u, 0xD1u, 0x8000BB58u,
                 0x8005E92Cu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB60, 0xD0u, 0xD1u, 0x8000BB60u,
                 0x801F3544u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB64, 0xD0u, 0xD1u, 0x8000BB64u,
                 0x8005E5FCu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB70, 0xD0u, 0xD1u, 0x8000BB70u,
                 0x8000852Cu)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB74, 0xD0u, 0xD1u, 0x8000BB74u,
                 0x800115E4u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB84, 0xD0u, 0xD1u, 0x8000BB84u,
                 0x80185298u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB8C, 0xD0u, 0xD1u, 0x8000BB8Cu,
                 0x80070FA8u)
DEFINE_DRAW_CALL(diagnosticPerViewDrawBB9C, 0xD0u, 0xD1u, 0x8000BB9Cu,
                 0x801EFBFCu)

#undef DEFINE_DRAW_CALL

extern "C" void diagnosticOrthoReset() {
    LMState::postLoadMilestone(0xA6u);
    reinterpret_cast<VoidFn>(kLMDefaultOrthoViewAddr)();
    LMState::postLoadMilestone(0xA7u);
}

extern "C" void diagnosticPreMainUpdate() {
    LMState::postLoadMilestone(0x8Cu);
    reinterpret_cast<VoidFn>(kLMPreMainUpdateAddr)();
    LMState::postLoadMilestone(0x8Du);
}

extern "C" void diagnosticPostMainUpdate(void *state) {
    LMState::postLoadMilestone(0x8Eu);
    reinterpret_cast<VoidPtrFn>(kLMPostMainUpdateAddr)(state);
    LMState::postLoadMilestone(0x8Fu);
}

// These execute immediately after the post-presenter transaction wrapper and
// close the remaining blind spot before the next frame-begin milestone.
extern "C" void diagnosticConditionalTail(void *state) {
    LMState::postLoadMilestone(0x90u);
    reinterpret_cast<VoidPtrFn>(kLMConditionalTailAddr)(state);
    LMState::postLoadMilestone(0x91u);
}

extern "C" void diagnosticLoopTailSync() {
    LMState::postLoadMilestone(0x92u);
    reinterpret_cast<VoidFn>(kLMLoopTailSyncAddr)();
    LMState::postLoadMilestone(0x93u);
}

extern "C" void diagnosticLoopTailClock() {
    LMState::postLoadMilestone(0x94u);
    reinterpret_cast<VoidFn>(kLMLoopTailClockAddr)();
    LMState::postLoadMilestone(0x95u);
}

// If restored state makes LM leave its inner game loop, these markers follow
// the return through the outer scene-transition path. The vtable call between
// 0x97 and 0x98 remains deliberately unwrapped, so a final 0x97 isolates it.
extern "C" void diagnosticGameLoop() {
    LMState::postLoadMilestone(0x96u);
    reinterpret_cast<VoidFn>(kLMGameLoopAddr)();
    LMState::postLoadMilestone(0x97u);
}

extern "C" void diagnosticOuterCleanup() {
    LMState::postLoadMilestone(0x98u);
    reinterpret_cast<VoidFn>(kLMOuterCleanupAddr)();
    LMState::postLoadMilestone(0x99u);
}

extern "C" void diagnosticOuterRestart(u32 heapCount) {
    LMState::postLoadMilestone(0x9Au);
    reinterpret_cast<VoidU32Fn>(kLMOuterRestartAddr)(heapCount);
    LMState::postLoadMilestone(0x9Bu);
}

// Wraps both GLMJ01 GXCopyDisp call sites.  The original copy is allowed to
// complete before touching the XFB, so this path cannot depend on the EFB's
// projection, vertex descriptors, resource fonts, or scene draw order.
extern "C" void diagnosticCopyDisp(void *xfb, bool clear) {
    HeapSample system;
    HeapSample game;
    sampleDiagnostic(&system, &game);
    LMState::presenterAfterSample();

    reinterpret_cast<GXCopyDispFn>(kGXCopyDispAddr)(xfb, clear);
    reinterpret_cast<VoidFn>(kGXDrawDoneAddr)();
    LMState::presenterAfterDrawDone();

    // Crash registration is lazy because LM's JUTException constructor clears
    // the callback during early boot. Paint before tick so the diagnostic
    // remains visible if a requested transaction never returns.
    LMCrash::init();

    const u32 rawAddress = reinterpret_cast<u32>(xfb);
    const u32 segment = rawAddress & 0xC0000000u;
    const u32 physical = rawAddress & 0x3FFFFFFFu;
    const bool validXfb =
        (segment == 0x80000000u || segment == 0xC0000000u) &&
        (physical & 31u) == 0u && physical >= 0x00003100u &&
        physical <= 0x01800000u - kXfbSize;
    if (validXfb) {
        void *const cachedXfb =
            reinterpret_cast<void *>(kMem1Start | physical);
        reinterpret_cast<CacheRangeFn>(kDCInvalidateRangeAddr)(cachedXfb,
                                                                kXfbSize);

        const u32 directPrintAddress = readWord(kDirectPrintPtrAddr);
        const bool directPrintReady =
            isMem1Range(directPrintAddress, 0x18u);
        if (directPrintReady) {
            drawPanel(reinterpret_cast<void *>(directPrintAddress), cachedXfb,
                      system, game);
        }
        drawRawHeartbeat(cachedXfb, directPrintReady);
    }

    // The transaction runs from diagnosticChangeFrameBuffer only after LM's
    // entire VI/retrace presenter tail has completed. Status appears here on
    // the following frame; a stalled operation leaves the prior frame visible.
}

#endif  // defined(SUSAMUNE_VERSION_LMJ)
