# Moonshine Luigi's Mansion

An experimental port of [Moonshine](https://github.com/panther03/moonshine)'s
Wii/Nintendont savestate architecture to the Japanese release of Luigi's
Mansion (`GLMJ01`).

## Current status

`Full-State Experimental 0.3.2` is the first hardware-testable state build.

- The custom Nintendont launcher accepts only the verified Japanese `GLMJ01`
  revision-0 executable for injection.
- A measured 512 KiB MEM1 window holds the LM payload without patching the ISO.
- One transactional slot uses Moonshine's protected 15.94 MiB MEM2 bank.
- The slot captures the complete secondary gameplay heap, its allocator and
  disposer metadata, the verified room/map flag slice, libc RNG state, and a
  small set of verified scene roots.
- Save/load is refused while DVD, ARAM, or memory-card work is active, while
  the heap is unstable, when a slot checksum fails, or when the observed live
  allocator/resource markers differ from the saved ones.
- Moonshine's ARM crash writer now accepts LM exception reports and rotates
  `susamune_crash_a/b.bin` plus readable `.txt` reports on the game-source
  storage device.

Controls are D-pad Left to save and D-pad Right to load. Start with same-room
tests. This first build normally rejects a load with `EPOCH` after the room,
scene, resource, or uncaptured allocator markers change. Those markers are not
a complete resource fingerprint, and JAudio software state is not reset yet,
so this is a crash-risk heap-state feasibility test, not cross-room support.

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
build-lm-diag/Moonshine-Luigis-Mansion-Full-State-Experimental-0.3.2.zip
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
The overlay must say `LM STATE X0.3.2`; wait until `F`, `C`, `H`, and `G` are
`OK` and `ST` is at least 3. If `ST` remains zero, photograph the short gate
name and eight-digit value shown after `G:`; they identify the rejected live
condition without weakening it.

Press D-pad Left once. `S:SAVED` and a nonzero `SZ` confirm a committed slot.
Change a visible state in the same room, then press D-pad Right once. A good
first restore says `S:LOADED`. `BUSY`, `BADCRC`, `BADHEAP`, `EPOCH`, or
`TOOBIG` is a deliberate refusal and should be photographed with the rest of
the overlay.

After same-room restores repeat reliably, an adjacent-room attempt should
normally report `EPOCH`; a load that gets past that gate is explicitly unsafe
in this build. Photograph any different result. If the game crashes, save the
newest `susamune_crash_a.txt` or `susamune_crash_b.txt` from the game-source
device before the next experiment overwrites the older rotating report.

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
