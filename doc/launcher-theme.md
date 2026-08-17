# Launcher background themes

The Susamune launcher creates a `theme` directory beside its `boot.dol`. Put an
optional background there using this exact name:

- `theme/background.png` — PNG, exactly 1024x480 pixels, at most 2 MiB.
- `theme/bgm.mp3` — MP3, at most 4 MiB. It loops while the launcher is open.

The path follows the launcher itself. For example, a launcher at
`sd:/apps/susamune/boot.dol` reads `sd:/apps/susamune/theme/background.png`
even if the game is on USB or is a real disc. A launcher started from USB reads
its theme from USB. Failure to create the directory is nonfatal.

The image is validated before decoding. A missing, oversized, corrupt, or
wrong-sized image leaves the embedded stock background in use. Theme failures
do not prevent the game from booting. If a file is present but cannot be used,
the launcher displays the reason briefly before continuing with the stock
background. A missing file is normal and does not show a warning.

JPEG, WebP, GIF, and renamed files are not supported. The custom background uses
the moving, darkened panorama effect; the embedded fallback keeps its normal
static presentation.

Launcher text keeps the redistributable embedded DejaVu Sans Mono Bold font.
Every black string is drawn with a one-pixel white outline in all eight
directions, matching Mare's high-contrast treatment. Mare's Super Mario Script
font is not bundled because the available font file contains no redistribution
license metadata.

The early IOS/kernel preparation screens still use the embedded background,
because the SD or USB volume is not mounted yet. The custom image begins on the
launcher menu after the storage check.

PNG decoding is streamed from FatFS one row at a time. The launcher releases
the embedded 640x480 texture before allocating the 1024x480 replacement, so it
does not hold the stock texture, the whole compressed PNG, and the custom
texture at once. The persistent texture increase is 737,280 bytes; the bounded
row buffer is 4,096 bytes. The exact 688,061-byte Mare test image reached a
2,024,304-byte isolated theme-heap high-water in the PowerPC decoder test:
1,966,080 bytes of final texture plus 58,224 bytes of allocator and libpng
overhead. Runtime logs sample the whole launcher heap while the decoder is
active. A decode or I/O failure reconstructs the embedded texture before the
launcher continues; if even that allocation fails, drawing switches to a plain
solid background that needs no texture data.

The MP3 is read only after the launcher's device is mounted. A missing file is
normal and leaves the launcher silent. An empty, oversized, unreadable, or
malformed file shows a nonfatal warning and also leaves it silent. The header
preflight skips a bounded ID3 tag and requires two consecutive, compatible MPEG
audio frames before libmad receives the file. The compressed file stays in one
32-byte-aligned buffer for playback; the 3,111,007-byte Mare reference file uses
3,111,008 heap bytes. No music is embedded in the distributed launcher.

ASND and the MP3 player are initialized once during early launcher startup,
before Nintendont's IOS reload. ASND owns AUDIO/DSP initialization; calling the
low-level initializers first consumed one-shot DSP state and was the cause of
the unsafe Pre-release 2 lifecycle. The file can be loaded later without
initializing the DSP a second time. The bundled MP3 player's priority-80 decoder
is dispatched ahead of the launcher's priority-64 main thread before a
successful start returns. A loop restart happens only after that decoder has
finished its ASND cleanup and reports that it is no longer playing. Shutdown
first disables loop restarts, then stops and joins an active decoder, ends ASND,
and frees the file buffer before game handoff, return-to-loader, or poweroff.
