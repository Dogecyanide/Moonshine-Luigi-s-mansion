# GLMJ01 savestate porting plan

## Supported domain

The first target is the unmodified Japanese Luigi's Mansion executable,
`GLMJ01`, disc revision 0. The sourced disc has been verified directly; its
`main.dol` SHA-1 is:

```text
722005ea9c1eab54b114f814734d8f327e5614ee
```

On the verified disc, `main.dol` begins at ISO offset `0x8600` and is
`0x394940` bytes long.

Every hook must also be authenticated by its surrounding instructions and
cross-references; addresses from another region must never be translated by a
fixed offset.

The initial gameplay domain is every reachable ordinary mansion room in
`map2`, including F1, F2, attic, basement, roof, stairs, elevator, and well
routes. The lab, training room, gallery, boss arenas, and ending are different
maps and remain hard incompatibilities at first.

Floor is not a savestate compatibility key. Compatibility is determined by:

```text
build + map + story/blackout state + allocator epoch + resource manifest
```

Luigi's Mansion stores a room's floor and its room-loading adjacency data as
separate map concepts. Two rooms on different floors may therefore be more
compatible than two distant rooms on the same floor.

## Safety model

Keep Moonshine's transactional slot header, checksums, protected MEM2 bank,
cache maintenance, post-render copy barrier, and audio-DMA precautions. Never
copy a snapshot merely because its room number looks compatible.

Every raw image must be validated while it is still in MEM2. Its heap block
links, sizes, group IDs, alignment, used/free topology, and total span must all
be valid. A manifest mismatch is a refusal, not permission to restore
optimistically.

Do not snapshot OS threads, stacks, DVD commands, CARD state, GX command state,
or JAudio internals. Saving or loading while any of those systems is active is
initially refused.

## Slot contents

An exact slot should contain:

- The raw secondary `JKRExpHeap` image, including allocator metadata.
- Audited game-owned static ranges and pointed allocations outside that heap.
- A pointer-free return capsule: map, room, entrance, story and blackout state,
  inventory, HP, money, keys, Boo and room-clear state, RNG, and timer.
- A heap manifest: heap object and bounds, block topology, and allocation-group
  histogram.
- A resource manifest: `MissionMode`, map archive, archive backing address and
  fingerprint, room archives, file-loader registry, persistent external roots,
  disposer lists, and ARAM/audio resource identity.

## Restore paths

### Direct exact restore

Use a direct transplant only when the current game still shares the slot's
proven allocator and resource epoch. `MissionMode` appears to retain the mounted
whole-map archive for the lifetime of normal gameplay, so this may work across
more `map2` doors than a conventional level-based game. Runtime measurement,
not that expectation, decides.

### Retail re-entry plus exact transplant

This is the planned route to total cross-room support:

1. Authenticate the slot before changing the live game.
2. Apply only the pointer-free return capsule needed by the retail loader.
3. Ask Luigi's Mansion's own room controller to enter the saved room and
   entrance.
4. Wait for the door, wipe, cutscene, DVD, ARAM, and audio work to settle for
   several consecutive frames.
5. Build a fresh target manifest and require exact compatibility.
6. Freeze at the post-draw barrier and revalidate both live and saved state.
7. Transplant the raw image.
8. Rebind only individually audited external resource pointers. Never scan
   arbitrary words for values that merely resemble pointers.
9. Validate the restored heaps and roots before resuming.
10. Observe a post-restore verification window of at least 90 frames.

### Soft return

If a room cannot reproduce a compatible allocation layout, an optional and
clearly labelled `SOFT RETURN` may use retail room entry plus the pointer-free
capsule. It can restore practice position and persistent progress, but it is
not an exact savestate and must never be advertised as one.

## Runtime gates

Check these before saving, before re-entry, after re-entry, and again while
execution is frozen for the final copy:

- Exact GLMJ01 build and snapshot-format version.
- Main gameplay scene and supported `map2` target.
- Stable room and entrance with compatible story/blackout state.
- No door animation, wipe, dialogue, cutscene, pause/GBH, death, or capture
  sequence.
- No CARD operation.
- Luigi's Mansion DVD outstanding count is zero, command blocks are idle, and
  the drive is not busy.
- No ARAM DMA or unsafe audio work.
- No heap-group mutation between consecutive stability samples.
- Both heaps and all saved manifests pass structural validation.
- Graphics work has completed before memory is copied.

If a pre-copy gate fails, defer or refuse without altering the slot. If an
unexpected post-copy validation fails, do not resume the corrupted world; use a
controlled reset/recovery path.

## Verified GLMJ01 instrumentation anchors

The function boundaries and semantics below have now been checked against the
actual verified DOL, not merely inferred by version-to-version offsets. This
does not automatically make each function a safe hook site: preserve the shown
instruction signature and verify the intended call-site context before writing
a branch.

| Meaning | Address |
| --- | ---: |
| Root / system / secondary gameplay heap pointers | `804A0B90 / 804A0B94 / 804A0B98` |
| Current scene pointer / scene ID | `80498B18 / 804A0C20` |
| Current map number (`sCurMapNo`; adjacent word is unproven) | `804A0C48` |
| Current GameMode / count / MissionMode | `804A17B0 / 804A17B4 / 804A17C8` |
| DVD file-info array, 64 x `0x88` | `8038FB98` |
| `LMDvdFile::sCurDvdFile` | `80391D98` |
| JKR volume list / current volume / current directory ID (`u32`) | `80494754 / 804A2038 / 804A2040` |
| ARAM command / ARAM-piece command lists | `804946F4 / 80494724` |
| JKR system / current / root heap statics | `804A1FF0 / 804A1FF4 / 804A1FF8` |

| Function lead | Address |
| --- | ---: |
| Allocate / free | `80005F94 / 8000604C` |
| Free heap groups 1 through N | `80006070` |
| Change / get heap group | `800060CC / 80006110` |
| Make secondary heap current | `80006118` |
| Synchronous archive wait wrapper / `LMOpenMemArchive` | `80006808 / 80006860` |
| Loader outstanding-I/O predicate / close | `80006A5C / 80006AB0` |
| Loader/ring init / actual DVD-thread init | `80006480 / 80006CC8` |
| MissionMode ctor / dtor / create / init | `800B8FD0 / 800B90E8 / 800B9204 / 800B9254` |

Useful first-word signatures from the retail DOL are:

| Function | First words |
| --- | --- |
| Free groups 1 through N | `7C0802A6 90010004 9421FFE8 93E10014` |
| Change heap group | `7C0802A6 90010004 9421FFE8 93E10014` |
| Loader outstanding-I/O predicate | `3C608039 80031D98 7C6000D0 3003FFFF` |
| MissionMode create | `7C0802A6 90010004 9421FFE8 93E10014 800D0CD4` |
| MissionMode init | `7C0802A6 3C808035 90010004 4CC63182` |

The actual bodies confirm that `0x80006070` loops over groups 1 through N,
`0x800060CC` changes both gameplay heaps' group IDs, `0x80006A5C` reads the
outstanding counter at `0x80391D98`, and `0x80006AB0` decrements that counter
before closing the DVD command.

MissionMode init reads `sCurMapNo` from `0x804A0C48`, formats
`/Map/map%d.szp`, calls the synchronous wait wrapper at `0x80006808`, and
stores the resulting archive at MissionMode offset `0x18`. That wrapper invokes
the lower loader at `0x800066A8` and waits for its message; the neighboring
`LMOpenMemArchive` implementation at `0x80006860` is not MissionMode's call
target. The destructor unmounts the stored archive. Its singleton at
`0x804A17C8` is written by create and cleared by the destructor.
No instruction in the retail DOL references the adjacent word at `0x804A0C4C`,
so it must not be treated as a verified next-map field.

The scene-transition function at `0x8000B6C0` is a verified hard epoch
boundary. It destroys the current scene and calls the group-free routine with
N = `0x1A`, freeing groups 1 through 26 before selecting the next scene. That
supports refusing exact restores across scenes/maps; it does not imply that an
ordinary room or floor transition has the same lifetime.

The best passive room-activity observation point found so far is the direct
call at `0x800B984C` inside `MissionMode::vt_14`. It invokes the EnManager
update at `0x800E4248` once per MissionMode update. The lower-level actor-room
edge sends occur at `0x800E8318` (left) and `0x800E8344` (entered), with the
cached state committed at `0x800E8348`. These events are actor-scoped and may
occur more than once for adjacency-loaded rooms, so they begin a debounce; they
are not a room-load-complete barrier.

The secondary `JKRExpHeap` census should include its start/end/size at offsets
`0x30/0x34/0x38`, current group at `0x69`, free-list heads at `0x74/0x78`,
used-list heads at `0x7C/0x80`, and every `0x10` block's address, size, flags,
group, and links.

The still-missing proof is the ordinary door/room transition emitter and its
relationship to group-free operations. Recover that call path from the DOL and
log it at runtime before classifying any room pair as direct-restore safe.

## Implementation ladder

### 0. Passive GLMJ01 launcher

- Boot GLMJ01 on Dolphin and Nintendont with no game-side payload.
- Confirm the preserved MEM2 layout has no Nintendont overlap.
- Complete a normal play session with no behavioral change.
- Do not reserve the inherited `0x80000`-byte MEM1 mod window until GLMJ01's
  exact arena floor, debug-stack gap, and root-heap initialization have been
  verified. Keep the 512 KiB design target, but derive a Luigi's Mansion base
  instead of reusing a Sunshine address.

### 1. Hook map and lifetime census

- Verify the DOL hash and recover every GLMJ01 hook by signature.
- Log the current map plus the room-transition destination and entrance.
- Log both heap block lists, group changes, and group-free events.
- Track `MissionMode`, map and room archives, their backing buffers,
  file-loader registries, persistent roots, DVD commands, ARAM work, and audio
  resource sets across doors, stairs, elevator, well/roof, blackout events,
  and scripted warps.
- Measure the actual secondary-heap high-water mark and full slot size against
  the protected MEM2 bank.

Classify each transition:

- A: same external epoch; direct restore candidate.
- B: same map/heap but changed external resources; retail re-entry required.
- C: recreated heap/map/sequence; exact restore prohibited until separately
  solved.

### 2. Same-room exact states

- Implement one transactional slot.
- Verify Luigi, camera, active ghosts, objects, lighting, money, keys, HP, RNG,
  and timer.
- Require 100 repeated loads with no heap drift or registry growth.
- Reject corrupt slots and all saves during DVD, ARAM, or CARD work.

### 3. Direct cross-room experiment

- Test a cleared adjacent door in both directions.
- Test a normal F1/F2 stair transition in both directions.
- Repeatedly run A-to-B-to-A and B-to-A-to-B without making the slot one-shot.

Any external-pointer, archive, audio, ARAM, or allocator failure permanently
moves that transition class to retail re-entry.

### 4. Retail re-entry

- Implement the loader capsule, target-room stabilization, strict manifest
  comparison, and post-copy verifier.
- Start with same-address compatibility.
- Add narrowly defined resource rebinding only after deterministic address
  changes have been measured and explained.

### 5. Full map2 matrix

Save in every reachable mansion room and load it from:

- Another room on the same floor.
- A room on another floor.
- At least one substantially different route history.

Stress representative ordinary doors, stairs, elevator, well/roof, and
blackout cases for 100 cycles. Different-map boss entries remain hard refusals.
Saving during an active door animation or cutscene is not required for the
definition of total cross-room support.

## Dial-back policy

- Unknown runtime lifetime: same-room only.
- Direct cross-room mismatch: retail re-entry only.
- Non-deterministic rebuilt layout: soft return for that room/route.
- Story or blackout incompatibility: refuse or soft return.
- Different map or sequence: hard refusal.

The central ISO-era question is not which floor a room occupies. It is whether
retail re-entry reconstructs the same heap and resource manifest after
different route histories.

## Existing practice-mod compatibility

An existing Luigi's Mansion practice mod is expected to be incorporated later.
Source is strongly preferred. If only an xdelta is available, apply it to a
disposable copy of the verified ISO, then compare the patched filesystem and
main DOL against retail to recover:

- Replaced or added disc files.
- Hook writes and injected executable ranges.
- Menu strings, binds, state variables, and feature entry points.
- Conflicts with the MEM1 reservation, post-draw barrier, room loader, and
  Nintendont/MEM2 handoff.

Treat that recovery as a compatibility inventory, not merge-ready source.
Integrate features individually after their ownership and lifetime are known;
never layer an opaque binary patch over the savestate payload.

The shipped user flow must remain Moonshine-style: select a clean GLMJ01 disc
or image in the custom Nintendont launcher and inject a separately packaged,
authenticated payload at boot. The GaddWarp xdelta is analysis input only; it
must not become an installation prerequisite or release artifact.

The supplied JP 2.2 patch has now been compared against retail. See
[the compatibility inventory](gaddwarp-jp2.2-inventory.md) for its verified
disc-file ownership, injected executable ranges, and collision risks.
