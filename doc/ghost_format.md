# Ghost file format

Status: V4 format. V3 ghosts remain readable with their original byte
semantics; V1/V2 test ghosts are intentionally refused. V4 retains V3's
bounded route segments and records attachment state without increasing the
sample stride, file limit, or runtime buffers.

## Scope

`SGHF` is the canonical, path-independent ghost object. It contains one raw
pose track and enough metadata to reject the wrong game, route, or decoder.
V3/V4 partition one absolute-QFT track into up to 64 ordered route segments; it
does not reset time at a stage load. It contains no console slot number,
storage generation, or filename.

Console storage is a separate layer. It uses fixed A/B slot envelopes below
`/susamune_ghosts/<region>/p<profile>`. The mailbox contract is declared by
`include/susamune/ghost_storage.h`; the kernel owns the envelope and fixed-path
implementation. It derives every path from validated numeric region, profile,
slot, and A/B bank values. It never uses imported display text or a portable
filename as a path component.

`SGIX` is an optional catalog for a future portable host import/export bundle.
The console does not read it, enumerate it, or use it as its source of truth.

All multibyte values in both formats are big-endian. Signed fields use two's
complement. Readers must decode fields from bytes and validate them before
constructing runtime state; casting an untrusted file to the C structs is not a
validation strategy.

The shared constants and layout declarations are in
`include/susamune/ghost_format.h`. `scripts/validate_ghost.py` is the independent
host validator.

## Canonical ghost header (`SGHF`)

The V4 header is exactly `0x100` bytes. Fields through `0xcf` retain V3's
offsets.

| Offset | Size | Field | V4 rule |
|---:|---:|---|---|
| `0x00` | 4 | `magic` | ASCII `SGHF` |
| `0x04` | 2 | `version` | `4` |
| `0x06` | 2 | `headerSize` | `0x100` |
| `0x08` | 4 | `fileSize` | Exact byte length; max `0x69ee0` |
| `0x0c` | 4 | `fileChecksum` | CRC-32 of the complete file |
| `0x10` | 4 | `headerChecksum` | CRC-32 of the header |
| `0x14` | 4 | `payloadChecksum` | CRC-32 of the sample payload |
| `0x18` | 4 | `requiredFeatures` | Extended-codec bit `0x00000001` |
| `0x1c` | 4 | `runFlags` | Advisory eligibility metadata |
| `0x20` | 4 | `gameId` | `GMSJ`, `GMSE`, or `GMSP` |
| `0x24` | 1 | `discRevision` | `0` |
| `0x25` | 1 | `region` | JP `0`, US `1`, PAL `2`; must match `gameId` |
| `0x26` | 1 | `sourceProfile` | `0` through `3` |
| `0x27` | 1 | `recordingMode` | Raw pose/QFT mode `2` |
| `0x28` | 1 | `sampleCodec` | Pose/attachment codec `1` |
| `0x29` | 1 | `sampleStride` | `16` |
| `0x2a` | 2 | `sampleIntervalQf` | `4` |
| `0x2c` | 1 | `routeArea` | Game area ID, at most `0x3c`; segment zero |
| `0x2d` | 1 | `routeEpisode` | Episode `0` through `9` |
| `0x2e` | 1 | `routeParentArea` | Parent area, or `0xff` when absent |
| `0x2f` | 1 | `routeFlags` | Only bits `0x01` and `0x02` |
| `0x30` | 4 | `routeVariant` | `-1` when absent, otherwise `0` through `255` |
| `0x34` | 4 | `resultQf` | Absolute QFT result, or `0xffffffff` |
| `0x38` | 4 | `startQf` | Absolute QFT at the first segment start |
| `0x3c` | 4 | `endQf` | Absolute QFT at the final segment end |
| `0x40` | 4 | `durationQf` | `endQf - startQf`, at most `107892` |
| `0x44` | 4 | `sampleCount` | `2` through `26974` |
| `0x48` | 4 | `payloadSize` | Fixed segment table plus pose samples |
| `0x4c` | 8 | `createdUnixHi/Lo` | Optional unsigned Unix time; zero if unknown |
| `0x54` | 8 | `ghostIdHi/Lo` | Opaque nonzero ID |
| `0x5c` | 1 | `authorLength` | `0` through `24` |
| `0x5d` | 1 | `nameLength` | `1` through `48` |
| `0x5e` | 1 | `profileNameLength` | `0` through `16` |
| `0x5f` | 1 | `checksumKind` | CRC-32 kind `1` |
| `0x60` | 24 | `author` | Length-delimited display text |
| `0x78` | 48 | `name` | Length-delimited display text |
| `0xa8` | 16 | `profileName` | Length-delimited display text |
| `0xb8` | 72 | extension | V4 segment/attachment layout below |

`startQf` and `endQf` are bounded to `0x7fffffff`, matching the signed runtime
clock. `endQf` must not precede `startQf`. A present `resultQf` must lie inside
the inclusive start/end interval. Storage quotas use `durationQf`, not the
absolute clock values or the result.

`routeVariant` preserves `TFlagManager` flag `0x40003`, the parent-episode or
scenario selector that distinguishes internal routes sharing an area ID.
`routeParentArea` records the logical parent when it can be resolved. Route flag
`0x01` marks an internal scene and must be set exactly when `routeParentArea` is
not `0xff`. Flag `0x02` marks parent-start route identity and is invalid without
that parent area. V3/V4 validate this tuple structurally but deliberately do not
embed a game-specific child-to-parent lookup table; custom parent mappings stay
valid within the same area and variant bounds. Unknown route flags are invalid
instead of being guessed.

The display fields contain printable ASCII bytes `0x20` through `0x7e`.
Slash and backslash are rejected even though these strings must never become
paths. Bytes after each declared length must be zero. This deliberately avoids
an ARM-side Unicode decoder and region-font differences; UTF-8 would require a
later version or required feature.

## V4 segment and attachment extension

V4 gives the header's final 72 bytes this exact layout:

| Offset | Size | Field | V4 rule |
|---:|---:|---|---|
| `0xb8` | 2 | `segmentCount` | `1` through `64` |
| `0xba` | 2 | `segmentSize` | `0x20` |
| `0xbc` | 4 | `segmentTableOffset` | `0x100` |
| `0xc0` | 4 | `segmentTableSize` | `0x800` |
| `0xc4` | 4 | `sampleDataOffset` | `0x900` |
| `0xc8` | 4 | `sampleDataSize` | `sampleCount * 16` |
| `0xcc` | 4 | `segmentTableChecksum` | CRC-32 of the complete `0x800` table |
| `0xd0` | 1 | `attachmentCount` | `0` through `7` |
| `0xd1` | 1 | `attachmentSize` | `6` |
| `0xd2` | 2 | `attachmentFlags` | Only held-overflow bit `0x0001` |
| `0xd4` | 2 | reserved | Zero |
| `0xd6` | 42 | attachment descriptors | Seven fixed six-byte rows |

Each active attachment descriptor is a big-endian `u32 mObjectID` followed by
the actor's big-endian `u16 JDrama::TNameRef::mKeyCode`. These values are
address-free source-game identifiers. They are interpreted together with the
header's source region; no vtable, pointer, or code address enters the file.
Active descriptors must be nonzero and unique. Rows after `attachmentCount`
must be zero.

The held-overflow flag permits held index `15` and records that the writer
encountered an unrepresentable identity. This can happen after seven distinct
identities or when the live actor exposes the unidentifiable all-zero pair.
It may remain set if a later end-QF trim removes the relevant sample; overflow
never fails or stops a recording.

The segment table always reserves 64 descriptors. Descriptors after
`segmentCount` are zero. This fixed 2 KiB cost keeps every offset bounded and
makes a future file impossible to reinterpret through a forged variable
offset. Each active descriptor is 32 bytes:

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | first sample index |
| `0x04` | 4 | sample count, at least one |
| `0x08` | 4 | absolute segment start QF |
| `0x0c` | 4 | absolute segment end QF |
| `0x10` | 4 | signed route variant |
| `0x14` | 4 | area, episode, parent area, route flags |
| `0x18` | 8 | reserved, all zero |

Descriptors cover the sample array contiguously and exactly: descriptor zero
starts at sample zero and each later `firstSample` equals the preceding end.
Every QF range is inclusive, bounded to `0x7fffffff`, and ordered so the next
start is at least the previous end. Gaps are valid and represent loading or a
transition with no observable Mario pose. Overlap and time regression are
invalid.

The first descriptor's route and start QF equal the common header route and
`startQf`; the final descriptor ends at header `endQf`. Header `durationQf` is
the global `endQf - startQf`, including transition gaps. It is deliberately not
the sum of segment spans. A present result belongs to the final segment. This
one absolute timeline lets playback wait when live QFT is early or seek into a
saved segment when live QFT is late.

Writers always emit the fixed table, including for a one-segment route. This
keeps one canonical offset. V3 uses the same segment fields and offsets but
requires all 48 bytes after the segment-table checksum to be zero.

## Pose payload

The `0x800` segment table occupies `0x100-0x900`, and V4 sample data begins at
`0x900`. Every sample is exactly 16 bytes:

| Offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0x00` | 2 | `s16` | Game-native Mario yaw |
| `0x02` | 2 | `u16` | QFT delta |
| `0x04` | 3 | signed BE24 | X position multiplied by 8 |
| `0x07` | 3 | signed BE24 | Y position multiplied by 8 |
| `0x0a` | 3 | signed BE24 | Z position multiplied by 8 |
| `0x0d` | 3 | BE24 | Animation ID 9, phase 8, Yoshi state 3, held index 4 |

The first sample of every segment has delta zero. Later deltas are at
least four QF, except that each segment's terminal sample may use one through
three. Deltas within one segment sum exactly to that descriptor's
`endQf - startQf`. A one-sample segment is valid only with equal start/end QF.
No delta spans the unobserved gap between descriptors.

This normalizes the runtime's absolute first timestamp while retaining exact
QFT alignment in `startQf`. Writers must emit the final transform at `endQf`;
readers do not extrapolate a missing tail.

Fixed positions are limited to `-8000000` through `8000000`, equivalent to
plus or minus 1,000,000 game units. V3/V4 allow cadence gaps, including lag, as
long as an individual `u16` delta can represent the gap. A ghost is a visual
reference, not a deterministic simulation; the format makes no promise about
RNG, objects, or physics state.

The animation field is `0..335`. When the Yoshi field is zero it is the shared
`gMarioAnimeData` logical index. Mounted samples instead store the direct
retail rider BCK ID in `0xB6..0xC6`; retail bypasses `gMarioAnimeData` for
these poses.

V4's phase is normalized to `0..255`; the runtime expands it to the renderer's
`0..4095` phase domain. Equal-ID samples interpolate phase along the shortest
modular path; an ID change steps at its recorded four-QF boundary. JP/US/PAL
retail animation maps were verified byte-identical, while the file remains
independent of region-specific animation pointers.

The Yoshi field is zero when not mounted, `1..4` for green, orange, purple,
and pink, and `5` for mounted with an unknown color. Values `6..7` are invalid.
The held index is zero for no held actor, `1..7` for the corresponding header
descriptor, or `15` for held-but-unknown when the overflow flag is set. Values
`8..14`, an index beyond `attachmentCount`, and `15` without the flag are
invalid.

The current renderer supports Yoshi's base-body model, the five retail
`TResetFruit` models (`0x40000390..0x40000394`), and the exact retail
`TJumpBase` spring (`0x40000017`). Unknown or unproved held actors stay hidden.
It does not record Mario's motion blend, upper-body/FLUDD blend, detailed Yoshi
pose or separate hands/tongue, held-object animation state, hand/accessory
selection, or goop/material state.

The maximum sample data is `26974 * 16 = 431584` bytes (`0x695e0`). V4 adds
the fixed `0x800` table and header for a maximum 433888 bytes (`0x69ee0`),
still below the `0x7ff00` transfer payload. V4 itself sets
`SUSAMUNE_GHOST_REQUIRED_EXTENDED_CODEC` and codec `1`; those values are an
exact pair rather than advisory metadata.

## V3 compatibility

V3 remains a strict read format. It requires version `3`, required features
zero, raw codec `0`, a zero 48-byte extension tail, and the original sample
trailer of animation ID 9, normalized phase 12, and three zero low bits. A V4
reader selects this decoder by version and never treats those validated-zero
bits as attachment data. V4 writers always emit version `4`; a released
V3-only reader sees the newer version before parsing its body and refuses it
as unsupported, preserving the slot instead of corrupting or replacing it.

## Checksums

All checksums use reflected CRC-32/ISO-HDLC:

- polynomial `0xedb88320`;
- initial value `0xffffffff`;
- reflected input and output;
- final XOR `0xffffffff`.

This is the CRC returned by zlib's `crc32` interface.

Coverage is exact:

- `payloadChecksum`: bytes `[headerSize, fileSize)` with no substitutions.
- `headerChecksum`: the `0x100`-byte header with bytes `[0x0c, 0x14)`
  (`fileChecksum` and `headerChecksum`) replaced by zero.
- `fileChecksum`: bytes `[0, fileSize)` with bytes `[0x0c, 0x10)`
  (`fileChecksum` only) replaced by zero. It includes the stored header and
  payload checksums.

A writer calculates the payload checksum, then the header checksum, then the
file checksum. A checksum mismatch is corruption, not an unsupported version.

## Version and corruption behavior

A reader first recognizes the magic and version from the bounded eight-byte
prefix. A recognized newer version or unknown required feature is refused as
unsupported. Console storage must mark that slot unsafe/read-only so an older
kernel cannot replace data it does not understand.

A supported version with impossible sizes, mismatched metadata, invalid text,
unknown unflagged codec values, nonzero reserved bytes, or bad checksums is
corrupt. It is isolated to its slot and never partially activated. LOAD decodes
into an inactive buffer and changes the active ghost only after the complete
file passes validation.

Personal-library ghosts remain region-local: `gameId`, `region`, disc revision,
and source profile must match the running build and selected PB profile.

The global imported pool may use an unmodified revision-zero JP, US, or PAL
file on another region. This is visual pose-and-animation playback, not format
conversion: the source tuple and all metadata remain immutable. A foreign file
is accepted only when its header route, and every SGHF V3/V4 segment route, has a
shared retail meaning. The portable route policy is:

- standalone areas `00, 01, 02, 03, 04, 05, 06, 08, 09, 14, 15, 16, 17, 18,
  1D, 34, 3C`, with parent `FF` and route flags exactly zero;
- child-parent pairs `07->06, 0D->05, 0E->06, 10->09, 1E->03, 1F->09,
  20->04, 21->04, 28->06, 29->05, 2A->08, 2C->09, 2E->02, 2F->02,
  30->03, 32->05, 33->06, 37->02, 38->06, 39->09, 3A->05, 3B->03`.

A listed child must carry `INTERNAL_SCENE`, may also carry `PARENT_START`, and
must carry no other route flag. Episode and variant retain the ordinary
bounded format rules. Any unlisted or mismatched route fails closed. This
policy is shared by the ARM catalog/load validator and PPC race activation.

## Bounded share-file round trip

Received files are placed, without a rename step, in one user-facing directory
on the launcher's storage device:

`/susamune_ghosts/import/`

The filename is not trusted. A candidate leaf must be at most 95 bytes, consist
only of printable ASCII, end case-insensitively in `.smsghost`, contain none of
`"*/:<>?\|`, and not end in a dot or space. Only the sanitized leaf is cached,
and paths are always rebuilt beneath the fixed import root.

The ARM scans one directory entry or one bounded file operation per DI-idle
service pass. It checks the header and the complete SGHF V3/V4 fixed segment
table before cataloguing a candidate. Compatible candidates are ordered by an
ASCII case-insensitive lexical comparison with exact-ASCII tie-breaking. The
lexicographically smallest 12 become global imported rows `0..11`; this is
stable regardless of FAT directory order. A bounded overflow count reports how
many additional compatible candidates were found. LOAD opens the cached leaf
again and revalidates the complete file, including payload and file CRCs,
before changing playback.

If a supported-version file changes after prefix scanning and fails full LOAD
payload or CRC validation, its cached sanitized leaf remains as a visible
unsafe imported row. It cannot be loaded, but explicit DELETE may unlink that
one quarantined source. This keeps the published row count consistent and
retains an in-game way to remove a payload-corrupt file. A forward-version
result instead invalidates the catalog; the mandatory rescan omits it, so an
older build cannot delete it.

EXPORT reads and fully validates the selected A/B library generation, then
writes only canonical SGHF bytes to a friendly deterministic leaf:

`YYYY_MM_DD_<route>_<time>[<CRC>].smsghost`

For example, `2026_08_15_BH4_42490[89ABCDEF].smsghost` is a Bianco 4
export whose compact duration is 42.490 seconds. The date comes from FatFS at
export time. Main courses use bounded abbreviations (`BH`, `RH`, `GB`, `PP`,
`SB`, `PV`, and `NB`, plus `AS`, `DP`, `CM`, and `BW` where applicable) and a
one-based episode. An internal start uses the validated parent area and route
variant when available. Other routes use uppercase raw `AxxEyy`.

The compact millisecond value is the exact floor
`durationQf * 1001 / 120`: `42.490` becomes `42490`, and `1:02.345` becomes
`102345`. Arithmetic widens before multiplying. The bracketed suffix is the
stored canonical file CRC as eight uppercase hex digits. No display name,
author, or external filename contributes text to the path.

EXPORT uses create-new semantics: it never truncates or overwrites an existing
leaf. For a new leaf it writes a zero header, writes the remainder in at most
16 KiB per DI-idle pass, syncs it, and commits the real `0x100` header last. An
existing deterministic name is reported as already existing; this prototype
does not compare the existing bytes or call the operation successful. An
interrupted new export can leave an invalid partial leaf for the user to
remove, but cannot damage an earlier export or the A/B library. Friendly
exports remain unmanaged copies outside the journal quota. A different date
produces another file; this build does not enumerate or prune export copies.
Full or write-protected media reports an error without changing a library
generation or catalog row.

Imported ghosts remain canonical files in the import directory. They are not
copied into a PB profile or rewritten into an A/B envelope, and they are
visible whichever PB profile is active. Loading or racing does not consume a
personal slot. Explicit imported DELETE unlinks the selected source leaf; no
automatic import, pruning, rename, or replacement occurs.

## Quotas and storage cost

The personal-library quota namespace is `(running game region, PB profile)`.
Each namespace must satisfy both limits:

- 45 writable rows (`0..44`) for new saves;
- at most `4315684` aggregate `durationQf`, exactly ten hours at
  `120000/1001` QF per second.

Rows `45..47` are outside the PR1 namespace. The 48-row wire catalog retains
three zero tail rows for ABI convenience, but the ARM does not scan the old
files, they do not contribute count or duration, and LOAD/EXPORT/DELETE rejects
those indices. The old Gate ghost library must be cleared before PR1; orphaned
tail files are otherwise ignored. Every V4 save refuses an occupied row;
there is no overwrite request in the mailbox ABI or UI. Across four profiles
this reserves 12 rows' worth of policy budget for the single global imported
pool.
The import pool has at most 12 visible files and therefore at most three hours
at the per-file 15-minute cap; it does not consume the personal ten-hour quota.

Forty full 15-minute ghosts consume `4315680` QF, leaving only four QF; a 41st
full ghost fails the time quota. Forty-five shorter new ghosts may instead
reach the writable-row limit. SAVE computes the proposed total with checked
arithmetic.

At the four-QF sampling interval, raw animated poses cost about 479.52 bytes/s,
28.77 kB/min (28.10 KiB/min), or 1.73 MB/hour (1.65 MiB/hour). The 45-row
namespace plus the ten-hour cap is smaller than the historical bound. For a
conservative SD estimate, the former 48-file V1 bound was 17276368 bytes and
the fixed tables retained by V3/V4 raise it to 17374672 bytes (16.57 MiB).

Console A/B recovery can temporarily retain twice that V3/V4 amount: 34749344
bytes per profile, 138997376 bytes per region, and 416992128 bytes across all
three regions (approximately 33.14, 132.56, and 397.67 MiB). These remain safe
upper bounds even though rows `45..47` are no longer scanned. Twelve
maximum-size V3/V4 import files total 5206656 bytes (4.97 MiB); extra files may
remain in the user-managed directory but are not visible until their lexical
predecessors are removed. These are SD-card bounds. Runtime MEM2 remains a
fixed record buffer, playback buffer, and transfer buffer; it does not scale
with the library.

## Portable bundle index (`SGIX`)

SGIX is reserved for a future host-side export bundle. Its generation and
catalog validation are useful for host tooling, but it is not the console A/B
slot envelope or an on-console index.

The header is `0x80` bytes:

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | `SGIX` magic |
| `0x04` | 2 | version `1` |
| `0x06` | 2 | header size `0x80` |
| `0x08` | 2 | entry size `0x80` |
| `0x0a` | 2 | entry count, at most 48 |
| `0x0c` | 4 | exact file size |
| `0x10` | 4 | wrapping generation |
| `0x14` | 4 | file CRC, with this field zeroed |
| `0x18` | 4 | CRC of all entry bytes |
| `0x1c` | 4 | game ID |
| `0x20` | 4 | region, profile, max entries `48`, flags `0` |
| `0x24` | 4 | cached total duration |
| `0x28` | 4 | quota duration `4315684` |
| `0x2c` | 8 | creation Unix time, high word then low word |
| `0x34` | 1 | profile-name length |
| `0x35` | 1 | checksum kind `1` |
| `0x36` | 2 | zero |
| `0x38` | 16 | profile name |
| `0x48` | 56 | zero |

Each `0x80`-byte entry contains, in order: the 64-bit ghost ID; file size and
CRC; duration, result, start and end QF; sample count; creation time; route
variant; four route bytes; name and author lengths; 16-bit entry flags; a
48-byte name; and a 24-byte author. The only SGIX V1 entry flags are pinned
`0x0001` and autosaved `0x0002`. Cached file size requires the V3/V4 fixed-table
shape; bundle validation then requires an exact match with the referenced SGHF.

A portable exporter may name a ghost `g_<16 lower-case hex digits>.smsghost` using
the opaque ID, after checking for ID collisions. That convention is host-only.
Display names never become filenames. A bundle reader validates every file and
all cached entry fields before accepting the catalog.

Wrapping generation comparison uses the half-range rule: candidate `a` is
newer than `b` when `(a - b) mod 2^32` is nonzero and less than `2^31`. Exactly
`2^31` apart is ambiguous and refused. A corrupt newer copy may fall back to a
valid older copy, but the presence of a recognized forward-version copy blocks
fallback and overwrite.

## Host validation

Validate one or more canonical files:

```text
python scripts/validate_ghost.py ghost.smsghost
```

Validate SGIX plus every host-export file it references:

```text
python scripts/validate_ghost.py export.sgi --bundle-dir exported_ghosts
```

Run the format tests:

```text
python scripts/test_ghost_format.py
```

The validator returns exit code 0 for valid input, 2 for corruption or policy
violations, and 3 for a recognized unsupported version or feature.
