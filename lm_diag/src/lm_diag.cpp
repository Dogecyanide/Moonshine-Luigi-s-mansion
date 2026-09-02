#if defined(SUSAMUNE_VERSION_LMJ)

#include "Dolphin/types.h"
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

const u32 kGXCopyDispAddr = 0x801F045Cu;
const u32 kGXDrawDoneAddr = 0x801EF5F0u;
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

typedef void (*VoidFn)();
typedef void (*GXCopyDispFn)(void *, bool);
typedef void (*CacheRangeFn)(void *, u32);
typedef void (*DirectPrintEraseFn)(void *, u16, u16, u16, u16);
typedef void (*DirectPrintChangeFrameBufferFn)(void *, void *, u16, u16);
typedef void (*DirectPrintDrawStringFn)(void *, u16, u16, const char *, ...);
typedef u32 (*ExpHeapSizeFn)(void *);
typedef bool (*ExpHeapCheckFn)(void *);

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
    const u32 rootSize = rootReadable ? readWord(root + 0x38u) : 0;
    const u32 current = readWord(kJKRCurrentHeapAddr);
    const u32 group = isExpHeapPointer(current) ? readByte(current + 0x69u) : 0;

    // JUTDirectPrint writes its built-in 6x7 font straight into the copied
    // YUYV framebuffer.  It has no resource-font or heap dependency.  At a
    // 640-pixel XFB it treats these as 320x240 logical coordinates.
    reinterpret_cast<DirectPrintChangeFrameBufferFn>(
        kDirectPrintChangeFrameBufferAddr)(directPrint, xfb, kXfbWidth,
                                            kXfbHeight);
    reinterpret_cast<DirectPrintEraseFn>(kDirectPrintEraseAddr)(
        directPrint, 0, 0, 320, 58);
    reinterpret_cast<DirectPrintDrawStringFn>(kDirectPrintDrawStringAddr)(
        directPrint, 2, 2,
        "LM MEM DIAG 0.2 INJECTED  F:%s C:%s H:%s\n"
        "ROOT %08lX %08lX-%08lX %luK\n"
        "SYS  %08lX L/T/M %lu/%lu/%luK\n"
        "GAME %08lX L/T/M %lu/%lu/%luK\n"
        "CUR %08lX G%lu A %08lX>%08lX H%08lX",
        status(sFloorObserved, sFloorOk), status(sCanaryReady, sCanaryOk),
        status(sHeapCheckReady, sHeapCheckOk), root, rootStart, rootEnd,
        displayKiB(rootSize), system.pointer,
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
            words[y * stride + xPair + x] =
                (checker == directPrintReady) ? white : black;
        }
    }
    reinterpret_cast<CacheRangeFn>(kDCFlushRangeAddr)(xfb,
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

// Wraps both GLMJ01 GXCopyDisp call sites.  The original copy is allowed to
// complete before touching the XFB, so this path cannot depend on the EFB's
// projection, vertex descriptors, resource fonts, or scene draw order.
extern "C" void diagnosticCopyDisp(void *xfb, bool clear) {
    HeapSample system;
    HeapSample game;
    sampleDiagnostic(&system, &game);

    reinterpret_cast<GXCopyDispFn>(kGXCopyDispAddr)(xfb, clear);
    reinterpret_cast<VoidFn>(kGXDrawDoneAddr)();

    const u32 rawAddress = reinterpret_cast<u32>(xfb);
    const u32 segment = rawAddress & 0xC0000000u;
    const u32 physical = rawAddress & 0x3FFFFFFFu;
    if ((segment != 0x80000000u && segment != 0xC0000000u) ||
        (physical & 31u) != 0 || physical < 0x00003100u ||
        physical > 0x01800000u - kXfbSize) {
        return;
    }
    void *const cachedXfb = reinterpret_cast<void *>(kMem1Start | physical);
    reinterpret_cast<CacheRangeFn>(kDCInvalidateRangeAddr)(cachedXfb,
                                                            kXfbSize);

    const u32 directPrintAddress = readWord(kDirectPrintPtrAddr);
    const bool directPrintReady = isMem1Range(directPrintAddress, 0x18u);
    if (directPrintReady) {
        drawPanel(reinterpret_cast<void *>(directPrintAddress), cachedXfb,
                  system, game);
    }
    drawRawHeartbeat(cachedXfb, directPrintReady);
}

#endif  // defined(SUSAMUNE_VERSION_LMJ)
