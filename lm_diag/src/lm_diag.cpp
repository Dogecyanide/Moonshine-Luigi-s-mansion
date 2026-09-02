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
const u32 kPersistentPrintAddr = 0x804A0FE8u;

const u32 kLMChangeFrameBufferAddr = 0x800077E8u;
const u32 kLMDefaultOrthoViewAddr = 0x800078FCu;
const u32 kGlobalFontAddr = 0x803C3394u;
const u32 kJ2DPrintCtorAddr = 0x801A97ECu;
const u32 kJ2DPrintDtorAddr = 0x801A9940u;
const u32 kJ2DPrintPrintAddr = 0x801A9C04u;
const u32 kFontSetGXAddr = 0x801D11ACu;
const u32 kExpHeapLargestFreeAddr = 0x801CA4D0u;
const u32 kExpHeapTotalFreeAddr = 0x801CA53Cu;
const u32 kExpHeapCheckAddr = 0x801CA61Cu;
const u32 kExpHeapVtable = 0x8038886Cu;

const u32 kModEnd = SUSAMUNE_MOD_BASE_LMJ + SUSAMUNE_MOD_REGION_SIZE;
const u32 kCanaryAddr = kModEnd - 0x10u;
const u32 kMem1Start = 0x80000000u;
const u32 kMem1End = 0x81800000u;
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
typedef void (*J2DPrintCtorFn)(void *, void *, s32);
typedef void (*J2DPrintDtorFn)(void *, s16);
typedef void (*J2DPrintPrintFn)(void *, s32, s32, const char *, ...);
typedef void (*FontSetGXFn)(void *);
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

void drawPanel(void *print, s32 x, s32 y, u32 color,
               const HeapSample &system, const HeapSample &game) {
    // These offsets are from GLMJ01's retail J2DPrint::private_initiate at
    // 0x801A998C, not the inherited Sunshine header's approximate layout.
    *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(print) + 0x3Cu) = color;
    *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(print) + 0x40u) = color;

    const u32 root = readWord(kLMRootHeapAddr);
    const bool rootReadable = isExpHeapPointer(root);
    const u32 rootStart = rootReadable ? readWord(root + 0x30u) : 0;
    const u32 rootEnd = rootReadable ? readWord(root + 0x34u) : 0;
    const u32 rootSize = rootReadable ? readWord(root + 0x38u) : 0;
    const u32 current = readWord(kJKRCurrentHeapAddr);
    const u32 group = isExpHeapPointer(current) ? readByte(current + 0x69u) : 0;

    reinterpret_cast<J2DPrintPrintFn>(kJ2DPrintPrintAddr)(
        print, x, y,
        "LM MEM F:%s C:%s H:%s\n"
        "R %08lX %08lX-%08lX %luK\n"
        "S %08lX L/T/m %lu/%lu/%luK\n"
        "G %08lX L/T/m %lu/%lu/%luK\n"
        "C %08lX g%lu A %08lX>%08lX-%08lX",
        status(sFloorObserved, sFloorOk), status(sCanaryReady, sCanaryOk),
        status(sHeapCheckReady, sHeapCheckOk), root, rootStart, rootEnd,
        rootSize >> 10, system.pointer, system.largestFree >> 10,
        system.totalFree >> 10, sSystemMinimum >> 10, game.pointer,
        game.largestFree >> 10, game.totalFree >> 10, sGameMinimum >> 10,
        current, group, sInitialArenaLo, sRaisedArenaLo, sInitialArenaHi);
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

// Replaces the sole per-frame call to LMChangeFrameBuffer.  GLMJ01 executes
// this after scene drawing and immediately before GXCopyDisp, so drawing here
// reaches the EFB normally without a GPU stall or CPU access to either XFB.
extern "C" void diagnosticFrame() {
    sampleFloorAndCanary();
    const HeapSample system = sampleHeap(kLMSystemHeapAddr);
    const HeapSample game = sampleHeap(kLMGameHeapAddr);
    if (system.valid) {
        updateMinimum(system.totalFree, &sSystemMinimum, &sSystemSampled);
    }
    if (game.valid) {
        updateMinimum(game.totalFree, &sGameMinimum, &sGameSampled);
    }
    sampleHeapChecks(system, game);

    const u32 persistentPrint = readWord(kPersistentPrintAddr);
    const bool fontReady =
        isMem1Range(persistentPrint, 0x5Cu) &&
        readWord(persistentPrint + 4u) == kGlobalFontAddr &&
        isMem1Pointer(readWord(kGlobalFontAddr + 0x4Cu));
    if (fontReady) {
        reinterpret_cast<VoidFn>(kLMDefaultOrthoViewAddr)();

        // The actual GLMJ01 object ends at +0x5C.  Its two-argument
        // constructor uses the game's already-created 1 KiB printf buffer, so
        // constructing this private renderer on the stack performs no heap
        // allocation and cannot disturb the game's persistent J2DPrint state.
        alignas(4) u8 printStorage[0x5Cu];
        void *const print = printStorage;
        reinterpret_cast<J2DPrintCtorFn>(kJ2DPrintCtorAddr)(
            print, reinterpret_cast<void *>(kGlobalFontAddr), 0);
        *reinterpret_cast<s32 *>(printStorage + 0x48u) = 13;  // leading
        *reinterpret_cast<s32 *>(printStorage + 0x50u) = 12;  // glyph width
        *reinterpret_cast<s32 *>(printStorage + 0x54u) = 12;  // glyph height
        reinterpret_cast<FontSetGXFn>(kFontSetGXAddr)(
            reinterpret_cast<void *>(kGlobalFontAddr));

        // A one-pixel black pass keeps the compact white text legible over
        // bright rooms without requiring a J2D graphics-port object.
        drawPanel(print, 9, 15, 0x000000FFu, system, game);
        drawPanel(print, 8, 14, 0xFFFFFFFFu, system, game);
        reinterpret_cast<J2DPrintDtorFn>(kJ2DPrintDtorAddr)(print, -1);
    }

    reinterpret_cast<VoidFn>(kLMChangeFrameBufferAddr)();
}

#endif  // defined(SUSAMUNE_VERSION_LMJ)
