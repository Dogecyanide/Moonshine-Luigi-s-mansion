# GaddWarp JP 2.2 compatibility inventory

This document records a binary comparison of `GaddWarpJP2.2.xdelta` against
the verified Japanese revision-0 Luigi's Mansion image. It is an integration
reference, not a source reconstruction and not permission to distribute either
disc image.

## Fingerprints

| Input or result | Fingerprint |
| --- | --- |
| xdelta SHA-256 | `DD45D711A4E7F591CD82A5711109B4C6BEAAF1CD1167F4A9A10DC8DA07D0067B` |
| Clean GLMJ01 ISO SHA-256 | `A1B2944B95DD3A40263565775ED029CE60AF6075A9F3947DB0347A7599941DA7` |
| Patched ISO SHA-256 | `182D26DB2FDB3FE30EFDE513CB324DFF23D961445288937909C60DF1D52B7898` |
| Clean `main.dol` SHA-1 | `722005EA9C1EAB54B114F814734D8F327E5614EE` |
| Patched `main.dol` SHA-1 | `7A234EBEFCA5FD34A2C52669C142EB1E4D87D143` |

Both images remain `GLMJ01`, revision 0, and exactly 1,459,978,240 bytes.
BI2 and the apploader are unchanged. The patched disc is fully repacked, so
xdelta size and file relocation are not useful measures of feature locality.

## Disc filesystem changes

The clean image contains 820 files in 59 directories. The patch retains every
disc path and adds eight files:

```text
Event/event102.szp
Event/event103.szp
Event/event104.szp
Event/event105.szp
Event/event106.szp
Event/event107.szp
Event/event108.szp
Event/event109.szp
```

Of the 820 common files, 697 are byte-identical and 123 have changed content.
The changed set owns substantial game data:

- 99 existing Event archives plus the eight additions. The new scripts and
  messages expose the `Gadd OS 1.0` practice interface, including room, boss,
  map, out-of-bounds and hallway warps, flags/settings, Boss Rush, and Portrait
  Rush.
- Every `Map/map0.szp` through `Map/map13.szp`, with changes to event,
  character, generator, path, and JMP data as well as added assets.
- Six room archives, `Game/game.szp`, `Kawano/res_slct`, `model/baby.szp`, and
  the opening banner.
- The banner identifies `Gadd Warp JP v2.2` and replaces its image and metadata.

Some rebuilt archives also contain `.DS_Store`, `._` files, backups, and debug
output. Those are packing artifacts, not features to preserve.

## Executable changes

The clean `main.dol` is `0x394940` bytes. The patched DOL is `0x395B14` bytes,
an increase of 4,564 bytes. Its original sections retain their virtual
addresses and sizes, while new sections include:

| Section | Address range | Purpose observed |
| --- | --- | --- |
| added text | `804B0040..804B00FC` | two persistence wrappers |
| added text | `8038B840..8038C940` | pre-entry relocation/patch loader |
| added data | `804B0008..804B0014` | wrapper state |

The entry point changes from retail `0x80003100` to `0x8038B840`. The new
entry deliberately overlays the beginning of retail BSS, installs or relocates
its payload with cache maintenance and branch rewriting, then calls the retail
entry point. This loader must not be composed with Moonshine's launcher-side
injection.

Exactly 42 existing instruction words change:

- 17 call sites are redirected through the two functions at `0x804B0040` and
  `0x804B0088`. The wrappers preserve object fields `+0x1184/+0x1188` around a
  particular retail create/spawn path.
- 25 other words directly change branches, comparisons, IDs, or constants.

The patch also edits actor-registry records, a function-pointer table, four
other records, and two embedded `staticdata_arc` members (`ninlogo.pcm` and
`res_titl.szp`). Their exact ownership must be recovered from source before
reimplementation.

## Interaction with the Moonshine port

GaddWarp's loader and data around `0x804B0000` are a direct collision risk for
any future bottom-of-arena Moonshine code reservation. Its room-observation,
MissionMode, map-archive, and heap-group functions remain byte-identical to
retail, so the patch provides no alternate room-load-complete hook.

Do not layer this xdelta over Moonshine and do not copy its injected ranges.
Keep the release flow on a clean GLMJ01 image. When source becomes available,
integrate features individually into the authenticated runtime payload and
reconcile any required Map/Event assets deliberately. Until then, use this
inventory only to avoid hook, memory, and resource conflicts.
