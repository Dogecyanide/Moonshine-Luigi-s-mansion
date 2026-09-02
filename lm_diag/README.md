# GLMJ01 MEM1 diagnostic

This payload is the first game-side test for the Japanese Luigi's Mansion
revision (`GLMJ01`, revision 0). It reserves the Moonshine 512 KiB MEM1 window
at `0x804B8400-0x80538400` and renders a heap report with LM's retail font.
The launcher authenticates the clean DOL layout and every hook word before it
copies or patches anything; another revision runs unmodified.

The five overlay rows are:

```text
LM MEM F:<floor> C:<canary> H:<heap check>
R <root> <start>-<end> <size KiB>
S <system> L/T/m <largest>/<total>/<minimum total KiB>
G <game>   L/T/m <largest>/<total>/<minimum total KiB>
C <current> g<group> A <raw low>><raised low>-<initial high>
```

`WAIT` means that the relevant heap has not existed long enough to test.
`OK` means the condition has been observed and remains valid. `BAD` is latched
after a real floor, canary, or `JKRExpHeap::check` failure; a normal room-load
gap does not turn the heap check bad.

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
