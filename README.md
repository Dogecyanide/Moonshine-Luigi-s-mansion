# Moonshine Luigi's Mansion

An experimental port of [Moonshine](https://github.com/panther03/moonshine)'s
Wii/Nintendont savestate architecture to the Japanese release of Luigi's
Mansion (`GLMJ01`).

## Current status

`Full-State Experimental 0.3.13` is the current hardware-testable state build.

- The custom Nintendont launcher accepts only the verified Japanese `GLMJ01`
  revision-0 executable for injection.
- A measured 512 KiB MEM1 window holds the LM payload without patching the ISO.
- One transactional slot uses Moonshine's protected 15.94 MiB MEM2 bank.
- The slot captures the complete secondary gameplay heap, its allocator and
  disposer metadata, audited gameplay-static SDATA/SBSS slices with known
  live-owned blocks excluded, LM's standalone camera/viewport state, the
  main-loop control pair, both grain-effect managers and their list sentinels,
  the verified room/map flag slice, and libc RNG state.
- Save/load is refused while DVD, ARAM, or memory-card work is active, while
  the heap is unstable, when a slot checksum fails, or when the observed live
  allocator/resource markers differ from the saved ones.
- Each transaction drains LM's prior JAudio scene handles while preserving its
  required replacement bootstrap handle, then holds the OS scheduler while
  only lock-free snapshot work runs.
- Moonshine's ARM crash writer now accepts LM exception reports and rotates
  `susamune_crash_a/b.bin` plus readable `.txt` reports on the game-source
  storage device.
- A cache-coherent phase journal records the last completed save/load step in
  `/ndebug.log`, even when the PowerPC hard-locks and no exception is raised.
- State requests run after LM's complete framebuffer/retrace routine, matching
  Moonshine's proven post-draw timing. Additional journal markers split the
  first restored draw into matrix, scene-callback, and projection stages, then
  identify the exact direct renderer call if the callback does not return.
- Post-load tracing remains armed for eight complete restored frames and also
  brackets the main-loop tail, so a delayed hard lock retains its exact frame
  number and last entered retail call.
- Cross-room refusals retain a complete 22-field epoch mask plus the saved and
  live values of the highest-priority mismatch on both the overlay and in
  `/ndebug.log`. This is diagnostic only; every compatibility gate remains on.
- Volume-list refusals also walk and validate every mounted archive, preserve a
  bounded save-time census outside the snapshot, and identify the first two
  removed or added volumes with their object/backing heap ownership.

Controls are D-pad Left to save and D-pad Right to load. Start with same-room
tests. This first build normally rejects a load with `EPOCH` after the room,
scene, resource, or uncaptured allocator markers change. Those markers are not
a complete resource fingerprint, so this remains a crash-risk heap-state
feasibility test, not cross-room support. Audio may remain silent after a
save or load until game logic starts the room sequence again.

The inherited Sunshine payload remains in the repository as porting reference.
Its build targets are hidden unless CMake is explicitly configured with
`-DLM_BOOTSTRAP=OFF`; do not apply those DOL/BPS/mod-bin outputs to Luigi's
Mansion.

## Goal

The target is exact savestates between any ordinary mansion rooms in `map2`,
including transitions between floors. Floor is not the compatibility boundary:
allocator and resource lifetime are. Boss arenas and other maps are explicitly
out of scope for the first implementation.

As with Moonshine, the release architecture must boot a clean, verified
Japanese `GLMJ01` image and inject the game-side payload at runtime through the
custom Nintendont launcher. A pre-patched ISO is neither a user requirement nor
a distributable release artifact.

See [the GLMJ01 porting plan](doc/glmj01-porting.md) for the architecture,
runtime gates, and test ladder.

## Build the launcher

On Windows with Python, CMake, Ninja, Git LFS, and the repository's LFS objects
present:

```powershell
python setup_venv.py
cmake --preset diagnostic_console
cmake --build --preset diagnostic
```

The build emits a version-labelled tester package plus a stable compatibility
name:

```text
build-lm-diag/Moonshine-Luigis-Mansion-Full-State-Experimental-0.3.13.zip
build-lm-diag/moonshine_luigis_mansion_launcher.zip
```

Both ZIPs are byte-identical. Use the version-labelled file when sharing a
build; every future `LAUNCHER_VERSION` automatically gets its own filename.

Extract it so the SD card contains:

```text
apps/moonshine_luigis_mansion/boot.dol
apps/moonshine_luigis_mansion/icon.png
apps/moonshine_luigis_mansion/meta.xml
apps/moonshine_luigis_mansion/mod_lmj.bin
```

The launcher stores its own settings in `/moonshine_lm.ini`. No game image is
included or accepted into this repository; test with a legally dumped Japanese
disc or ISO.

## Wii experimental-state test

Back up any real memory-card data, install the four packaged files under
`apps/moonshine_luigis_mansion/`, and launch a clean revision-0 GLMJ01 image.
The overlay must say `LM STATE X0.3.13`; wait until `F`, `C`, `H`, and `G` are
`OK` and `ST` is at least 3. If `ST` remains zero, photograph the short gate
name and eight-digit value shown after `G:`; they identify the rejected live
condition without weakening it.

Press D-pad Left once. `S:SAVED` and a nonzero `SZ` confirm a committed slot.
Change a visible state in the same room, then press D-pad Right once. A good
first restore says `S:LOADED`. `BUSY`, `BADCRC`, `BADHEAP`, `EPOCH`, or
`TOOBIG` is a deliberate refusal and should be photographed with the rest of
the overlay. For a preflight mismatch that reports `EPOCH`, the `E:` row
identifies the first differing field, the `M` value records every differing
preflight field, and the final pair is `saved>live`. A later generic refusal
can still show `E:NONE M00000000`.

For a volume refusal, `V:` shows saved/live member counts, total removals and
additions, and whether the live order is an exact saved-list suffix (`HEAD1`,
`HEAD2`, or `HEADN`). The following two rows name the first changed archives;
`O` classifies the object owner and `B` identifies the heap range containing
the validated RARC backing bytes; both use game, system, root, or unknown.
`VC` and `D` compare the current-volume pointer and directory ID. These rows
are observational only and never weaken the restore gate.

After same-room restores repeat reliably, an adjacent-room attempt should
normally report `EPOCH`; a load that gets past that gate is explicitly unsafe
in this build. Photograph any different result. If the game crashes, save the
newest `susamune_crash_a.txt` or `susamune_crash_b.txt` from the game-source
device before the next experiment overwrites the older rotating report.
If it hard-locks without a new crash report, power it off, return the SD card,
and preserve `/ndebug.log`; its final `Susamune: phase` line identifies the
exact restore or first-post-load boundary that was reached.

## Lineage and credits

- [Moonshine](https://github.com/panther03/moonshine), by Dogecyanide,
  panther03, and contributors, supplies the savestate and launcher foundation.
- [Nintendont](https://github.com/FIX94/Nintendont) and
  [Better Nintendont](https://github.com/SuperrSonic/Better-Nintendont) supply
  the GameCube-on-Wii runtime.
- [Yasiki](https://github.com/Moddimation/Yasiki),
  [Booldozer](https://github.com/ColinShark/Booldozer), and the Luigi's Mansion
  decompilation ecosystem provide reverse-engineering reference material.

This is experimental software. Keep real memory-card data backed up while
testing early builds.
