# MEM1 stability test

The 96 KiB build moves the game heap floor up by 32 KiB relative to the
previous release. Console results are authoritative; Dolphin can miss real
overlap and cache failures.

## Diagnostic overlay

Configure an all-region diagnostic build with:

```text
cmake --preset release_console -B build_memdiag -DENABLE_MEM_DIAGNOSTICS=ON
cmake --build build_memdiag --target mod_bins
```

The overlay is always visible in this build and allocates no heap:

```text
MEM R:8043E020 E:8043E020 F:OK C:OK
ST 7/0 F:1234K I:1250K M:1210K H:OK
```

- `R`: root-heap object address.
- `E`: exclusive end of the 96 KiB mod region.
- `F`: root heap and its first usable byte are outside the mod region.
- `C`: the unused scratch-tail canary has never changed.
- `ST`: current area and episode.
- `F`: current largest free stage-heap span.
- `I`: free stage heap when `setupObjects()` returned.
- `M`: lowest observed free stage heap in this stage visit.
- `H`: all setup and periodic stage-heap structural checks have passed. `WAIT`
  is normal while the asynchronous stage setup is running.

Any `BAD` is a failure. Photograph the overlay after a fresh load and again at
the lowest observed `M`. A minimum below 32 KiB is a warning that needs an A/B
check against the 64 KiB build even if the stage still runs.

## Primary JP and PAL stress

Test JP first because it is the main run version, then repeat the same sequence
on PAL because its blob is largest and its fixed QFT scratch crosses a signed
low-half boundary in the 96 KiB layout.

For both Sirena 1 (Phantamanta) and Pinna Park interior:

1. Cold boot with cheats off and load the stage normally.
2. Photograph the overlay immediately after control begins.
3. Play through the busiest part for at least five minutes. Keep effects,
   enemies and overlays active; record the lowest `M`.
4. Instant Restart five times, then Full Restart five times.
5. Leave and re-enter the stage five times. Confirm `F`, `C` and `H` stay `OK`
   and `I` returns to the same value on equivalent loads.
6. Save once during high activity, change the scene for ten seconds, then load
   the state ten times. Confirm `M` never becomes corrupt or implausibly large.
7. Open every menu tab, the input/metadata layout editors and the warp wheel.
   Leave the input, metadata, QFT and attempt overlays enabled together.

A black loading screen tied to one stage suggests low stage-heap headroom.
Failure only after several reloads suggests corruption or a leak. A red `C`
points specifically at the mod/scratch boundary.

## QFT and relocation gate

Run this on JP and PAL before a long soak:

1. Start a normal stage and confirm QFT begins near zero.
2. Enter a secret through its parent stage; time must hold across the loading
   zone and continue rather than reset.
3. Exercise Instant Restart and Full Restart; both start a fresh attempt.
4. Collect a Shine with No Shine Get Animation off, then on. Confirm the timer,
   attempt counter, IL identity and recent result remain coherent.
5. Test a death stop, a boss stop and freeze events at 0.5 and 1 second.
6. In Ricco 2 or a Piantissimo mission, verify the retail timer owns the large
   panel while full-level QFT remains in the compact display.
7. Toggle native Fast Text off/on/off after message data has loaded. Normal
   dialogue must restore exactly. On PAL, repeat in English and French.

If the regional DPad Functions Gecko code is used, test it separately with
native Fast Text off. The launcher should relocate it by `0x1A000`.

## Closing coverage

- JP: run at least 30 mixed stage loads, including Sirena 1, Pinna interior,
  Pianta 3, Noki 4 and Corona/Bowser.
- PAL: repeat the primary stress, QFT gate and ten savestate loads.
- US: three cold boots, Fast Text off/on/off, one parent-to-secret transition,
  one Shine, one restart and five savestate loads.
- Press the console reset button from gameplay once per region. It must return
  to the title rather than reload the current stage.

For a failure, record region, disc or ISO, cheats on/off, stage/episode, exact
action, repetition count, all overlay fields and whether the menu still opens.

## 96 KiB console result

JP and PAL completed the primary pressure pass without a canary, heap, loading
or behavioral failure. The lowest observed stage-heap minimum was 1413 KiB in
JP Bianco 5 and 1438 KiB on PAL. JP Sirena 1 stayed above 2100 KiB and Pinna
Park interior stayed above 2400 KiB. These measurements were taken from the
diagnostic build's per-visit `M` field.
