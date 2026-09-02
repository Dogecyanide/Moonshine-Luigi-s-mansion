# Moonshine Luigi's Mansion

An experimental port of [Moonshine](https://github.com/panther03/moonshine)'s
Wii/Nintendont savestate architecture to the Japanese release of Luigi's
Mansion (`GLMJ01`).

## Current status

This branch is a safe launcher bootstrap, not a playable savestate mod yet.

- The custom Nintendont launcher accepts the Japanese `GLMJ` game code; the
  sourced and verified disc is `GLMJ01`, revision 0.
- The Homebrew Channel app is named `Moonshine Luigi's Mansion`.
- Game-specific injection is compiled out and no `mod_jp.bin` is packaged.
- Sunshine-only disc scanning and ghost-directory creation are disabled.
- Luigi's Mansion therefore boots unmodified while the GLMJ01 hook map is
  being recovered.
- Moonshine's protected MEM2 layout is preserved for the future snapshot
  backend.

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
cmake --preset release_console
cmake --build --preset console
```

The build emits a version-labelled tester package plus a stable compatibility
name:

```text
build/Moonshine-Luigis-Mansion-Bootstrap-0.1.0.zip
build/moonshine_luigis_mansion_launcher.zip
```

Both ZIPs are byte-identical. Use the version-labelled file when sharing a
build; every future `LAUNCHER_VERSION` automatically gets its own filename.

Extract it so the SD card contains:

```text
apps/moonshine_luigis_mansion/boot.dol
apps/moonshine_luigis_mansion/icon.png
apps/moonshine_luigis_mansion/meta.xml
```

The launcher stores its own settings in `/moonshine_lm.ini`. No game image is
included or accepted into this repository; test with a legally dumped Japanese
disc or ISO.

## Wii bootstrap smoke test

Back up any real memory-card data, install the three packaged files under
`apps/moonshine_luigis_mansion/`, and launch the app from the Homebrew Channel.
The menu must show `Japanese (GLMJ01)` as the fixed target and
`[bootstrap: no payload]` in the build label. Select a clean revision-0 GLMJ01
disc or image and confirm that Luigi's Mansion reaches gameplay and behaves
normally through several door transitions and a save/load cycle.

Stop if the target label or bootstrap label differs. At this phase, seeing any
game-side Moonshine menu or practice feature is a packaging error.

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
