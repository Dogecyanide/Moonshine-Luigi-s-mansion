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

In case you would like to maintain a different set of gecko codes for this practice rom and the standard ROM, or different in-game settings (for example if you would like to keep texture cache accuracy on fast for the standard ROM), you can instead configure with:

```
cmake --preset release_emu -DUPDATE_ISO_METADATA=ON
```

and build as before. This will produce an iso with game code GMSJ02 instead of GMSJ01, which will have a different settings file in Dolphin that can have its own cheats and etc.
