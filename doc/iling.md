# ILs

ILs is the Y+Start menu's level-select and PB system. Its catalog contains all
96 non-blue-coin Shines, 10 independent no-FLUDD secret-stage splits, and 11
Any% Plaza segments:

- 56 episode Shines
- 14 course bonus Shines
- 8 hundred-coin Shines
- 15 Delfino Plaza Shines
- 2 Airstrip Shines
- Corona Mountain

The 24 blue-coin-shop Shines are deliberately excluded.

The Any% group is the final menu group. Its rows are Bianco Plant, Delfino
Shadow Mario, Travel Skip, Gelato Plant, Pianta Enter, Honey Skip, Ricco Enter,
Bianco II Enter, Sirena Enter, Noki Enter and Corona Enter. Z toggles the PB
fanfare directly on this tab.

## Timing and identity

A Shine PB ends at QFT's Shine-stop event: the first frame of the Shine Get
cutscene, after `TMario::winDemo` finishes its initial jump. It does not end on
raw `TShine::touchPlayer`. Corona Mountain ends at QFT's Bowser-stop event.
Any% portal splits end on QFT's loading-zone capture. The hook publishes the
target with the time, so the PB appears on the touch frame even when an entry
demo later replaces QFT's visual freeze. Honey Skip ends on the game-over flag
write, and Bianco Plant ends on the final damaging hit rather than the later
death animation.

Scenes cannot identify a result. An episode Shine, a hidden Shine and a
100-coin Shine can all exist in the same director, and Plaza has many Shines in
one scene. `onFireGetStar` therefore publishes `TShine::mMapObjID` (the decomp's
event id at `TShine+0x134`) before the Shine event serial. No Shine Get
Animation's cave publishes the same id. ILs accepts a result only when its
event id and its configured attempt origin both match.

QFT accumulates time through normal parent-to-secret and parent-to-boss loading
zones. ILs carries the attempt through those directors. The no-FLUDD secret
episodes have separate Full, Secret and Reds rows. QFT's attempt serial changes
on a real restart; resetting inside the child cancels the Full attempt and arms
that child as a fresh Secret or Reds attempt instead.

Both QFT displays hold the captured split while entering a loading zone, but
the clock continues through the load. When a retail mission timer owns the
large Sunshine panel, Susamune leaves it untouched and keeps the full-level QFT
visible in the compact display. Ricco 2 Full accepts either retail race result
id from its parent-stage origin while the direct Race row keeps its own PB.
Gelato 8 also accepts a Gelato 1 origin for Gelato Beach Skip; Pinna 6 Full
accepts Pinna 3 and Pinna 5 origins for Early Yoshi-Go-Round.

## Starts and temporary progression

Ordinary entries use `LevelWarp::warpTo`. Any% entries use
`LevelWarp::warpFrom` so Plaza sees the configured previous scene and selects
the retail post-Shine return point, orientation and animation. In particular,
Noki to flooded Plaza uses the game's own falling Corona-facing return.

Some direct secret and Plaza entries need temporary progression flags. ILs
records the values it changes and applies them after the old director has
finished. Full and Secret keep the primary Shine clear through
`setMario()` so it removes FLUDD; Reds keeps it set through the same point.
Launching Full or Secret also selects the global No FLUDD mode; launching Reds
selects All secrets, and the menu saves that choice normally.
ILs then restores the flag before playable frames, reapplying it briefly when
a Full attempt enters its child stage. This prevents a pause-menu or blue-coin
card save from serialising practice-only progression. Restoration is
conflict-aware: a real gameplay write that replaced a temporary value wins.

Plaza scenarios also snapshot the four packed story bits that Force Plaza
Events changes. Their route profile remains active only as long as the segment
needs it, while unrelated bits in the same byte are preserved. Pause, card-save,
wrong-exit and context-abort paths restore the original profile.

The route-specific 100-coin starts are:

| Course | Episode |
|---|---:|
| Bianco Hills | 6 |
| Ricco Harbor | 3 |
| Gelato Beach | 3 |
| Pinna Park | 8 |
| Sirena Beach | 7 |
| Noki Bay | 2 |
| Pianta Village | 5 |
| Delfino | Plaza |

## Persistence

PB slots are stable across catalog reordering: ordinary rows use retail Shine
event ids, slots 70-79 hold the ten independent Secret splits, and the Any%
rows use 80-85, 108-111 and 120. These override slots are deliberately outside
the Shine rows exposed by ILs.

The PPC and ARM kernel exchange the 121 values through the independent
`SusamuneILingPbCfg` mailbox appended to `SusamuneCfg`. It has its own control,
acknowledgement and payload cache lines; settings saves never flush or
invalidate the PB lines.

The console build keeps its live 121-value mirror in the final 512 bytes of the
reserved config window. The ARM never touches that PPC-only mirror, so the
mailbox payload stays immutable while a save is in flight and later PB changes
can queue safely for the next write.

The kernel writes two fixed binary generations per game region on the device
that launched Susamune:

```text
/susamune_pbs_v1_<region>_a.bin
/susamune_pbs_v1_<region>_b.bin
```

Each file includes magic, version, region game id, generation and checksum. A
save overwrites only the inactive generation, so an interrupted write leaves
the previous valid list recoverable. PB files are intentionally separate from
`susamune.ini` and its text copy-through size ceiling. The format version is
also part of the filename so an older launcher cannot overwrite a newer
journal after a downgrade.

Savestates keep enough attempt bookkeeping to restore temporary practice flags,
but loading one disarms PB recording for the rest of that attempt. A level
reset or fresh stage load arms PBs again. Savestates never rewind the PB list
or its save transaction.

## Console test matrix

Do not test all 96 entries for every change. Cover the distinct mechanisms:

1. A normal main-stage Shine after natural entry and level reset.
2. Bianco 3 from parent stage through its secret.
3. One Full/Secret/Reds trio, including a reset and FLUDD state.
4. Delfino Box Game 2 or another one-time Plaza object.
5. Airstrip 1; its first-visit scenario may need more progression flags.
6. Corona through the Bowser stop.
7. Bianco Plant's final hit and Honey Skip's death endpoint.
8. One Any% portal split, plus the Noki-to-Corona falling spawn.
9. Delete confirmation, fanfare Off, and PB survival across a reboot.
