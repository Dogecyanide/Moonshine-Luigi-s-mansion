// =====================================================================
// savestate.cpp
//
// Emulator-style savestates for Super Mario Sunshine running under
// Nintendont. Hooked once per frame from main.cpp::onUpdate; D-pad LEFT
// snapshots, D-pad RIGHT restores.
//
// What gets snapshotted
// ---------------------
// 1. The current "stage" heap: gpApplication.mCurrentHeap is a
//    JKRSolidHeap (created in TApplication::initialize_nlogoAfter) that
//    fills the rest of the root heap. Almost every per-stage allocation
//    lives here -- TMario, every enemy, MapObj, particles, the scene
//    graph, the camera, etc. Each director cycle the game does a
//    freeAll() on this heap, so its high-water layout is stable for the
//    duration of one scenario.
//
// 2. The "game half" of .bss / .sdata / .sbss. Pointers from heap-resident
//    objects into BSS (and back) need to survive a load, so we restore the
//    parts of BSS that hold mutable game state -- the TFlagManager
//    singleton pointer, gpMarDirector, gpMSound, the libc rand() state,
//    every game module's static counters/caches. The boundaries below were
//    derived from the selected maps/<version>.map: the first game-side modules are
//    MarioUtil.a/DrawUtil.cpp in .bss/.sbss and MoveBG.a/MapObjGeneral.cpp
//    in .sdata. Everything below those addresses is JSystem / JAudio /
//    runtime / OS / DVD / VI / PAD / CARD / GX / SI / EXI / THP / debugger
//    state which we DO NOT touch -- restoring OS thread queues, DVD command
//    queues, audio DSP mailboxes, etc. would crash the console.
//
// What is intentionally not snapshotted
// -------------------------------------
// .data    : almost entirely vtables and static const tables. Read-only at
//            runtime, so no need to copy it back.
// stack    : we are running on it.
// system   : everything before the selected game-side range in each section.
// audio    : MSound has internal queues that reference DSP-side state. We
//            stopAllSound() before save AND before restore so the audio
//            engine never sees inconsistent state.
//
// Invariants for a successful load
// --------------------------------
// - Same build (vtable addresses, BSS layout)
// - Same scenario as the snapshot (same area + episode)
// - Heap object ended up at the same address (deterministic boot)
// - No async DVD / archive load in flight (best-effort: gameplay is
//   normally quiescent)
//
// Where the snapshot lives
// ------------------------
// On Wii via Nintendont, MEM2 cached lives at 0x90000000 / uncached at
// 0xD0000000. Nintendont reserves chunks of MEM2 for itself; the safe
// window depends on the Nintendont build. The address below is a guess
// and almost certainly needs to be tuned per setup. On Dolphin we just
// pick a spot in the emulator's larger virtual space.
// =====================================================================

#include "susamune/savestate.hxx"
#include "susamune/addresses.hxx"

#include "Dolphin/CARD.h"
#include "Dolphin/OS.h"
#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DTextBox.hxx"
#include "JKernel/JKRHeap.hxx"
#include "JUtility/JUTGamePad.hxx"
#include "SMS/GC2D/SmplFader.hxx"
#include "SMS/MSound/MSound.hxx"
#include "SMS/Manager/FlagManager.hxx"
#include "SMS/Manager/RumbleManager.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/CardManager.hxx"
#include "SMS/System/MarDirector.hxx"


// ---------------------------------------------------------------------
// Build configuration
// ---------------------------------------------------------------------

#if IS_EMULATOR
// Dolphin: a region in the emulator's "free" space.
static const u32 kSnapshotBase = 0x70000000u;
#else
// Wii: on Nintendont, when memcard emu is disabled, this area is right
// after the 3MB DI cache. It is free until 0x92F00000 which is where
// the Nintendont kernel itself is loaded.
static const u32 kSnapshotBase = 0x91F00000;
#endif

// How much MEM2 we promise not to step outside of. The actual snapshot is
// (game-bss + game-sdata + game-sbss + heap), which should be under 16MiB.
static const u32 kSnapshotReservedSize = 0x01000000u; // 16 MiB


// ---------------------------------------------------------------------
// Static memory regions to snapshot (game-side BSS / sdata / sbss).
// Boundaries come from maps/<version>.map. The first game modules are
// MarioUtil.a/DrawUtil.cpp in BSS/SBSS and MoveBG.a/MapObjGeneral.cpp in
// SDATA; every range stops before the JSystem/runtime portion of that linker
// section. Section ends are unsafe: they include live renderer, DSP, heap,
// and OS state that must not be restored.
//
// gpApplication itself sits at the very top of .bss (main.o) and holds
// pointers and scene-id fields TApplication touches every frame, so we
// pull it in as a one-off range.
// ---------------------------------------------------------------------

namespace {

struct StaticRange {
    u32 start;
    u32 end;
    const char *name;
};

const StaticRange kStaticRanges[] = {
    { SUSAMUNE_ADDR_APPLICATION, SUSAMUNE_ADDR_APPLICATION + sizeof(TApplication), "gpApp" },
    { SUSAMUNE_ADDR_GAME_BSS_START, SUSAMUNE_ADDR_GAME_BSS_END, "bss-game" },
    { SUSAMUNE_ADDR_GAME_SDATA_START, SUSAMUNE_ADDR_GAME_SDATA_END, "sdata-game" },
    { SUSAMUNE_ADDR_GAME_SBSS_START, SUSAMUNE_ADDR_GAME_SBSS_END, "sbss-game" },
    // MSL rand.c `next` -- the seed for libc rand()/srand(), which is what
    // every gameplay RNG funnels through (MarioUtil MsRandF/MsRandI, so King
    // Boo's fruit pulls, Gooper Blooper / manta patterns, enemy timers, etc).
    // It sits below the selected game-sdata boundary, so it was excluded
    // before -- which is why the RNG stream
    // kept advancing across a load instead of rewinding. It's a plain 4-byte
    // counter with no hardware/kernel linkage, so restoring it is safe.
    { SUSAMUNE_ADDR_LIBC_RAND_SEED, SUSAMUNE_ADDR_LIBC_RAND_SEED + sizeof(u32), "rand-seed" },
};
const int kNumStaticRanges = sizeof(kStaticRanges) / sizeof(kStaticRanges[0]);

// Pointed-to allocations: globals in BSS that hold a pointer to a
// root-heap-allocated object the game mutates every frame. The static
// ranges above already preserve the *pointer* (it lives in BSS), but the
// pointed-to *bytes* are on the root heap, which is outside our heap
// snapshot. So we follow each pointer at save time and capture its
// target as an additional region.
struct PointedAlloc {
    u32 ptr_addr;     // address of the global pointer (in BSS)
    u32 size;         // bytes to capture from *(*ptr_addr)
    const char *name;
};

const PointedAlloc kPointedAllocs[] = {
    // TFlagManager::smInstance -- coin counts, shines, episode flags,
    // life count, etc. Without this, coin pickups are forgotten on load.
    { SUSAMUNE_ADDR_FLAG_MANAGER_INSTANCE, sizeof(TFlagManager),  "TFlagManager" },
    // TTimeRec::_instance -- the input/profiler recorder. Object size is
    // 0x820 bytes; the constructor argument 0xDFC0 is unrelated to it.
    { SUSAMUNE_ADDR_TIME_REC_INSTANCE,     0x820u,                "TTimeRec"     },
    // SMSRumbleMgr -- rumble channels' active state.
    { SUSAMUNE_ADDR_RUMBLE_MANAGER,        sizeof(RumbleMgr),     "RumbleMgr"    },
    // gpApplication.mGamePads[0..3] -- the four TMarioGamePad objects on
    // the root heap. The pointers themselves live inside the gpApplication
    // static range, but the per-pad button-meaning state machine
    // (mMeaning, mFrameMeaning, mState.mDisable, mState.mIsTalking, ...)
    // mutates every frame. Without restoring it, reloading while a dialog
    // had disabled the pad leaves the meaning bits stuck.
    // Address = &gpApplication + offsetof(TApplication, mGamePads[i]).
    { SUSAMUNE_ADDR_APPLICATION_GAMEPAD(0), sizeof(TMarioGamePad), "Pad0" },
    { SUSAMUNE_ADDR_APPLICATION_GAMEPAD(1), sizeof(TMarioGamePad), "Pad1" },
    { SUSAMUNE_ADDR_APPLICATION_GAMEPAD(2), sizeof(TMarioGamePad), "Pad2" },
    { SUSAMUNE_ADDR_APPLICATION_GAMEPAD(3), sizeof(TMarioGamePad), "Pad3" },
    // gpApplication.mFader -- the screen fader's animation state lives on
    // the root heap. Without this the fader gets stuck mid-fade when you
    // load from inside a transition (shine-get fadeout, save card screen).
    // Address = &gpApplication + offsetof(TApplication, mFader).
    { SUSAMUNE_ADDR_APPLICATION_FADER, sizeof(TSmplFader), "Fader" },
};
const int kNumPointedAllocs = sizeof(kPointedAllocs) / sizeof(kPointedAllocs[0]);

// One header lives at the very start of the snapshot buffer; the saved
// bytes follow at kHeaderSize.
const u32 kSnapshotMagic   = 0x53555341u; // 'SUSA'
const u32 kSnapshotVersion = 7u;          // bumped: exclude system-owned static storage
const u32 kHeaderSize      = 0x100u;
// One slot per static range, one per pointed alloc, plus one for the heap.
const int kMaxRegions      = kNumStaticRanges + kNumPointedAllocs + 1;

struct RegionEntry {
    u32 addr;       // virtual address restored to
    u32 size;       // bytes
    u32 buf_offset; // offset from snapshot buffer base (after header)
};

struct SavestateHeader {
    u32 magic;
    u32 version;
    u32 game_version;
    u32 heap_addr;        // value of gpApplication.mCurrentHeap at save time
    u32 heap_size;        // bytes between heap and heap->mEnd
    u8  area_id;
    u8  episode_id;
    u16 _pad0;
    u32 region_count;
    // OSGetTime() at save. TMarDirector::mStopwatch (the Piantissimo-chase /
    // blooper-race mission countdown) stores an absolute console-uptime
    // timestamp in mLast; a byte-for-byte restore of that field is not
    // enough; see the mission-timer correction in loadState().
    OSTime save_time;
    RegionEntry regions[kMaxRegions];
};
static_assert(sizeof(SavestateHeader) <= kHeaderSize,
              "savestate header no longer fits in its reserved space");

inline SavestateHeader *headerPtr() {
    return reinterpret_cast<SavestateHeader *>(kSnapshotBase);
}
inline u8 *bufferPtr() {
    return reinterpret_cast<u8 *>(kSnapshotBase + kHeaderSize);
}

// Manual word-aligned copy. Avoid pulling in libc memcpy and mirror what
// the existing code did.
void wordCopy(void *dst, const void *src, size_t bytes) {
    u32 *d        = static_cast<u32 *>(dst);
    const u32 *s  = static_cast<const u32 *>(src);
    size_t words  = bytes >> 2;
    for (size_t i = 0; i < words; i++) {
        d[i] = s[i];
    }
    u8 *db        = reinterpret_cast<u8 *>(d + words);
    const u8 *sb  = reinterpret_cast<const u8 *>(s + words);
    size_t tail   = bytes & 3u;
    for (size_t i = 0; i < tail; i++) {
        db[i] = sb[i];
    }
}

// ---------------------------------------------------------------------
// Hardware audio mute
// ---------------------------------------------------------------------
// The snapshot copy runs with interrupts disabled (see save/loadState). For
// a multi-megabyte heap that is several ms during which the DSP-driven audio
// DMA never gets its refill interrupt, so the DAC just replays its last block
// -> an unpleasant hiccup/buzz. stopAllSound() only quiets the software mixer;
// it does not stop the DMA that is already feeding the DAC.
//
// The master audio-out DMA enable is bit 0x8000 of DSPRegs[27] (0xCC005000 +
// 27*2). This is exactly the bit AIStartDMA sets / AIStopDMA clears -- SMS's
// own audio interrupt handler toggles it every block. Clearing it silences
// the DAC immediately at the hardware level, which is the only mute that
// survives our interrupts-disabled window; setting it back resumes output.
// (AIStopDMA itself is stripped from the game binary, so we poke the register
// directly. The 0xCC00_xxxx MMIO block is uncached, so no cache handling.)
volatile u16 *const kDspRegs      = reinterpret_cast<volatile u16 *>(0xCC005000u);
const u16           kAiDmaEnable  = 0x8000u;

inline bool muteAudioDma() {
    bool wasOn = (kDspRegs[27] & kAiDmaEnable) != 0;
    kDspRegs[27] = kDspRegs[27] & ~kAiDmaEnable;
    return wasOn;
}
inline void unmuteAudioDma(bool wasOn) {
    if (wasOn) {
        kDspRegs[27] = kDspRegs[27] | kAiDmaEnable;
    }
}

// ---------------------------------------------------------------------
// Load-in-flight / transition gate
// ---------------------------------------------------------------------
// Snapshotting during a stage load or the opening sequence captures (or
// scribbles over) a heap that the async setup thread is still populating ->
// crash. TMarDirector::direct() returns early every frame while _260 == 0,
// which is precisely the window where the setup thread (gSetupThread) has not
// been joined yet -- i.e. the all-black "loading" screen. It flips to 1 only
// after the load completes. States 0..3 (INTRO_INIT / INTRO_PLAYING /
// GAME_STARTING / opening wipe) are the intro cutscene + fade-in that follow,
// where the camera demo and further archive/particle loads are still settling
// and snapshotting was also seen to crash. Only allow it once the director has
// reached STATE_NORMAL (4) or a later, settled state (pause / shine-select /
// blue save screen -- the transitions we deliberately support restoring from).
bool inLoadTransition() {
    if (!gpMarDirector) {
        return true;
    }
    if (gpMarDirector->_260 == 0) {
        return true; // setup thread still loading -- all-black screen
    }
    if (gpMarDirector->mCurState < TMarDirector::STATE_NORMAL) {
        return true; // intro cutscene / stage fade-in still playing
    }
    return false;
}

} // namespace


// ---------------------------------------------------------------------
// SavestateManager
// ---------------------------------------------------------------------

SavestateManager::SavestateManager() {
    mInfoText                    = new J2DTextBox(gpSystemFont->mFont, "ready");
    mInfoText->mCharSizeX        = 18;
    mInfoText->mCharSizeY        = 18;
    mInfoText->mGradientBottom   = { 255, 200, 0, 255 };
    mInfoText->mGradientTop      = { 255, 200, 0, 255 };

    // Mark the snapshot buffer empty so a stale MEM2 load doesn't
    // accidentally pass the magic check after a cold boot.
    headerPtr()->magic = 0;
}

void SavestateManager::setStatus(const char *msg) {
    if (mInfoText) {
        mInfoText->setString(msg);
    }
}

bool SavestateManager::saveState() {
    // Refuse while a stage load is in flight or the intro sequence is playing;
    // the heap is not yet stable there. See inLoadTransition().
    if (inLoadTransition()) {
        setStatus("E:loading");
        return false;
    }

    JKRHeap *heap = gpApplication.mCurrentHeap;
    if (!heap) {
        setStatus("E:noheap");
        return false;
    }

    const u32 heapStart = reinterpret_cast<u32>(heap);
    const u32 heapEnd   = reinterpret_cast<u32>(heap->mEnd);
    if (heapEnd <= heapStart) {
        setStatus("E:badheap");
        return false;
    }
    const u32 heapSize  = heapEnd - heapStart;

    // Bounds check before we write a single byte.
    u32 total = 0;
    for (int i = 0; i < kNumStaticRanges; i++) {
        total += kStaticRanges[i].end - kStaticRanges[i].start;
    }
    for (int i = 0; i < kNumPointedAllocs; i++) {
        total += kPointedAllocs[i].size;
    }
    total += heapSize;
    if (total + kHeaderSize > kSnapshotReservedSize) {
        setStatus("E:size");
        return false;
    }

    // Audio engine has DSP-side state we can't snapshot; quiet it before
    // we touch anything so the current-sounds list won't reference freed
    // tracks after a future restore.
    if (gpMSound) {
        gpMSound->stopAllSound();
    }

    // NOTE (disabled): draining the GP here (GXDrawDone) would guarantee the
    // frame's async GXCopyTex writes -- e.g. the pollution "goop" texture
    // copied back to mPollutionMap -- have landed in RAM before we read it,
    // at the cost of a one-frame stall. Not needed in practice: on console
    // the buffer is at worst one frame stale, and on Dolphin correctness
    // depends on Texture Cache Accuracy = Safe, not on this. Re-enable if a
    // console test shows a torn/stale goop snapshot. (#include "Dolphin/GX.h")
    // GXDrawDone();

    // We are called from inside onUpdate, which runs on the main thread
    // between director->direct() and rendering. That is already the most
    // quiescent point in the frame, but disable interrupts anyway so we
    // don't race a VI retrace callback that touches heap objects.
    bool ints = OSDisableInterrupts();
    // Silence the DAC for the whole interrupts-off window so the frozen audio
    // DMA doesn't buzz; restored just before interrupts come back.
    bool dma = muteAudioDma();

    SavestateHeader *h = headerPtr();
    h->magic        = 0; // committed at end as a torn-write guard
    h->version      = kSnapshotVersion;
    h->game_version = SUSAMUNE_GAME_VERSION;
    h->heap_addr    = heapStart;
    h->heap_size    = heapSize;
    h->area_id      = gpApplication.mCurrentScene.mAreaID;
    h->episode_id   = gpApplication.mCurrentScene.mEpisodeID;
    h->_pad0        = 0;
    h->region_count = 0;
    h->save_time    = OSGetTime();

    u32 offset = 0;
    for (int i = 0; i < kNumStaticRanges; i++) {
        u32 sz = kStaticRanges[i].end - kStaticRanges[i].start;
        wordCopy(bufferPtr() + offset,
                 reinterpret_cast<void *>(kStaticRanges[i].start), sz);
        h->regions[h->region_count].addr       = kStaticRanges[i].start;
        h->regions[h->region_count].size       = sz;
        h->regions[h->region_count].buf_offset = offset;
        h->region_count++;
        offset += sz;
    }

    // Follow each tracked pointer and capture its target. These objects
    // live on the root heap, which the JKRSolidHeap snapshot does not
    // cover -- without this, e.g. coin counts (TFlagManager) and shine
    // flags survive *only* because their pointer in BSS is restored, but
    // the bytes it points at are whatever is live at load time.
    for (int i = 0; i < kNumPointedAllocs; i++) {
        const PointedAlloc &pa = kPointedAllocs[i];
        u32 target = *reinterpret_cast<u32 *>(pa.ptr_addr);
        if (target == 0) {
            continue; // not yet initialised
        }
        // If the target happens to live inside the JKRSolidHeap range,
        // skip it -- it'll already be covered by the heap snapshot.
        if (target >= heapStart && target + pa.size <= heapEnd) {
            continue;
        }
        wordCopy(bufferPtr() + offset, reinterpret_cast<void *>(target),
                 pa.size);
        h->regions[h->region_count].addr       = target;
        h->regions[h->region_count].size       = pa.size;
        h->regions[h->region_count].buf_offset = offset;
        h->region_count++;
        offset += pa.size;
    }

    // Heap last (largest payload).
    wordCopy(bufferPtr() + offset, reinterpret_cast<void *>(heapStart),
             heapSize);
    h->regions[h->region_count].addr       = heapStart;
    h->regions[h->region_count].size       = heapSize;
    h->regions[h->region_count].buf_offset = offset;
    h->region_count++;
    offset += heapSize;

    // Push the bytes out of dcache so a subsequent uncached read (e.g. by
    // a future load that swaps the BAT or by debug tooling) sees them.
    DCStoreRange(headerPtr(), kHeaderSize);
    DCStoreRange(bufferPtr(), offset);

    // Commit magic last. After this point the snapshot is loadable.
    h->magic = kSnapshotMagic;
    DCStoreRange(&h->magic, sizeof(h->magic));

    unmuteAudioDma(dma);
    OSRestoreInterrupts(ints);

    setStatus("saved");
    return true;
}

bool SavestateManager::loadState() {
    // Refuse while a stage load is in flight or the intro sequence is playing;
    // overwriting a heap the setup thread is still filling crashes. See
    // inLoadTransition().
    if (inLoadTransition()) {
        setStatus("E:loading");
        return false;
    }

    SavestateHeader *h = headerPtr();
    if (h->magic != kSnapshotMagic) {
        setStatus("E:nosnap");
        return false;
    }
    if (h->version != kSnapshotVersion) {
        setStatus("E:version");
        return false;
    }
    if (h->game_version != SUSAMUNE_GAME_VERSION) {
        setStatus("E:region");
        return false;
    }

    JKRHeap *heap = gpApplication.mCurrentHeap;
    if (!heap) {
        setStatus("E:noheap");
        return false;
    }

    // Pointers in the snapshotted heap are absolute. If the heap moved
    // (different scenario, different boot path), restoring would scribble
    // stale pointers all over the place. Refuse the load.
    if (reinterpret_cast<u32>(heap) != h->heap_addr) {
        setStatus("E:hpaddr");
        return false;
    }
    const u32 heapSize = reinterpret_cast<u32>(heap->mEnd)
                       - reinterpret_cast<u32>(heap);
    if (heapSize != h->heap_size) {
        setStatus("E:hpsize");
        return false;
    }

    // Same-scenario only. Restoring across a moveStage() is more
    // complicated -- the heap freeAll()s and gets re-populated by the
    // new director's setup -- and not the use case we're after.
    if (h->area_id    != gpApplication.mCurrentScene.mAreaID
     || h->episode_id != gpApplication.mCurrentScene.mEpisodeID) {
        setStatus("E:scene");
        return false;
    }

    // gpCardManager has its own worker thread, mutex, and cond var on the
    // root heap. Snapshotting/restoring it would trash kernel-side thread
    // bookkeeping, so we don't -- but we also can't safely tear down the
    // rest of the world while the card thread is mid-transaction (this is
    // why loading from the blue save screen used to crash). Spin here with
    // interrupts enabled until the card thread reports idle. CARD_ERROR_BUSY
    // is -1; any other value (including ready / error codes) means the
    // worker is parked waiting for its next command.
    if (gpCardManager) {
        int spins = 0;
        while (gpCardManager->getLastStatus() == CARD_ERROR_BUSY) {
            if (++spins > 600) { // ~10 s @ 60 Hz of yielding
                setStatus("E:cardbsy");
                return false;
            }
            OSYieldThread();
        }
    }

    // Same reasoning as save().
    if (gpMSound) {
        gpMSound->stopAllSound();
    }

    // NOTE (disabled): draining the GP here (GXDrawDone), then invalidating
    // the texture cache after the restore (GXInvalidateTexAll), was an
    // attempt to stop an in-flight goop EFB copy from clobbering the restore
    // and to force the GPU to re-read our restored bytes. The real cause of
    // goop not restoring was Dolphin serving a stale cached copy of the
    // pollution texture -- fixed by Texture Cache Accuracy = Safe -- not GP
    // timing, and on console the game already invalidates every frame. Left
    // out to avoid the one-frame stall; re-enable for a console test if
    // needed. (#include "Dolphin/GX.h")
    // GXDrawDone();

    bool ints = OSDisableInterrupts();
    // Silence the DAC across the interrupts-off restore so the frozen audio
    // DMA doesn't buzz; restored just before interrupts come back.
    bool dma = muteAudioDma();

    for (u32 i = 0; i < h->region_count; i++) {
        const RegionEntry &r = h->regions[i];
        wordCopy(reinterpret_cast<void *>(r.addr), bufferPtr() + r.buf_offset,
                 r.size);
        // wordCopy() has already placed the restored bytes in D-cache, so the
        // CPU can use them immediately. Store them for GX/DMA visibility, but
        // do not flush-and-invalidate the whole stage heap: doing so makes the
        // next frame fault every restored line back in from RAM.
        DCStoreRange(reinterpret_cast<void *>(r.addr), r.size);
        // No instruction-cache invalidation: we never restore .text.
    }

    // (see note above) GXInvalidateTexAll();

    // TMarDirector::mStopwatch (the Piantissimo-chase / blooper-race mission
    // countdown, restored above as part of the heap region) stores an
    // absolute OSGetTime() timestamp in mLast rather than an elapsed
    // duration -- OSCheckStopwatch() computes `total + (now - mLast)`. A
    // byte-for-byte restore puts back the OLD mLast, so on the very next
    // check the timer would read as if it had kept running in real time
    // across the save/load gap instead of rewinding. Shift mLast forward by
    // exactly that real-time gap so OSCheckStopwatch() reproduces the same
    // value it had at save time.
    if (gpMarDirector) {
        OSTime delta                = OSGetTime() - h->save_time;
        gpMarDirector->mStopwatch.mLast += delta;
        DCStoreRange(&gpMarDirector->mStopwatch, sizeof(OSStopwatch));
    }

    unmuteAudioDma(dma);
    OSRestoreInterrupts(ints);

    setStatus("loaded");
    return true;
}

void SavestateManager::updateHook(TMarioGamePad *controller) {
    if (!controller) return;

    const u32 ri = controller->mButtons.mRapidInput;

    // mRapidInput is rising-edge per frame -- one trigger per press.
    if (ri & JUTGamePad::DPAD_LEFT) {
        saveState();
    } else if (ri & JUTGamePad::DPAD_RIGHT) {
        loadState();
    }
}

void SavestateManager::draw(J2DOrthoGraph *ortho) {
    (void)ortho;
#if ENABLE_SAVESTATE_DBG
    if (mInfoText) {
        mInfoText->draw(20, 60);
    }
#endif
}
