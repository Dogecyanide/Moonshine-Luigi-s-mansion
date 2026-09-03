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

The six overlay rows are:

```text
LM STATE X0.3.10 F:<floor> C:<canary> H:<heap check>
S:<state status> ST<stable frames> SZ<snapshot KiB> G:<gate> <gate value>
ROOT <root> <start>-<end>
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
records while saving, loading, and traversing the restored-frame trace window.
The ARM writes and syncs these independently, so the last record survives a
PowerPC hard lock that never reaches the exception dumper.

Version `0.3.9` additionally captures GLMJ01's standalone `0x270`-byte
camera/viewport state block at `0x80398770-0x803989E0`. The normal-room draw
path reads its projection, viewport, scissor, and matrix fields directly. The
following display object is deliberately excluded because it owns live
double-buffer pointers.

For `0.3.10`, `80` means the load returned at the true post-presenter boundary.
The immediate main-loop tail is bracketed by `90/91`, `92/93`, and `94/95`;
the next update uses `88/89`, `8A/8B`, optionally `8C/8D`, and `8E/8F`.
`81` through `86` bracket the following complete presenter, diagnostic copy,
and tick. This repeats for eight restored presentations and retains the loop
tail/update that follows the eighth. For loop markers, `arg0` is the number of
restored presentations already completed; for `81` through `86`, it is the
one-based presentation ordinal (`80` uses zero). The ARM logger may miss fast
intermediate values, but an entry value remains the final record when its
corresponding retail call hard-locks.

Inside `8A/8B`, `A0/A1` bracket the first position-matrix upload, `A2/A3`
bracket the final normal-matrix upload, `A4/A5` bracket the active scene's
draw callback, and `A6/A7` bracket the final orthographic-view reset. A last
`A1` therefore means one of the intervening matrix uploads stalled; a last
`A4` identifies the scene draw callback itself. In `0.3.9`, `A4/A5` record the
main draw state in `arg0` and callback address in `arg1`. `B0/B1` bracket each
direct call in the Main Game draw dispatcher, while `C0/C1` bracket every
direct call in its normal-room renderer. For those records, `arg0` is the
retail call site and `arg1` is its original callee; a final `B0` or `C0`
therefore identifies the exact call that did not return.

Version `0.3.10` adds `D0/D1` around every direct call inside the central
per-view routine at `0x8000BA64`, which `0.3.9` isolated. These records use the
same `arg0` call-site and `arg1` callee convention.

If the inner game loop exits during that window, `96/97` identify loop
entry/return, `98/99` bracket outer cleanup, and `9A/9B` bracket its restart.
The invocation containing the load can only emit `97` because tracing was not
armed at its entry. A final `97` isolates the following scene-table virtual
call.

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
