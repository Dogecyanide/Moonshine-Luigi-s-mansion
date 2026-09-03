# GLMJ01 MEM1 diagnostic

This payload is the first game-side test for the Japanese Luigi's Mansion
revision (`GLMJ01`, revision 0). It reserves the Moonshine 512 KiB MEM1 window
at `0x804B8400-0x80538400` and renders a heap report directly into LM's copied
640x480 YUYV framebuffer with the retail `JUTDirectPrint` bitmap renderer. It
does not depend on a resource font, heap allocation, projection, or scene GX
state. The panel and raw checkerboard are inset from the top to survive normal
capture overscan; the checkerboard remains visible even if the text renderer is
unavailable. The launcher authenticates the clean DOL layout and
every hook word before it copies or patches anything; another revision runs
unmodified.

The five overlay rows are:

```text
LM MEM DIAG 0.2.2 INJECTED F:<floor> C:<canary> H:<heap check>
ROOT <root> <start>-<end> <size KiB>
SYS  <system> L/T/M <largest>/<total>/<minimum total KiB>
GAME <game>   L/T/M <largest>/<total>/<minimum total KiB>
CUR <current> G<group> A <raw low>><raised low> H<initial high>
```

`WAIT` means that the relevant heap has not existed long enough to test.
`OK` means the condition has been observed and remains valid. `BAD` is latched
after a real floor, canary, or `JKRExpHeap::check` failure; a normal room-load
gap does not turn the heap check bad.

Diagnostic packages also force Nintendont's `/ndebug.log` on the game-source
device (the SD card for the current `path_jp=sd:` setup). It records payload
validation, the observed DOL tuple, every preflight word, and successful hook
installation, giving an independent answer if the capture contains no
checkerboard. Full-state builds also append cache-coherent `Susamune: phase`
records while saving, loading, and returning through the first restored frame.
The ARM writes and syncs these independently, so the last record survives a
PowerPC hard lock that never reaches the exception dumper.

For `0.3.6`, the expected first-restored-frame phase order is `80`, `88/89`,
`8A/8B`, optionally `8C/8D`, `8E/8F`, then `81` through `86`. `80` means the
load returned at the true post-presenter boundary. The paired values bracket
the frame-begin, main-scene, optional pre-update, and post-update calls. `81`
through `86` bracket the next complete presenter, diagnostic copy, and tick.
The ARM logger may miss fast intermediate values, but an entry value remains
the final record when its corresponding retail call hard-locks.

For a useful hardware pass, capture the title screen, an active room after a
few minutes, a room transition on the same floor, a floor transition, and the
lowest `S`/`G` values seen during normal play.

Build the Homebrew Channel package with:

```text
cmake --preset diagnostic_console
cmake --build --preset diagnostic
```

The resulting ZIP contains only `boot.dol`, `icon.png`, `meta.xml`, and the
authenticated `mod_lmj.bin`; it does not patch the ISO.
