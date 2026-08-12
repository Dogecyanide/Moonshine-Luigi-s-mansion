# A romhack for Super Mario Sunshine practice

It adds emulator-like savestates to console (Wii through Nintendont)  Vibe coded software, use at your own risk.

It exploits the fact that the Wii has a considerable amount of RAM free when running a Gamecube game through Nintendont, to snapshot more or less the entire state of the game. This lets you save and restore basically anywhere, any time within the same stage/area (including during a cutscene, shine get or death animation, etc.).

## Usage + Caveats

D-pad left to save, d-pad right to load. 
In Nintendont, Please turn cheats OFF.

* JP 1.0 (GMSJ01), US (GMSE01), and PAL (GMSP01) are supported. Builds and
  launchers must match the disc's region.
* You can only save and load from within the same area (the buttons do nothing if there is no savestate or if it belongs to a different area). It uses a pretty strict definition as to what is a different area, for example airstrip before and after collecting FLUDD are different areas. Going to another stage doesn't get rid of your savestate. 
* Loading from a savestate breaks the music and may mess with the audio in other ways, but generally speaking the sound effects still work. In some cases, FLUDD sounds break, like when loading after collecting a shine. Reloading the level should restore the music and sound effects though.
* Saving and loading during stage intro cutscenes works but seems a bit flaky, especially when using a real disc.

## Installation

### Console (wii)

Download the launcher zip from the Releases page. There is one launcher for every region: extract it to your `apps/` folder on your SD card, so that you have `apps:/susamune_launcher/{boot.dol,meta.xml,icon.png,mod_jp.bin,mod_us.bin,mod_pal.bin}`. The launcher detects the disc and loads the matching `mod_*.bin`, so keep those files next to `boot.dol`. By default, the launcher is configured to load from a real disc. If you want to load your game from SD card or USB instead, follow the instructions in the comments in `meta.xml` to change the boot path to your ISO.

Settings and binds are stored per region in `susamune.ini` at the root of the SD card, in `[settings_jp]` / `[binds_jp]` sections and their `us` / `pal` counterparts.

To launch your game, simply go to the homebrew channel and run the susamune launcher app that should appear. Your gecko codes should work out of the box if you are already using Nintendont for practice, They are directly integrated with susamune and do NOT require seperate practice codes to be installed. 

### Emulator

Download the BPS for your region from the Releases page and apply it to a clean
ISO with a BPS patcher such as
[Floating IPS](https://github.com/Alcaro/Flips/releases). The patch verifies
the source image before writing the Susamune ISO.

| Region | Patch | Clean CRC32 | Clean MD5 |
| --- | --- | --- | --- |
| JP 1.0 (`GMSJ01`) | `susamune_jp.bps` | `C3B17583` | `3B07A4BB22DB926B177E207F9D7F0D87` |
| US (`GMSE01`) | `susamune_us.bps` | `771AD977` | `0C6D2EDAE9FDF40DFC410FF1623E4119` |
| PAL (`GMSP01`) | `susamune_pal.bps` | `4C1D3641` | `72C4860D8555D5E790628E348ABC244D` |

To build an ISO directly from source, Windows, CMake and Python in PATH are
required. Place the source ISO at the repo root as `GMSJ01.iso`, `GMSE01.iso`,
or `GMSP01.iso` (or pass its path with `-DSMS_ISO`).

In the root of the repo, first install the virtualenv with:

```
python setup_venv.py
```

Then configure with:

```
cmake --preset release_emu
```

Then build with:

```
cmake --build --preset emu_iso
```

The ISO appears as `build/susamune_<version>.iso`. To build US or PAL, add
`-DVERS=us` or `-DVERS=pal` when configuring CMake. `VERS` is a list — the
console launcher build uses it to pick which `mod_<region>.bin` files to bundle
(`-DVERS="jp;us;pal"`) — but the ISO/DOL targets can only target one disc, so
they follow its first entry.

> [!IMPORTANT]
> Saving and loading the goop is broken in Dolphin unless 'Texture Cache Accuracy' it set to Safe. You can find this option in the 'Hacks' tab of 'Graphics' in the game's config:
> ![dolphin texture cache setting](doc/texture_cache_setting.png)
> It says it degrades performance although on my machine it seems to run fine still. YMMV.

In case you would like to maintain a different set of gecko codes for this practice rom and the standard ROM, or different in-game settings (for example if you would like to keep texture cache accuracy on fast for the standard ROM), you can instead configure with:

```
cmake --preset release_emu -DUPDATE_ISO_METADATA=ON
```

and build as before. This will produce an iso with game code GMSJ02 instead of GMSJ01, which will have a different settings file in Dolphin that can have its own cheats and etc.

## Credits

- https://github.com/DotKuribo/BetterSunshineEngine/
    - Used heavily as reference. Clang fork with CodeWarrior ABI support used to compile mod (toolchain/)
- https://github.com/DotKuribo/SunshineHeaderInterface
    - THANK YOU FOR WRITING THIS
- https://github.com/SuperrSonic/Better-Nintendont
    - Nintendont fork used as base for the launcher in this repo
- https://github.com/doldecomp/sms
    - Used for Claude to know how everything works

## FAQ (Frequently Anticipated Questions) 

### Does it support Gamecube?

No.

### Why isn't this a Gecko code?

Because:

* On console, it relies on a specific memory layout of Nintendont to facilitate the savestates feature. Depending on your settings, version of Nintendont, or if you are using a fork, the savestate buffer might overlap with other things like the disc cache or emulated memory card, and you will get crashes. I am therefore distributing a fork of nintendont that has these things locked down.
* It's not very optimized in terms of code size, and using dol_c_kit I get a code that is a larger than a lot of loaders will accept AFAIK (660 lines). This number seems quite bloated so I think it's just a compilation problem. Theoretically it should be easy to fashion into a gecko code. 
* I might add other features in the future that might exceed the limits of what is reasaonble in a gecko code so I would like to not rely on them.

### What regions/versions are supported?

JP 1.0 (GMSJ01), US (GMSE01), and PAL (GMSP01) are supported. Select the
target with `-DVERS=jp`, `-DVERS=us`, or `-DVERS=pal` at configure time.
