# susamune — context for agentic sessions

A speedrun-practice mod for **Super Mario Sunshine** (JP GMSJ01, US GMSE01, and PAL GMSP01). The mod's code is injected into the game at runtime — the primary distribution is a **custom Nintendont** (Homebrew Channel app) that patches the selected disc revision in memory on boot, so end users need only a real disc (or their own ISO on SD) and **no patched ISO/DOL**. Dolphin remains the primary *development* environment (via a patched `main.dol`). The companion repo `../../src/sms` is the in-progress decompilation of the game and the source of truth for any game-side type layouts; refer to it freely when sizing a struct or tracing a code path.

## Repository layout

The mod sources live at the repo root (there is no longer a `susamune/` subdirectory):

- `src/*.cpp` — mod source, auto-globbed by CMake:
  - `main.cpp` — hook entry points. `onUpdate` runs each frame in place of `mDirector->direct()`, `afterDraw` runs at the end of rendering and processes queued savestate loads after the game's `GXDrawDone` barrier, `onSetup` runs once per stage load, and `onUpdateGameMode` runs in the game-mode loop. Also holds `getArenaLo` (heap-reservation hook, see below).
  - `savestate.cpp` / `savestate.hxx` — the savestate feature.
  - `menu.cpp` / `menu.hxx` — the on-screen menu: presentation and navigation only. A `Menu` frames a list of tabs (`MenuTab` subclasses defined inside `menu.cpp`); each tab owns its own `update`/`draw`. Adding a tab is a new class plus one line in `Menu::Menu()`. Wired up only when `ENABLE_MENU` is set (default ON). Uses **no heap**: the `Menu` and its tabs are placement-new'd once into static BSS buffers, and rendering re-points a single shared `J2DTextBox` at borrowed const strings each frame (the system heap is nearly full — see `getArenaLo`). Feature logic is delegated out: warp tabs call `warp.*`, the savestate tab edits `settings.*`.
  - `warp.cpp` / `warp.hxx` — stage-warp data (`kStageNames`, `kPresets`) and logic. The menu only calls `Warp::request(...)`; `main.cpp`'s `onUpdateGameMode` polls `Warp::pending()` and calls `Warp::execute()` (flag setup + `mNextScene`) then `moveStage()`.
  - `settings.cpp` / `settings.hxx` — persistent mod settings, independent of the menu that edits them and the features that read them. Each setting is one row in `kSettingDescs` (name, type BOOL/CHOICE, default) plus one byte of live value in the global `gSettings`; the menu renders/edits them generically and `savestate.cpp` reads them. `SettingsData` is a POD blob (magic+version) sized for future memory-card / SD serialization. Current settings: `SETTING_SAVE_RNG_STATE` (gates the RNG-seed snapshot range in `savestate.cpp`).
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

   Boundaries are derived separately from each `maps/<vers>.map` and live in `include/susamune/addresses.hxx` (selected by the `SUSAMUNE_VERSION_{JP,US,PAL}` define CMake sets):
   - `gpApplication` only (TApplication struct)
   - game `.bss` — from `MarioUtil/DrawUtil.cpp` to the first JSystem global. **On JP** this window has the SMS audio modules carved out (see below), realised as three sub-ranges `bss-game-1/2/3` in `kStaticRanges` under `#if defined(SUSAMUNE_VERSION_JP)`; US/PAL stay contiguous (`bss-game`) until their audio modules are mapped.
   - game `.sdata` — from `MoveBG/MapObjGeneral.cpp` to the first JSystem global
   - game `.sbss` — from `MarioUtil/DrawUtil.cpp` to the first JSystem global
   - MSL `rand.c` `next`, the libc RNG seed (`SUSAMUNE_ADDR_LIBC_RAND_SEED`, below the sdata-game boundary; pulled in as a one-off so King Boo fruit pulls / manta patterns / enemy RNG rewind on load)

   The linker sections are not game-only: JKR / JAudio / runtime / OS / DVD / VI / PAD / CARD / GX / SI / EXI / THP / debugger state appears both before and after the game subranges depending on the retail build. We deliberately leave that state alone. Restoring OS thread queues, DVD command queues, renderer state, or audio DSP mailboxes crashes or corrupts the console.

   **Audio-module carve-out.** Two chunks *inside* the game-bss range are excluded because they hold live JAudio handles/track pointers: `System.a/MSoundMainSide.cpp` `[0x803f2c38, 0x803f2cf0)` and the whole `MSound.a` cluster `[0x803f44d0, 0x803f57a0)` (`MAnmSound` … `MSModBgm`). We `stopAllSound()` on both save and load (resetting JAudio), so restoring an *old* copy of those handles would point them at track state that has since been freed/reused → dangling deref; leaving the audio BSS untouched keeps it consistent with the post-`stopAllSound` JAudio state. (This was originally an attempt to fix the Pianta-talk shine-get hang below — it did **not** fix it — but it's kept as a correctness improvement, since restoring stale JAudio handles is wrong regardless.)

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
- **System storage outside the game-only ranges** — JSystem / JAudio / OS / DVD / VI / PAD / CARD / GX / SI / EXI / THP / TRK. Hardware-tied or kernel-tied state.
- **Audio** — `gpMSound`'s playing-sounds list references DSP-side state we can't snapshot. We call `gpMSound->stopAllSound()` before BOTH save and restore, so the audio engine never sees inconsistent state. Music re-triggers naturally on the next stage event. On JP this is also why the SMS audio-module BSS is carved out of the game-bss range (see the audio-module carve-out above) — restoring it would put back stale JAudio handles.
- **`gpCardManager`** — has its own worker thread + mutex + cond var on the root heap. Byte-copying those would trash kernel-side bookkeeping. Instead, `loadState` spins (yielding) on `gpCardManager->getLastStatus() == CARD_ERROR_BUSY` until the card thread is idle before restoring anything. (Note the launcher disables memory-card *emulation* by default — see below — but the game still runs the same CARD code paths against real/absent EXI.)

### Invariants enforced before load

- Snapshot magic matches (`'SUSA' = 0x53555341`)
- Snapshot version matches (`kSnapshotVersion`, currently 7; bump when the layout breaks)
- Snapshot region matches (`game_version` in the header vs `SUSAMUNE_GAME_VERSION`) — a JP snapshot won't load on a US/PAL build and vice versa
- `gpApplication.mCurrentHeap` is at the same address as save time (heap moved → all pointers stale)
- Heap size matches
- Area + episode IDs match — same-scenario only. Cross-scenario would have to deal with `freeAll()` between directors.
- **Not in a load/intro transition** (`inLoadTransition()`): refuses while `gpMarDirector->_260 == 0` (the async setup thread is still populating the heap — the all-black loading screen) or while `mCurState < STATE_GAME_STARTING` (the black init + intro-cutscene states 0/1). It is allowed from `STATE_GAME_STARTING` (2) onward — i.e. as soon as the opening wipe fades the stage back in and Mario plays his materialise-in animation, since the whole stage is loaded and nothing streams in dynamically by then.

### Where the snapshot buffer lives

In MEM2. Configured via `kSnapshotBase` near the top of `savestate.cpp`, gated on the `IS_EMULATOR` define set by the CMake `IS_EMULATOR` option:

- **Dolphin** (`#if IS_EMULATOR`): `0x70000000` — emulator's "free" space.
- **Wii (Nintendont)**: `0x91F00000–0x92F00000` — a dedicated 16 MB window whose exclusive end is the Nintendont ARM kernel. `include/susamune/mem2_map.h` is the shared source of truth for both PPC aliases and ARM physical addresses; compile-time adjacency/alias checks prevent launcher buffers from entering the window.

Buffer is sized at 16 MB reserved (`kSnapshotReservedSize`). Layout: 256-byte header (magic, version, heap addr/size, area/episode, region table) at the base, then the region payloads packed back-to-back.

## Known issues / open work

- **FIXED: console NPC-talk/pause load hang.** Loading during the Pianta-handed shine animation (Pianta Village ep 3/6, Sirena Beach ep 6), and loading from the pause menu in Pianta Village ep 6, used to hard-hang the console. The cause was mid-frame restore ordering: D-pad loads ran from `onUpdate`, after `director->direct()` but before the same frame's fader update/draw, `gpMSound->mainLoop()`, and rendering. Those systems consumed a mixture of restored game state and live audio/GX state. D-pad loads are now queued until `afterDraw`, immediately after the game's `THPPlayerDrawDone()`/`GXDrawDone()` barrier; `loadState()` also enforces its own `GXDrawDone()` and invalidates the texture cache after restore. All four original reproductions were confirmed fixed on Wii hardware. Relocating every Nintendont user out of the snapshot window did **not** resolve this particular hang, but the collision was real and the exclusive MEM2 ownership is retained as a separate correctness fix. Historical observations:
  - **Console only** — cannot reproduce on Dolphin (its permissive memory map tolerates whatever real hardware faults on).
  - The last on-screen status remains `saved`, but that does **not** prove the restore loop itself wedged: `loaded` is rendered only later, after the restored fader/audio state is used again in the same frame.
  - **Not a bad restore-write address.** A temporary guard that validated every restore target against MEM1 (`0x80003100–0x81800000`) and reported out-of-range regions as `R<i> <addr>` never fired — every `r.addr`/`r.size` we write is in range. So it's not a captured pointer resolving to an unmapped address on our side.
  - **No exception dump.** Nintendont installs its own exception handler, so there's nothing on screen, and an `OSSetErrorHandler` overlay from the mod (record SRR0/DAR, skip the faulting instruction, resume so the render loop shows it) never displayed — Nintendont's handler wins and/or the resume didn't take. `OSFatal` isn't linked in the game either.
  - **Ruled out:** the SMS audio-module BSS (carving it out of the snapshot — see the audio carve-out above — did *not* fix this, though it's kept as a correctness improvement); and all shine/talk/demo/event-interpreter state, which the decomp shows is entirely in the captured stage heap.
- **Memory-card emulation remains incompatible with the Susamune MEM2 layout.** It is disabled by default. Stock Nintendont anchors card data at `0x91000000` and can grow into the relocated buffers below the snapshot; do not define `ENABLE_MEMCARD_EMU` without designing another placement.
- **Async DVD load mid-snapshot** — there's no `JKRDvdRipper::isIdle()`-style check. If a snapshot is taken mid-archive-load, you'd capture an inconsistent heap. In practice gameplay is quiescent, but a robust version would gate on this.
- **Cross-scenario load** is refused. To support it we'd need to trigger the same boot-time setup the game's stage transition does — much more involved.
- **Loading restarts audio silent** — sounds and BGM that were playing at save time are stopped, not resumed. The next stage event re-triggers them, so it usually self-corrects within a second. If we want true seamless audio resume, we'd need to capture the active BGM ID separately and re-issue it.

## How to extend

**Adding a new tracked root-heap allocation:**
1. Find the global pointer's address in `maps/<vers>.ld` (e.g. `grep -n "Symbol__" maps/jp.ld`).
2. Determine the pointed-to size — usually `sizeof(SomeClass)` after including the header.
3. Add an entry to `kPointedAllocs` in `savestate.cpp`.
4. `kMaxRegions` is computed automatically; verify `sizeof(SavestateHeader) < kHeaderSize` (currently 256 bytes — generous headroom).

**Adding a new static range** (e.g. some BSS modules in the OS half turn out to be safe to snapshot): add to `kStaticRanges`. The load loop is uniform, no other changes needed. The trailing `gate` field is `kNoGate` for an always-captured range, or a `SettingId` that must be enabled for the range to be captured (this is how `SETTING_SAVE_RNG_STATE` excludes the RNG seed).

**Adding a new setting:** add a `SettingId` enumerator (before `SETTING_COUNT`) in `settings.hxx` and a matching row in `kSettingDescs` (`settings.cpp`) — name, `SETTING_BOOL`/`SETTING_CHOICE`, choice count + labels, default. Nothing else changes: the savestate-settings tab renders/edits it generically, and features read it via `gSettings.get*/`. `SettingsData::values` is sized off `SETTING_COUNT`.

**Adding a new menu tab:** define a `MenuTab` subclass in `menu.cpp` (implement `title`/`update`/`draw`, keep per-tab cursor state in the object), give it a static BSS buffer next to the others, and add one `mTabs[mNumTabs++] = new (buf) YourTab();` line in `Menu::Menu()`. `kMaxTabs` caps the count; the tab strip scrolls horizontally when titles overflow. Tabs draw via `Menu::drawText` / `J2DFillBox` and must not allocate.

**Adding a new hook:** add an entry to `patches[]` in `scripts/patches.py` (`sym` = an `extern "C"` symbol in `src/`, `type` = `B`/`BL`/`W32`) and implement the symbol. The manifest/launcher/dol/gecko targets all pick it up automatically.

**Bumping the snapshot format**: increment `kSnapshotVersion`. MEM2 is volatile across power cycles, so there's no data migration concern; the version field exists to invalidate stale snapshots left in MEM2 by a different build of the mod.

## Launcher (custom Nintendont) — `launcher/`

`launcher/` is a Nintendont fork: an **ARM kernel** (`launcher/kernel`, built with devkitARM) plus a **PPC loader/GUI** (`launcher/loader`, built with devkitPPC + libogc). Packaged as the HBC app `susamune_launcher_<vers>/{boot.dol, icon.png, meta.xml}`.

Key modifications from stock Nintendont:

- **Mod injection** — `scripts/gen_inject_header.py` generates `susamune_inject.h` (the mod blob + the `(addr, word)` write list from the manifest, guarded by `SUSAMUNE_GAME_ID`). It's a **build artifact** written into the CMake build dir (next to the manifest), *not* version-controlled; the root `CMakeLists.txt` generates it from the manifest and passes its directory to the launcher build as `SUSAMUNE_INJECT_DIR`, which the kernel's CMake target adds to its include path. `launcher/kernel/Patch.c` includes it under `__has_include` so the kernel still compiles standalone (the `PatchSusamune*` functions fall back to no-ops when `SUSAMUNE_GAME_ID` is undefined). The kernel's `Patch.c` object has an explicit `OBJECT_DEPENDS` on `susamune_inject.h`, so a mod-source edit relinks only the kernel + loader rather than rebuilding the whole launcher. `PatchSusamune()` (called from `PatchGame` after `DoPatches`) checks the running game id equals `SUSAMUNE_GAME_ID`, memcpy's the blob into MEM1 at `base & 0x7FFFFFFF`, applies each write, and flushes.
- **Gecko-code relocation** — the mod raises the arena floor by `mod_region_size` (`0x8000`), shifting every bottom-anchored heap allocation up by that much, which breaks Gecko practice codes that write to *hardcoded heap addresses*. `PatchSusamuneGeckoCodes()` (Patch.c, called right after the `.gct` is copied to `cheats_start`) recognizes the full regional DPad Functions code tails (`dpad.txt`, `dpad_us.txt`, and `dpad_pal.txt`) via `RelocateSusamuneGeckoSignature()` and bumps only their message-buffer writes by `SUSAMUNE_ARENA_RESERVE` (exported into `susamune_inject.h` from the manifest's `region_reserve`, which is `mod_region_size`). The long, region-specific signatures make false matches implausible. Add more `(signature → relocate)` entries here if other absolute-heap-address codes need it; the general alternative is a top-of-arena reservation (keeps bottom-anchored data at stock addresses, no per-code signatures).
- **Real-disc (RealDI) boot** — pass a `di` game path (the loader accepts a `di:` arg / a `d…` argv). The RealDI disc buffer was relocated into the ISO-cache region (`0x11000000`, shrunk to 3 MB) so it no longer collides with the savestate mod's MEM2 window at `0x91F00000`.
- **Reserved savestate MEM2 window** — `include/susamune/mem2_map.h` packs the 3 MB ISO/RealDI cache and all relocated Nintendont scratch/handoff buffers below physical `0x11F00000`, reserves physical `0x11F00000–0x12F00000` exclusively for the snapshot, and places the ARM kernel at `0x12F00000`. The loader uses cached `0x9xxxxxxx` aliases; the kernel uses physical `0x1xxxxxxx` addresses. `launcher/mem_map.txt` documents the same layout.
- **Memory-card emulation disabled by default** — `launcher/loader/source/main.c` clears `NIN_CFG_MEMCARDEMU | NIN_CFG_MC_MULTI` and zeroes `MemCardBlocks` under `#ifndef ENABLE_MEMCARD_EMU`, *before* the card-file init and the kernel handoff. This skips the loader's card-file creation and the kernel's `GCNCard_Load` (both flag-gated), so no card file is created and no emulation code runs; the game falls back to real-EXI/SRAM (reports no memory card). The stock emulation buffer overlaps the packed MEM2 layout; do not define `ENABLE_MEMCARD_EMU` without first assigning it a non-overlapping region and adding that region to `mem2_map.h`.
- **"Unlock Read Speed" (`NIN_CFG_REMLIMIT`) enabled by default** — `launcher/loader/source/main.c` sets this bit when it builds the fresh `NIN_CFG` (i.e. no `nincfg.bin` exists yet), so first boot has read-speed unlocking on without the user visiting the settings menu.

### How the launcher build is structured (CMake)

The launcher spans three cross toolchains and CMake binds one compiler per build tree, so `launcher/CMakeLists.txt` is a **super-build**: it configures three `ExternalProject`s, one per toolchain group, each with its own toolchain file under `launcher/cmake/`:

- **ppc** (`toolchain-devkitPPC.cmake`, bare `powerpc-eabi-`): `multidol`, `resetstub`, `PADReadGC` (loader/source/ppc), the `kernel/asm` PPC code blobs, `codehandler`, and `fatfs-ppc`. Produces the `data/*.bin` blobs, the generated `asm/*.h` + `codehandler*.h` headers, and `libfatfs-ppc.a`.
- **arm** (`toolchain-devkitARM.cmake`, big-endian `arm-none-eabi-`): `fatfs-arm`, the `kernel` (→ `kernel.zip`), and `kernelboot`.
- **wii** (`toolchain-wii.cmake`, `powerpc-eabi-` + libogc): the `loader` GUI, embedding every data blob and converting to `boot.dol`.

Groups are wired **ppc → arm → wii** via `ExternalProject` `DEPENDS` and share three output dirs (`build/launcher/shared/{gen,data,lib}`). The root project drives the whole thing through an `ExternalProject_Add(nintendont_launcher)` that depends on the generated `susamune_inject.h`; the final `susamune_launcher_<vers>.zip` is packaged by `scripts/package_launcher.py` (jinja2 `meta.xml` + region icon).

**Toolchain sourcing** (was the old Git-Bash/`make` detour): `launcher/cmake/BundledToolchain.cmake` uses `DEVKITPPC`/`DEVKITARM` from the environment if set, otherwise extracts the bundled `launcher/nintendont_devkitpro_win32.zip` (a Git LFS file) and, on Windows, mirrors the `libwinpthread` DLL next to `cc1`. The devkitPro tools the bundle lacks are replaced from CMake: `scripts/bin2s.py` (data → object, via `launcher/cmake/Embed.cmake`), `scripts/bin2h.py` (blob → header), `scripts/elf2dol.py` (ELF → DOL), and `kernel.zip` is produced with `cmake -E tar --format=zip`. No `make`, Git Bash, or tool shims are involved anymore.

## Build & test loop

First-time setup: `python setup_venv.py` (creates `venv/` with the Python packages the build scripts need). Then:

```
cmake --preset default                  # configure (Ninja, build/ dir)
cmake --build build                     # build build/susamune_manifest.json (default `manifest` target)
cmake --build build --target dol        # patch a main.dol from the source ISO (Dolphin dev)
cmake --build build --target iso        # rebuild build/susamune_<vers>.iso
cmake --build build --target launcher   # build the Nintendont HBC app zip (build/susamune_launcher_<vers>.zip)
cmake --build build --target gecko      # emit build/susamune.txt (Dolphin cheat form; currently broken in-game)
```

`cmake -B build -G Ninja` works too; the preset just bakes in Ninja and the build dir. The Visual Studio generator is rejected.

Pipeline: CMake compiles/partial-links `src/*.cpp` into `build/susamune_pre.o` with the Kuribo clang fork, then `scripts/link_mod.py <mode>` links it against `maps/<vers>.ld` and emits one of three outputs from the same hook table:

- `launcher` → `build/susamune_manifest.json` (base address, code blob, and the `(addr, word)` writes realizing every hook plus the arena-reservation) — the default `manifest` target. The `launcher` target turns it into `susamune_inject.h` and feeds it to the launcher super-build.
- `patch_dol` → a patched `main.dol` (the `dol` target; `iso` wraps it back into an ISO).
- `gecko` → a Gecko cheat list (the `gecko` target).

The build uses Kuribo's clang fork (`KURIBO_COMPILER_HOME`, defaulting to `toolchain/`) with `-Werror`, c++17, no exceptions/RTTI/standard library. The source ISO defaults to `<ISO_CODE>.iso` in the repo root (e.g. `GMSJ01.iso`); override with `-DSMS_ISO=<path>` or the `SMS_ISO` env var. The linker script is regenerated on demand with `cmake --build build --target regen_ld` after `maps/<vers>.map` changes.

Build options (toggle in `ccmake`/`cmake-gui`, or with `-D<name>=<value>`):

- `IS_EMULATOR` (default OFF) — ON targets Dolphin (`kSnapshotBase = 0x70000000`), OFF targets Wii/Nintendont (the dedicated `0x91F00000–0x92F00000` window). Sets the `IS_EMULATOR` compile define. **Test on Dolphin first** (`-DIS_EMULATOR=ON` + the `dol`/`iso` targets); for console builds, leave it OFF and keep all launcher MEM2 changes in `include/susamune/mem2_map.h`.
- `ENABLE_SAVESTATE_DBG` (default OFF) — savestate debug output.
- `ENABLE_MENU` (default ON) — wire up `menu.cpp`'s in-game menu (warps + settings). Compiled regardless; this flag only controls whether `main.cpp` constructs/updates/draws it. (Formerly `ENABLE_WARP_MENU`, default OFF.)
- `UPDATE_ISO_METADATA` (default OFF) — for the `iso` target, bump the game code (01→02), swap banner/name, etc.
- `VERS` (jp/us/pal, default jp) — game version. This selects the map/linker script, patch addresses, game ID, and the C++ MEM1 layout definitions.

## Decomp cross-reference

For any game-side type whose layout you need precisely, the canonical source is `../../src/sms` (the SMS decomp). The headers under `include/` are reverse-engineered approximations that are good enough to compile against, but if a field offset matters (e.g. the offset of `mGamePads` in `TApplication`, or the exact size of `TFlagManager`), confirm against the decomp.
