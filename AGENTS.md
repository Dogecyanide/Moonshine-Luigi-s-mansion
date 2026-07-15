# susamune — context for agentic sessions

A speedrun-practice mod for **Super Mario Sunshine (JP, GMSJ01)**. The mod's code is injected into the game at runtime — the primary distribution is a **custom Nintendont** (Homebrew Channel app) that patches GMSJ01 in memory on boot, so end users need only a real disc (or their own ISO on SD) and **no patched ISO/DOL**. Dolphin remains the primary *development* environment (via a patched `main.dol`). The companion repo `../../src/sms` is the in-progress decompilation of the game and the source of truth for any game-side type layouts; refer to it freely when sizing a struct or tracing a code path.

## Repository layout

The mod sources live at the repo root (there is no longer a `susamune/` subdirectory):

- `src/*.cpp` — mod source, auto-globbed by CMake:
  - `main.cpp` — hook entry points. `onUpdate` runs each frame in place of `mDirector->direct()`, `afterDraw` at the end of rendering, `onSetup` once per stage load, `onUpdateGameMode` in the game-mode loop. Also holds `getArenaLo` (heap-reservation hook, see below).
  - `savestate.cpp` / `savestate.hxx` — the savestate feature.
  - `settings_menu.cpp` — the warp/settings menu (compiled always, but only wired up when `ENABLE_WARP_MENU` is set).
- `scripts/patches.py` — the hook table + memory-reservation config (addresses, mod region size, game id, region/disc metadata).
- `maps/<vers>.map` / `maps/<vers>.ld` — the CodeWarrior map and the linker script regenerated from it (`scripts/map_to_ld.py`).
- `include/` — reverse-engineered game headers (`include/JSystem` too).
- `scripts/` — the build pipeline (see "Build & test loop").
- `launcher/` — the custom Nintendont fork + launcher packaging.

## Injection architecture (how the mod gets into the game)

The mod is compiled + partial-linked into one relocatable object, then linked **into MEM1** at `__ArenaLo` (`0x80426020`). MEM1 placement is mandatory: PPC `bl`/`b` reach only ±32 MB, so linking the blob far away in MEM2 produces "relocation truncated to fit".

Room for the blob is carved from the **bottom of the game's heap arena**, not the stack:

- `getArenaLo` (`src/main.cpp`) is hooked onto `OSGetArenaLo` (`0x8008dcbc`, a `PatchType.B` in `patches.py`). It returns `__OSArenaLo + kArenaReserve`, so the root heap starts `kArenaReserve` bytes higher and `[__ArenaLo, __ArenaLo + mod_region_size)` is free for the mod's code + data.
- `kArenaReserve` in `main.cpp` **must equal** `mod_region_size` in `patches.py` (currently `0x8000`).
- We deliberately do **not** touch the top of the arena (the apploader stores the FST there — an earlier arena-*top* reservation overwrote the FST and crashed before the logo) and do **not** shrink the stack (that starved the scene-load stack → black screen).

`scripts/patches.py` `patches[]` also installs the actual mod hooks (`onUpdate`, `onSetup`, `afterDraw`, `onUpdateGameMode`) plus a couple of boot-speedup NOPs. Every hook is realized as a concrete `(addr, word)` write in the generated manifest.

## Savestate feature (savestate.cpp / savestate.hxx)

Goal: emulator-style savestates triggered by the d-pad (LEFT = save, RIGHT = load) so a runner can practice the same approach repeatedly without resetting the level. Designed to work on real Wii hardware under Nintendont — MEM2 has plenty of room beyond Nintendont's working set for a single-slot snapshot.

### Where game state lives, and what we copy

Three buckets:

1. **The stage heap** — `gpApplication.mCurrentHeap`, a `JKRSolidHeap` created in `TApplication::initialize_nlogoAfter` that fills the remaining root-heap free space. This is where almost every per-stage allocation lives: TMario, every enemy, MapObj, particles, the scene graph, the camera. We copy the whole range `[mCurrentHeap, mCurrentHeap->mEnd)`.

2. **The "game half" of `.bss` / `.sdata` / `.sbss`.** Pointers from heap-resident objects into BSS (TFlagManager singleton ptr, gpMarDirector, gpMSound, the libc rand seed, every game module's static counters) need to survive a load, so we restore the chunks of BSS that hold mutable game state.

   Boundaries (derived from `maps/jp.map`):
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
- **`gpCardManager`** — has its own worker thread + mutex + cond var on the root heap. Byte-copying those would trash kernel-side bookkeeping. Instead, `loadState` spins (yielding) on `gpCardManager->getLastStatus() == CARD_ERROR_BUSY` until the card thread is idle before restoring anything. (Note the launcher disables memory-card *emulation* by default — see below — but the game still runs the same CARD code paths against real/absent EXI.)

### Invariants enforced before load

- Snapshot magic matches (`'SUSA' = 0x53555341`)
- Snapshot version matches (`kSnapshotVersion`, currently 4; bump when the layout breaks)
- `gpApplication.mCurrentHeap` is at the same address as save time (heap moved → all pointers stale)
- Heap size matches
- Area + episode IDs match — same-scenario only. Cross-scenario would have to deal with `freeAll()` between directors.

### Where the snapshot buffer lives

In MEM2. Configured via `kSnapshotBase` near the top of `savestate.cpp`, gated on the `IS_EMULATOR` define set by the CMake `IS_EMULATOR` option:

- **Dolphin** (`#if IS_EMULATOR`): `0x70000000` — emulator's "free" space.
- **Wii (Nintendont)**: `0x91F00000` — right after Nintendont's 3 MB DI cache; free until `0x92F00000` where the Nintendont kernel itself loads. **This is version-dependent**; tune per Nintendont build if you see weird behavior after the first save.

Buffer is sized at 16 MB reserved (`kSnapshotReservedSize`). Layout: 256-byte header (magic, version, heap addr/size, area/episode, region table) at the base, then the region payloads packed back-to-back.

## Known issues / open work

- **Buffer placement on console** is best-effort. The previous attempt at `0xD1000000` (uncached MEM2 + 16 MB) collided with Nintendont; current `0x91F00000` is the educated guess. If saves work but subsequent Nintendont I/O explodes, this is the first thing to move.
- **Async DVD load mid-snapshot** — there's no `JKRDvdRipper::isIdle()`-style check. If a snapshot is taken mid-archive-load, you'd capture an inconsistent heap. In practice gameplay is quiescent, but a robust version would gate on this.
- **Cross-scenario load** is refused. To support it we'd need to trigger the same boot-time setup the game's stage transition does — much more involved.
- **Loading restarts audio silent** — sounds and BGM that were playing at save time are stopped, not resumed. The next stage event re-triggers them, so it usually self-corrects within a second. If we want true seamless audio resume, we'd need to capture the active BGM ID separately and re-issue it.

## How to extend

**Adding a new tracked root-heap allocation:**
1. Find the global pointer's address in `maps/<vers>.ld` (e.g. `grep -n "Symbol__" maps/jp.ld`).
2. Determine the pointed-to size — usually `sizeof(SomeClass)` after including the header.
3. Add an entry to `kPointedAllocs` in `savestate.cpp`.
4. `kMaxRegions` is computed automatically; verify `sizeof(SavestateHeader) < kHeaderSize` (currently 256 bytes — generous headroom).

**Adding a new static range** (e.g. some BSS modules in the OS half turn out to be safe to snapshot): add to `kStaticRanges`. The load loop is uniform, no other changes needed.

**Adding a new hook:** add an entry to `patches[]` in `scripts/patches.py` (`sym` = an `extern "C"` symbol in `src/`, `type` = `B`/`BL`/`W32`) and implement the symbol. The manifest/launcher/dol/gecko targets all pick it up automatically.

**Bumping the snapshot format**: increment `kSnapshotVersion`. MEM2 is volatile across power cycles, so there's no data migration concern; the version field exists to invalidate stale snapshots left in MEM2 by a different build of the mod.

## Launcher (custom Nintendont) — `launcher/`

`launcher/` is a Nintendont fork: an **ARM kernel** (`launcher/kernel`, built with devkitARM) plus a **PPC loader/GUI** (`launcher/loader`, built with devkitPPC + libogc). Packaged as the HBC app `susamune_launcher/{boot.dol, icon.png, meta.xml}`.

Key modifications from stock Nintendont:

- **Mod injection** — `scripts/build_launcher.py` generates `launcher/kernel/susamune_inject.h` (the mod blob + the `(addr, word)` write list from the manifest, guarded by `SUSAMUNE_GAME_ID`). `launcher/kernel/Patch.c`'s `PatchSusamune()` (called from `PatchGame` after `DoPatches`) checks the running game id equals `SUSAMUNE_GAME_ID`, memcpy's the blob into MEM1 at `base & 0x7FFFFFFF`, applies each write, and flushes. The checked-in `susamune_inject.h` is a no-op placeholder until a build regenerates it.
- **Gecko-code relocation** — the mod raises the arena floor by `mod_region_size` (`0x8000`), shifting every bottom-anchored heap allocation up by that much, which breaks Gecko practice codes that write to *hardcoded heap addresses*. `PatchSusamuneGeckoCodes()` (Patch.c, called right after the `.gct` is copied to `cheats_start`) scans the loaded cheat list for the known signature of the SMS "fast text" code (`dpad.txt` — it stamps Shift-JIS `!!!` into a buffer at `0x808D8A7E`) and bumps its two hardcoded addresses by `SUSAMUNE_ARENA_RESERVE` (exported into `susamune_inject.h` from the manifest's `region_reserve`, which is `mod_region_size`). The signature is 24 bytes (three code lines) so false matches are implausible. Add more `(signature → relocate)` entries here if other absolute-heap-address codes need it; the general alternative is a top-of-arena reservation (keeps bottom-anchored data at stock addresses, no per-code signatures).
- **Real-disc (RealDI) boot** — pass a `di` game path (the loader accepts a `di:` arg / a `d…` argv). The RealDI disc buffer was relocated into the ISO-cache region (`0x11000000`, shrunk to 3 MB) so it no longer collides with the savestate mod's MEM2 window at `0x91F00000`.
- **Memory-card emulation disabled by default** — `launcher/loader/source/main.c` clears `NIN_CFG_MEMCARDEMU | NIN_CFG_MC_MULTI` and zeroes `MemCardBlocks` under `#ifndef ENABLE_MEMCARD_EMU`, *before* the card-file init and the kernel handoff. This skips the loader's card-file creation and the kernel's `GCNCard_Load` (both flag-gated), so no card file is created and no emulation code runs; the game falls back to real-EXI/SRAM (reports no memory card). Define `ENABLE_MEMCARD_EMU` to restore stock behavior.

### Windows build detour (temporary)

The vendored Nintendont Makefiles assume a POSIX shell + devkitPro. `scripts/build_launcher.py` bridges this on Windows: it locates Git Bash's `sh`, unpacks the bundled `launcher/nintendont_devkitpro_win32.zip` toolchain (a Git LFS file) if `DEVKITPPC`/`DEVKITARM` aren't in the environment, mirrors the missing `libwinpthread` DLL next to `cc1`, installs Python shims for the absent `bin2s`/`elf2dol` tools (`scripts/bin2s.py`, `scripts/elf2dol.py`), renders `meta.xml` from `launcher/meta.xml.j2` (jinja2 + git hash), and zips `susamune_launcher/{boot.dol, icon.png, meta.xml}`. **This whole detour is meant to be temporary** — the intent is to port the launcher build to CMake and delete the make/Git-Bash/DLL/shim machinery. Until then, expect `make`-driven output during the `launcher` target.

## Build & test loop

First-time setup: `python setup_venv.py` (creates `venv/` with the Python packages the build scripts need). Then:

```
cmake --preset default                  # configure (Ninja, build/ dir)
cmake --build build                     # build build/susamune_manifest.json (default `manifest` target)
cmake --build build --target dol        # patch a main.dol from the source ISO (Dolphin dev)
cmake --build build --target iso        # rebuild build/susamune_<vers>.iso
cmake --build build --target launcher   # build the Nintendont HBC app zip (build/susamune_launcher.zip)
cmake --build build --target gecko      # emit build/susamune.txt (Dolphin cheat form; currently broken in-game)
```

`cmake -B build -G Ninja` works too; the preset just bakes in Ninja and the build dir. The Visual Studio generator is rejected.

Pipeline: CMake compiles/partial-links `src/*.cpp` into `build/susamune_pre.o` with the Kuribo clang fork, then `scripts/link_mod.py <mode>` links it against `maps/<vers>.ld` and emits one of three outputs from the same hook table:

- `launcher` → `build/susamune_manifest.json` (base address, code blob, and the `(addr, word)` writes realizing every hook plus the arena-reservation) — the default `manifest` target. The `launcher` target feeds it to `build_launcher.py`.
- `patch_dol` → a patched `main.dol` (the `dol` target; `iso` wraps it back into an ISO).
- `gecko` → a Gecko cheat list (the `gecko` target).

The build uses Kuribo's clang fork (`KURIBO_COMPILER_HOME`, defaulting to `toolchain/`) with `-Werror`, c++17, no exceptions/RTTI/standard library. The source ISO defaults to `<ISO_CODE>.iso` in the repo root (e.g. `GMSJ01.iso`); override with `-DSMS_ISO=<path>` or the `SMS_ISO` env var. The linker script is regenerated on demand with `cmake --build build --target regen_ld` after `maps/<vers>.map` changes.

Build options (toggle in `ccmake`/`cmake-gui`, or with `-D<name>=<value>`):

- `IS_EMULATOR` (default OFF) — ON targets Dolphin (`kSnapshotBase = 0x70000000`), OFF targets Wii/Nintendont (`0x91F00000`). Sets the `IS_EMULATOR` compile define. **Test on Dolphin first** (`-DIS_EMULATOR=ON` + the `dol`/`iso` targets); when building the launcher for console, leave it OFF and double-check `kSnapshotBase` against the current Nintendont build's MEM2 layout.
- `ENABLE_SAVESTATE_DBG` (default OFF) — savestate debug output.
- `ENABLE_WARP_MENU` (default OFF) — wire up `settings_menu.cpp`'s warp menu.
- `UPDATE_ISO_METADATA` (default OFF) — for the `iso` target, bump the game code (01→02), swap banner/name, etc.
- `VERS` (jp/us/pal, default jp) — game version; only `jp` is currently filled in (`us`/`pal` addresses are `None` in `patches.py`).

## Decomp cross-reference

For any game-side type whose layout you need precisely, the canonical source is `../../src/sms` (the SMS decomp). The headers under `include/` are reverse-engineered approximations that are good enough to compile against, but if a field offset matters (e.g. the offset of `mGamePads` in `TApplication`, or the exact size of `TFlagManager`), confirm against the decomp.
