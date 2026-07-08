# susamune — context for Claude Code sessions

A speedrun-practice mod for **Super Mario Sunshine (JP, GMSJ01)** that injects code into `main.dol`. Target platform is **Wii via Nintendont**, but Dolphin is the primary development environment. The companion repo `../../src/sms` is the in-progress decompilation of the game and the source of truth for any game-side type layouts; refer to it freely when sizing a struct or tracing a code path.

The hook points live in `susamune/src/main.cpp` — `onUpdate` runs each frame in place of `mDirector->direct()`, `afterDraw` runs at the end of rendering, `onSetup` runs once per stage load. Build with `scons`; patches are wired up in `susamune/patches.py` and the linker script is regenerated from `susamune/maps/jp.map` into `susamune/maps/jp.ld`.

## Savestate feature (savestate.cpp / savestate.hxx)

Goal: emulator-style savestates triggered by the d-pad (LEFT = save, RIGHT = load) so a runner can practice the same approach repeatedly without resetting the level. Designed to work on real Wii hardware under Nintendont — MEM2 has plenty of room beyond Nintendont's working set for a single-slot snapshot.

### Where game state lives, and what we copy

Three buckets:

1. **The stage heap** — `gpApplication.mCurrentHeap`, a `JKRSolidHeap` created in `TApplication::initialize_nlogoAfter` that fills the remaining root-heap free space. This is where almost every per-stage allocation lives: TMario, every enemy, MapObj, particles, the scene graph, the camera. We copy the whole range `[mCurrentHeap, mCurrentHeap->mEnd)`.

2. **The "game half" of `.bss` / `.sdata` / `.sbss`.** Pointers from heap-resident objects into BSS (TFlagManager singleton ptr, gpMarDirector, gpMSound, the libc rand seed, every game module's static counters) need to survive a load, so we restore the chunks of BSS that hold mutable game state.

   Boundaries (derived from `susamune/maps/jp.map`):
   - `.bss   [0x803e6000, 0x803e604c)` — `gpApplication` only (TApplication struct)
   - `.bss   [0x803f1c50, 0x80408ac0)` — first game module is `MarioUtil/DrawUtil.cpp`
   - `.sdata [0x80409008, 0x804097ac)` — first game module is `MoveBG/MapObjGeneral.cpp`
   - `.sbss  [0x8040a208, 0x8040b45c)` — first game module is `MarioUtil/DrawUtil.cpp`

   Everything *below* those addresses is JKR / JAudio / runtime / OS / DVD / VI / PAD / CARD / GX / SI / EXI / THP / debugger state which we deliberately leave alone. Restoring OS thread queues, DVD command queues, audio DSP mailboxes, etc. crashes the console.

3. **Tracked root-heap allocations** (the `kPointedAllocs` table). The `JKRSolidHeap` snapshot does NOT cover the root heap — there's a swath of allocations made in `TApplication::initialize()` (TFlagManager, gamepads, fader, rumble manager, ...) that live on the root heap before the JKRSolidHeap starts. The pointers to them are in BSS and get preserved by bucket 2 above, but the pointed-to bytes need a separate snapshot. For each one we follow the pointer at save time and capture the target as another region.

   Current table (savestate.cpp `kPointedAllocs`):

   | name | pointer addr | size | notes |
   |---|---|---|---|
   | TFlagManager | `0x8040a290` (`TFlagManager::smInstance`) | `sizeof(TFlagManager)` = 0x380 | Fixes coin counts, shine flags, episode flags |
   | TTimeRec | `0x8040a2f8` (`TTimeRec::_instance`) | 0x820 (the 0xDFC0 in `TTimeRec::start` is a config arg, not the size) | Input/profiler recorder |
   | SMSRumbleMgr | `0x8040a248` | `sizeof(RumbleMgr)` = 0x30 | Rumble channels' active state |
   | Pad0..Pad3 | `0x803e6020 + 4*i` (`gpApplication.mGamePads[i]`) | `sizeof(TMarioGamePad)` = 0xF0 | Fixes controller-stuck-disabled bug after loading from dialog |
   | Fader | `0x803e6034` (`gpApplication.mFader`) | `sizeof(TSmplFader)` = 0x38 | Fixes crash when loading during shine-spawn / shine-get / blue save screen fade transitions. The `TSMSFader` header previously omitted two fields (`_30` at 0x30 = current wipe type, `_34` at 0x34 = wipe progress float), so `sizeof` evaluated to 0x30 instead of the real 0x38. The wipe-type field at 0x30 controls which rendering path `draw()`/`drawFadeinout()` take and is passed to `Hx_GetWipeType`; the global wipe-system state (`hx`/`hx_buffer` in BSS) IS snapshotted, so a stale `_30` left it out of sync with the restored global state and crashed on the next `draw()`. |

### What we deliberately do NOT snapshot

- **`.data`** — almost entirely vtables and static const tables, read-only at runtime.
- **stack** — we're running on it.
- **System BSS below the game-half boundaries** — JSystem / JAudio / OS / DVD / VI / PAD / CARD / GX / SI / EXI / THP / TRK. Hardware-tied or kernel-tied state.
- **Audio** — `gpMSound`'s playing-sounds list references DSP-side state we can't snapshot. We call `gpMSound->stopAllSound()` before BOTH save and restore, so the audio engine never sees inconsistent state. Music re-triggers naturally on the next stage event.
- **`gpCardManager`** — has its own worker thread + mutex + cond var on the root heap. Byte-copying those would trash kernel-side bookkeeping. Instead, `loadState` spins (yielding) on `gpCardManager->getLastStatus() == CARD_ERROR_BUSY` until the card thread is idle before restoring anything.

### Invariants enforced before load

- Snapshot magic matches (`'SUSA' = 0x53555341`)
- Snapshot version matches (`kSnapshotVersion`; bump when the layout breaks)
- `gpApplication.mCurrentHeap` is at the same address as save time (heap moved → all pointers stale)
- Heap size matches
- Area + episode IDs match — same-scenario only. Cross-scenario would have to deal with `freeAll()` between directors.

### Where the snapshot buffer lives

In MEM2. Configured via `kSnapshotBase` near the top of `savestate.cpp`, gated on a `SUSAMUNE_EMULATOR` define from `susamune/config.hxx`:

- **Dolphin**: `0x70000000` — emulator's "free" space.
- **Wii (Nintendont)**: `0x91F00000` — right after Nintendont's 3 MB DI cache; free until `0x92F00000` where the Nintendont kernel itself loads. **This is version-dependent**; tune per Nintendont build if you see weird behavior after the first save.

Buffer is sized at 16 MB reserved (`kSnapshotReservedSize`). Layout: 256-byte header (magic, version, heap addr/size, area/episode, region table) at the base, then the region payloads packed back-to-back.

## Known issues / open work

- **Buffer placement on console** is best-effort. The previous attempt at `0xD1000000` (uncached MEM2 + 16 MB) collided with Nintendont; current `0x91F00000` is the educated guess. If saves work but subsequent Nintendont I/O explodes, this is the first thing to move.
- **Async DVD load mid-snapshot** — there's no `JKRDvdRipper::isIdle()`-style check. If a snapshot is taken mid-archive-load, you'd capture an inconsistent heap. In practice gameplay is quiescent, but a robust version would gate on this.
- **Cross-scenario load** is refused. To support it we'd need to trigger the same boot-time setup the game's stage transition does — much more involved.
- **Loading restarts audio silent** — sounds and BGM that were playing at save time are stopped, not resumed. The next stage event re-triggers them, so it usually self-corrects within a second. If we want true seamless audio resume, we'd need to capture the active BGM ID separately and re-issue it.

## How to extend

**Adding a new tracked root-heap allocation:**
1. Find the global pointer's address in `susamune/maps/jp.ld` (e.g. `grep -n "Symbol__" maps/jp.ld`).
2. Determine the pointed-to size — usually `sizeof(SomeClass)` after including the header.
3. Add an entry to `kPointedAllocs` in `savestate.cpp`.
4. `kMaxRegions` is computed automatically; verify `sizeof(SavestateHeader) < kHeaderSize` (currently 256 bytes — generous headroom).

**Adding a new static range** (e.g. some BSS modules in the OS half turn out to be safe to snapshot): add to `kStaticRanges`. The load loop is uniform, no other changes needed.

**Bumping the snapshot format**: increment `kSnapshotVersion`. MEM2 is volatile across power cycles, so there's no data migration concern; the version field exists to invalidate stale snapshots left in MEM2 by a different build of the mod.

## Build & test loop

```
scons dol     # build patched main.dol
scons iso     # rebuild susamune_jp.iso
```

Configured in `SConstruct`. The build uses Kuribo's clang fork (`KURIBO_COMPILER_HOME`) with `-Werror`, c++17, no exceptions/RTTI/standard library. Source files are auto-globbed from `susamune/src/*.cpp`. The linker script is auto-regenerated from `susamune/maps/jp.map` whenever the map changes.

Test on Dolphin first (set `SUSAMUNE_EMULATOR 1` in `susamune/config.hxx`). When porting to Nintendont, flip the define and double-check the `kSnapshotBase` address against the current Nintendont build's MEM2 layout.

## Decomp cross-reference

For any game-side type whose layout you need precisely, the canonical source is `../../src/sms` (the SMS decomp). The susamune headers under `susamune/include/` are reverse-engineered approximations that are good enough to compile against, but if a field offset matters (e.g. the offset of `mGamePads` in `TApplication`, or the exact size of `TFlagManager`), confirm against the decomp.
