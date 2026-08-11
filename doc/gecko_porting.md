# Porting SMS practice gecko codes into the mod

This is the working guide for turning the Super Mario Sunshine practice
"gecko" codes into native mod features. The end goal (see `doc/gecko_codes.md`
for the wishlist) is that everything a runner currently loads as a `.gct` of
gecko codes becomes a first-class, menu-toggleable part of susamune, correct on
all three supported revisions.

## Sources of truth

- **`../../src/gct-generator/Codes.xml`** — the upstream code database. Each
  `<code>` has a human `<description>` (what it does, and any button binds) and
  one `<source version="…">` block per game revision holding the raw gecko
  lines. This is the format the practice-code website compiles per region.
- **`../../src/gct-generator/ram_map.xlsx`** — a documented SMS RAM map. More
  detailed than the linker map for struct layouts (e.g. the `TFlagManager`
  flag struct). Use it when a code touches a field whose meaning isn't obvious.
- **`../../src/sms`** — the decomp. Canonical for any type layout / code path
  you need to confirm while reversing an asm code.

### Revisions

The XML carries four columns; the mod supports **three**:

| XML `version` | mod `VERS` | notes |
|---|---|---|
| `GMSJ01` | `jp` | |
| `GMSE01` | `us` | |
| `GMSP01` | `pal` | |
| `GMSJ0A` | — | JP revision A; **ignore**, not a supported target |

Every game address in the mod is written through
`SUSAMUNE_MEM1_ADDR(jp, us, pal)` (`include/susamune/addresses.hxx`), which
selects the right column at compile time from the `SUSAMUNE_VERSION_*` define
CMake sets. So a ported code is one table row carrying all three addresses.

## Gecko code types you'll meet

A gecko line is two 32-bit words: `TTAAAAAA VVVVVVVV`. `TT` (top bits of the
first word) is the type; the address operand is `0x80000000 | 0xAAAAAA`.

| Encoding | Meaning | Port as |
|---|---|---|
| `04AAAAAA VVVVVVVV` | write 32-bit `V` at `0x80AAAAAA` | whole-word patch |
| `02AAAAAA 0000VVVV` | write 16-bit `V` at `0x80AAAAAA` | masked (half-word) patch |
| `00AAAAAA 000000VV` | write 8-bit `V` | masked (byte) patch |
| `06AAAAAA NNNNNNNN` + data | write an `N`-byte blob (a "code cave") | see Class B |
| `C6AAAAAA TTTTTTTT` | insert a **branch** at `0x80AAAAAA` to `0xTTTTTTTT` | whole-word patch, value = encoded `b` |
| `C2AAAAAA NNNNNNNN` + asm | **insert assembly**: branch from the site into `N` lines of PPC asm hosted by the code handler, then back | Class B (trampoline) |
| `C0…` | execute assembly once per pass (not injected at a site) | Class B |
| `28/2A/…`, `80/82/86/8A`, `E0000000` | the gecko VM: conditionals, gecko-register loads/stores, block terminators | used by the multi-feature codes (DPad, Nozzle Lock); reverse the *effect*, see Class C |

### Decoding a `C6` branch

`C6` writes a plain `b` (no link). The word to write is
`0x48000000 | ((target - addr) & 0x03FFFFFC)`. Because the practice codes'
branch *distances* are usually identical across revisions (only the absolute
addresses differ), the encoded branch word is the same constant in all three
columns — e.g. "Enable Exit Area Everywhere" is `b +0xC` = `0x4800000C`
everywhere; only the address to write it at changes.

## Prefer reimplementing the behaviour in C

**The mechanisms below reproduce a gecko code's *bytes*; that is a convenience
for simple codes, not the goal.** Replaying someone else's compiled
instructions is opaque, hard to review, and has to be re-derived per revision.
Once a code is more than a couple of writes, the better port is to understand
what it does and write the equivalent **C in the mod**, against the decomp's
types and the mod's own hooks.

Rule of thumb:

- A handful of constant writes → a stream row (Class A/C). Cheap and clear.
- Short logic that depends on the intercepted register state → a compact
  Class-B hook.
- Anything with substantial control flow, state, or ordering → **reimplement
  in C** when practical. If exact hook semantics are necessary, keep the cave
  small and document its behavioral invariants.

Reimplementing needs the *semantics*, which come from `../../src/sms`, not from
staring at hex. Resolve the code's addresses to functions via
`maps/<vers>.map`, read those functions in the decomp, and then express the
intent. See "Behavior-sensitive hook ports" below for two worked examples.

### Never infer behaviour from a name

Every bug in the first pass of the stateful codes came from trusting a label:

- `APP_STATE_MENU` is the **debug** menu, not the title screen.
- `APP_STATE_TITLE` builds the **file-select** screen, not the title screen.
  (The title screen is `APP_STATE_GAMEPLAY` running `AREA_OPTION` as a stage.)
- "No Shine Get **Animation**" does not remove the animation — the animation is
  the point; what it removes is banking the shine and ending the run.

Names in this codebase come from three different reverse-engineering efforts
(susamune's `Context`, the decomp's `APP_STATE_*`, and the upstream code
titles) and none of them agree. Confirm against the function that actually
does the work — for a state machine, the transition function; for a feature,
the code's `<description>` plus the decomp. If a port looks far simpler than
the gecko original, that is evidence the behaviour has been misread, not that
the original is overbuilt.

## Implementation classes

1. **Class A — static memory / instruction writes** (`04`/`02`/`00`/`C6`, and
   `06` data writes to a fixed datum). The code just overwrites game memory
   with a constant. Handled by the flat `gFeaturePatches[]` stream in
   `features.cpp`.
2. **Class B — asm injections** (`C2`/`C0`, and `06` code-caves that are jumped
   into). The logic lives in asm that runs with a hooked function's register
   state. Implemented via `gAsmCaves[]` / `kAsmHooks` in `features.cpp` (reproduce the asm
   verbatim in a mod cave; toggle a branch at the site — see "Class B: asm
   hooks" below). Use this only when the asm is short and opaque; prefer C.
3. **Bind-driven actions** — codes that *do* something when a button
   combination is pressed rather than toggling state. The behaviour is written
   as C in `actions.cpp`; the combo comes from a configurable bind
   (`binds.*`) instead of the fixed one the gecko hardcodes. See "Bind-driven
   actions" below.
4. **Class C — multi-state options** carved out of a big multi-feature code
   (DPad Functions, Nozzle Lock). These use the gecko VM to store a mode byte
   and apply different writes per mode. Implemented by the specialized choice
   sites and plain patch ranges in `features.cpp` (Nozzle Lock,
   FLUDD-in-secrets, Fast Text) —
   see "Class C: multi-state options" below.
5. **Native or hybrid stateful ports** — behaviour is expressed in C where
   possible, with a short hook only where the intercepted register state is
   essential. See "Behavior-sensitive hook ports" below.

## Class A: the `features.cpp` mechanism

`src/features.cpp` holds one mutable flat `gFeaturePatches[]` stream. A patch
is a two-word masked write:

```c
struct Patch { u32 addrState; u32 value; };
```

Every site is in `0x80xxxxxx` MEM1, so the otherwise-fixed high address byte
stores metadata and runtime state. `FBEGIN` starts a feature range and records
its `SettingId`; following `FWORD`, `FHALFHI`, `FHEAP`, or `FHEAPLO` rows belong
to that range until the next begin row. The low two address bits select a
high- or low-halfword write; an unflagged row replaces the full word.

`featuresApply()` runs every frame from `onUpdate`, but writes only when the
setting transitions. Each non-early row lazily captures its live original into
`gPatchOrig[]`; capture/on bits share `addrState`. "Off" therefore restores the
word actually shipped by the running revision, while steady state does not
read or rewrite game memory. `writeGameCode()` flushes and invalidates the four
bytes whenever a transition changes an instruction. An unresolved PAL Fast
Text row still claims its stable original slot while its address is zero, then
captures normally after the language-specific address resolves.

This pass reaches back further than "gameplay" — `proc()` runs `gameLoop()` for
the logo and title app states as well — but not further back than
`director->direct()`, which `onUpdate` calls *before* `featuresApply()`. A
feature whose sites are already directing by then needs the patch installed
sooner. `FBEGIN_EARLY(...)` starts such a range and `featuresApplyEarly()` — called
from `onAppInit`, the last point before `TApplication::proc()` starts the
app-state machine — writes **only** those rows. Intro Skip is the one today,
and on the per-frame pass alone the logos still play.

Early rows are **write-once**: `featuresApply()` skips them, they hold no slot
in the captured-original / installed-state arrays, and nothing ever restores
them. That is not a shortcut — restoring is meaningless for a site that has
already run by the time the menu can be opened, and a reboot reloads the game's
own code from disc anyway. So turning such a setting off applies at the next
boot, not immediately. Do not widen `FBEGIN_EARLY` to the whole table: rows that
patch heap words (Fast Text) would capture their "original" out of heap the
game has not filled in yet, and write that garbage back when toggled off.

### Adding a Class-A code

1. **Find the code** in `Codes.xml`; read its `<description>` so you know the
   intended effect (and confirm it against the decomp if the asm is doing
   something subtle).
2. **Transcribe the three source columns** (`GMSJ01`, `GMSE01`, `GMSP01`).
   Ignore `GMSJ0A`. For each gecko line choose the stream row:
   - first `04AAAAAA V` → `FBEGIN(SETTING_…, 0x80<jp>, 0x80<us>, 0x80<pal>, 0xV)`
   - later `04AAAAAA V` → `FWORD(0x80<jp>, 0x80<us>, 0x80<pal>, 0xV)`
   - `02AAAAAA 0000V` at a word-aligned address → `FHALFHI(…, 0xV0000)`
   - `C6AAAAAA T` → `FWORD(…, encodeBranch(addr,target))` (same constant across
     regions when the distances match — verify).
   Use `FHEAP`/`FHEAPLO` instead for a hardcoded bottom-anchored heap address.
3. **Add the contiguous range** to `gFeaturePatches`, commenting the raw gecko
   lines and what each patched word is (opcode mnemonic helps future readers).
4. **Add a `SettingId`** by appending a row to `SUSAMUNE_SETTING_LIST`
   (`settings_list.h` — the list is shared with the launcher and is
   **append-only**, since its order is the persisted layout) and a row
   at the same ordinal in `src/settings_descs.inc` — name, default `0` (Off),
   and the `SETTING_CAT_*` for its tab.
5. Update the regional stream-shape assertions and `kNumEarlyPatches` if the
   new range is early, then build all three regions and test the transitions.

The high-byte tag has five bits and reserves zero for continuation rows, so a
Class-A feature's `SettingId` must be `0..30`. A later appended setting will not
fit without redesigning the encoding; the template assertion fails rather
than silently truncating it.

### Menu wiring

Settings render generically. The packed descriptor carries a `SettingCategory`;
the menu builds one `CategorySettingsTab` per category (`menu.cpp`, wired in
`Menu::Menu()`), each of which filters via `Settings::category()` and
renders/edits the matching rows (scrolling if they overflow). So a new toggle
in an existing category needs **no menu change** — just the settings row. A new
category means a new enum value, title-pool offset, static buffer and
`CategorySettingsTab` construction in `Menu::Menu()`.

Current generic tabs are `QoL`, `Cosmetic`, `Misc`, `Savestate`, `UI`, and
`Timer`. Bespoke tabs are `Creation`, `ILs`, and `Binds`; `Warps` and `Stages`
are prepended when `ENABLE_DEBUG_WARPS` is enabled. Creation contains QFT,
Input Display, and Metadata sections.

## Class B: asm hooks (`C2` codes)

A `C2AAAAAA NNNNNNNN` code inserts `N` lines of asm at site `0x80AAAAAA`. The
code handler (`launcher/codehandler/codehandler.s`, `_hook1`) does exactly two
writes:

1. at the site, `b site -> asm_block` (over the original instruction), and
2. it **overwrites the last word of the asm block** — the trailing `00000000`
   placeholder every `C2` ends with — with `b -> site+4`.

So the asm as authored already contains any displaced original instruction and
whatever register handling it needs; the handler just runs it and returns to
`site+4`. **We don't need to understand the asm** — we reproduce it byte-for-byte.

`gAsmCaves[]` and `kAsmHooks` in `features.cpp` do this toggleably:

- The mutable cave slices tile one initialized `gAsmCaves[]` pool in hook
  order. Offset enum values name every boundary; each descriptor stores the
  game site, a direct pointer to its slice, the slice length, and a byte whose
  low seven bits are the `SettingId` and high bit is current-on state. Executed
  Gecko instructions are preserved; a trailing padding NOP immediately before
  the return placeholder may be omitted only after proving no internal branch
  targets it.
- On the first pass, every trailing `00000000` becomes `b -> site+4`, each cave
  is flushed/invalidated, and each live site word is captured. One global flag
  guards that initialization. Enabling writes `b site -> cave`; disabling
  restores the captured original. Later passes write only on setting
  transitions, through `writeGameCode()`.
- The mod is linked in MEM1 within ±32 MB of game code, so both branches are
  reachable.
- **Region-specific asm:** where a `C2`'s asm embeds an address (an sda
  `r13`-relative load, or a `lis`/`ori` pair), build those words with
  `SUSAMUNE_MEM1_ADDR(...)` so one pool serves all three revisions — see the
  Free Pause and Deathless slices.
- **Companion static writes:** several of these gecko codes are a `C2` *plus*
  some plain `04`/`C6` lines. Those go in a flat patch range under the *same*
  `SettingId`, so the whole feature toggles together.

### Adding a Class-B (`C2`) code

1. Append the asm words to `gAsmCaves[]`, **including** the trailing
   `00000000` placeholder, and add its begin/end boundaries to the offset enum.
   Bake region-specific words via `SUSAMUNE_MEM1_ADDR`.
2. Add a `HOOK(SETTING_FOO, <jp>, <us>, <pal>, begin, end)` row. Hook setting
   IDs must remain below 128 because bit 7 stores current-on state.
3. If the code has non-`C2` lines, add a flat patch range under the same
   `SETTING_FOO`.
4. Add the setting-list and matching `src/settings_descs.inc` rows, update the
   pool/count assertions, and build all three regions.

## Class C: multi-state options

Some gecko codes bundle several features behind a runtime **mode byte**: the
code reads controller input, ORs mode bits into a scratch address, and each
frame applies a different set of writes per mode bit via gecko conditionals
(`28…`/`E0…`). We don't reproduce the state machine — we expose each wanted
sub-feature as its own setting and apply its word set directly.

The current fixed-shape choice writer captures all three live originals once
and packs capture/current-choice state into one byte. By convention **choice 0
is the game default and restores the captured original**, so its label must be
the normal-behaviour state. Higher choices synthesize the exact instruction:
- Nozzle Lock (4-state: Unlocked/Rocket/Turbo/Hover) is one site forced to
  `li r31,<id>`. FLUDD-in-secrets (3-state: Completed/No FLUDD/All secrets) is
  two `TRedCoinSwitch::load` sites.
- A sub-feature that is just on/off (Fast Text) is a plain BOOL patch range —
  On writes the forced words, Off restores the original.

### ⚠️ Identify DPad sub-features by address, not by condition order

The DPad Functions code's mode-bit order does **not** line up with the feature
list in its description, and several sub-features touch unrelated-looking
addresses. **Resolve every site address in `maps/<vers>.map` before deciding
what a block does.** For DPad Functions that showed:

- the `8A`/`8C` gecko-register blocks load `gpMarioPos` / `gpCamera` → the
  **position save/load** feature (belongs with savestates, not a toggle);
- the `0x004`/`0x008` block writes into `Talk2D2` / `EventWatcher` → **Fast
  Text** (dialog), *not* FLUDD as the condition order first suggested;
- the `0x401`/`0x402`/`0x404` block writes into `TRedCoinSwitch::load` → the
  actual **FLUDD-in-secrets** 3-state.

Getting this from the raw gecko alone is a trap; the map is authoritative.

## Behavior-sensitive hook ports

These two ports retain compact asm hooks because their behavior depends on the
intercepted register state. They are worth reading as templates for cases where
a plain patch stream or a pure C callback cannot preserve the original timing.

> **Intro Skip is a Class-A `FBEGIN_EARLY` range, not one of these stage
> hooks.** An
> earlier hand-written port was wrong in a way worth remembering: it repointed
> `TApplication::proc`'s app-state jump table for state 4 at the gameplay case.
> State 4 is `APP_STATE_DONE`, not "the intro movie" — boot merely *reaches*
> the intro through it, because that case sets `mNextArea` to `AREA_OPTION` and
> falls through into the `APP_STATE_MOVIE` body. `proc()` enters the same state
> whenever the app is sent back to the title, which is what the console reset
> button does (`isSomethingPushed()` -> `nextState = APP_STATE_DONE`), so the
> redirect turned every reset into a reload of the current stage. The upstream
> code avoids that by rewriting the case body rather than the dispatch: it sets
> `mCurrArea` to `AREA_OPTION` itself and only then branches into the gameplay
> body, so every path through `APP_STATE_DONE` — reset included — has a
> destination. Anything that patches a shared state-machine entry for a one-off
> boot transition has this problem; check what else reaches the state, and what
> state it leaves behind, before you patch it.

### Stage Intro Skip (`features.cpp`)

*Skip the per-stage intro cutscene.* It is a **skip, not a fast-forward** — the
whole intro is run out inside a single frame.

`TMarDirector::direct` spends a tick budget in a loop, ending it by setting
`mGameState |= 0x4000` (which also gates the draw pass). Two hooks:

- **`direct+0x158`**, on that very store. While `mCurState == 1` and the
  intro-text state `mConsole->unk94->unk2BC < 3`, it refills the budget
  (`r3 + 15`, r3 being the pre-decrement budget), resets the tick counter, and
  **branches past the store** — so the loop keeps running game logic, without
  rendering, until the intro state advances. At `unk2BC == 3` it zeroes
  `mFader->unk18` instead; otherwise the original store runs.
- **`changeState+0x1CC`**, on the `andi.` that tests the skip buttons. It
  `crandc`s the result so that when `unk2BC == 0` the code takes its "player
  pressed skip" path unprompted.

`unk2BC`'s offset differs per revision (JP `0x2BC`, US `0x2B8`, **PAL `0x8DC`**),
as does the fader load, so those words come from the per-region upstream
sources.

> Do **not** confuse this with the separate *Fast Forward* code, which scales
> `direct()`'s `600` literal (2 ticks/frame stock → 8 at 4x, 16 at 8x) and
> applies to the whole game. An earlier version of this feature implemented that
> instead, which merely sped the intro up.

### No Shine Get Animation (`features.cpp`)

*Play the shine grab in full, then stop dead.* Mario should do the spin and fall
to the ground — so the landing can be timed — and from the moment he lands be
back under player control, with no collected-shine animation trailing him and
nothing banked.

Five hooks; four are asm, the fifth is `featuresOnStageLoad()`:

| site | original | replaced with |
|---|---|---|
| `winDemo+0x88` | `gpMarDirector->fireGetStar(shine)` | remember `director->unk58` (frame) |
| `winDemo+0xA4` | `shine->receiveMessage(mario, TAKE)` | `shine->unk64 &= ~1` |
| `winDemo+0xAC` | `mario->mSubState = 1` | `mState = STATE_IDLE; mSubState = 0` |
| `TShine::touchPlayer` entry | prologue | return unless >4 frames since last touch |
| `setupObjects` | — | clear the remembered frame |

Each does one necessary thing, and dropping any of them shows:

- `fireGetStar` is what banks the shine and ends the run.
- **`receiveMessage(TAKE)` is what makes the collected shine animate and follow
  Mario** — leaving it in place is why an earlier version had a shine bobbing
  over his head afterwards. Clearing bit 0 of the shine's flags instead also
  restores its collision so it can be grabbed again.
- `+0xAC` keeps Mario out of winDemo's second phase, which would otherwise
  re-assert the Shine Get animation and stop his process every frame.
- The debounce is required *because* collision comes back: without it Mario
  re-enters the grab on the next frame while still standing in the shine.

The touch timestamp sits at a fixed scratch address
(`SUSAMUNE_ADDR_SHINE_TOUCH_FRAME`), as upstream does. A cave has no way to
find a mod global, so the alternative is patching the address into every
`lis`/`ori` that references it at init -- more machinery than a fixed word in
the scratch region is worth. See `addresses.hxx` for what else lives there.

## Bind-driven actions (`binds.*` + `actions.cpp`)

Several codes are not toggles: they act when a button combination is pressed.
Upstream hardcodes the combination; susamune makes it configurable.

### The bind subsystem

`binds.*` is a deliberate mirror of `settings.*`, so the two read the same way:

- one row in `SUSAMUNE_BIND_LIST` (`include/susamune/binds_list.h`) —
  enumerator + stable ini key, shared with the ARM kernel, **append-only**
  because the order is the persisted layout;
- one matching row in `src/binds_descs.inc` — menu label + default combo;
- one `u16` of live value in `gBinds`.

A bind is a mask of at most four GameCube button bits, drawn from
`SUSAMUNE_BIND_BUTTON_LIST` (A/B/X/Y/L/R/Z/Start + the four d-pad directions —
`0x1F7F`).

Matching is **exact equality** over those bits: the buttons held must be the
bind's and nothing else. That is what almost every gecko code does, through the
gecko VM (`28400D50 0000VVVV` with a zero mask — Fast Forward, DPad Functions,
Mario/Coin Count Savestate) or a plain `cmplwi` (Instant Restart's `0x208`, the
Red Coin / QF Time / In-Game Time savestates' `1` and `2`). Two actions bound to
the same combo both fire; nothing arbitrates.

A few codes want the looser rule instead — Pattern Selector tests a bare
`andi. r0,r0,0x40`, and Spawn Yoshi (`rlwinm r0,r4,0,16,27`) and Manual Attempt
Counter (`andi. r0,r0,0xFFF0`) mask the d-pad nibble out before comparing. That
is what `isHeldSubset()` / `wasPressedSubset()` are for.

Input is read from `JUTGamePad::mPadStatus[0]` — the raw pad sample — not from
the game's `TMarioGamePad`. That is the same place the gecko codes read, and it
keeps binds alive while the game has the player's pad disabled (dialogue,
cutscenes), which is exactly when fast-forward is wanted.

`gBinds.update()` runs once per frame from `onUpdate`, **before**
`director->direct()` — `onUpdateGameMode` runs inside it and asks whether the
menu bind fired this frame, to swallow the pause the menu combo's Start would
otherwise trigger.

Every query reports false for an unbound bind, while the recorder is running,
and from then until the pad is released — a four-button combo commits while
still held, and would otherwise immediately fire what it had just been bound to.
Binds are **not** silenced while the menu is open, since the menu toggle is
itself a bind.

**Recording.** `A` on a row in the menu's Binds tab arms `gBinds`' recorder.
It waits for the pad to go idle (the `A` that started it is still down), then
accumulates held buttons and commits as soon as either a held button is
released or four are down. Only the C-stick is watched by the menu meanwhile —
every real button is a candidate for the combo — so the tab reports
`grabsInput()` and `Menu::update` hands it the pad exclusively, suppressing tab
switching and the `Y`+`Start` close combo. C-stick in any direction cancels;
C-stick left on an idle row clears a bind to "none".

**Persistence** rides on the settings handoff: `SusamuneCfg` gained
`binds[]` + `bindCount` and susamune.ini gained a `[binds_<region>]` section, written as
`+`-joined tokens (`regrab_object = X+DUp`) from the shared button list. A
launcher built before binds existed leaves `bindCount` zero — it memsets only
the part of the block it knows about — which reads as "nothing persisted, keep
the defaults", so no version bump was needed.

### Regrab Last Held Object (DPad Functions, mode bit `0x408`)

```
48000000 8040A398   ; pointer = gpMarioAddress
1400007C 00000383   ; *(u32*)(pointer + 0x7C) = 0x383
```

`TMario+0x7C` is `mState` and `0x383` is `MARIO_STATUS_TAKE`, so the Gecko
operation is "force Mario into the pick-up action". A throw actually clears
`mHeldObject`, however, and TAKE only attaches an object through `mGrabTarget`.
The native port therefore remembers the real held-object pointer, supplies it
as the target, and lets the object's retail `HIT_MESSAGE_TAKE` handler restore
its own holding state.

Susamune mirrors upstream and re-writes the word every frame while the exact
combo is held.

### Spawn Yoshi

Two `C2` injections upstream.

The first, at `TMario::checkCollision+0x44`, paints the requested colour onto
`mario->mYoshi`, refills its juice, and branches into the middle of that
function's existing "Mario landed on Yoshi" path at `checkCollision+0x1C0` —
past the part that would teleport Mario onto the (unhatched, positionless)
Yoshi. What is left of that path is six lines, and `spawnYoshi()` in
`actions.cpp` just *is* those six lines, with no patch at all:

```c
mModelAngleY = mAngle.y;
if (hasFludd) { stash nozzle + water level for the dismount }
mYoshi->ride();
hasFludd = true;                        // Yoshi's juice runs through the FLUDD
mFludd->changeNozzle(Yoshi, true);
changePlayerStatus(MARIO_STATUS_WAIT, 0, false);
```

The colour ids come from the gecko's `rlwnm r0, 0x63000000, r4, 30, 31` lookup
and match `TYoshi::Color`: green 0, orange 1, purple 2, pink 3. The gate on
`mState & 0x1000` is `checkCollision`'s own early-out, kept so the action can't
fire during a cutscene.

The second injection, at `TEggYoshi::control+0x1C`, is the one thing a
per-frame hook cannot do: it runs **once per egg in the stage**, making the
level's egg vanish and remembering it on the Yoshi (`TYoshi::mEgg`) so the egg
is restored via `startFruit()` when the Yoshi expires. There is no global to
enumerate eggs from, so we keep an injection there — but as a **ten-word
trampoline into C**, not transcribed asm: save LR, `mr r3,r31`, `bl
susamuneOnEggYoshiControl`, restore, run the displaced original, branch back.
Two things make that trampoline safe and cheap:

- the site is the instruction *after* `bl TMapObjBase::control()`, so every
  volatile register (and LR, CR, f0-f13) is dead there and the C callee may
  clobber whatever it likes; `r31` is `this`.
- it is spliced in only while a spawn is armed (`gEggKillFrames`), so the game
  runs unpatched the rest of the time. The countdown exists because
  `TEggYoshi::control` runs inside `director->direct()`, i.e. *before* the next
  `actionsApply()`.

### Fast Forward

```
020ECDE2 00000258   ; default
28400D50 00000201   ; if buttons == B + DPad Left
020ECDE2 00000960
28400D51 00000202   ; else if buttons == B + DPad Right
020ECDE2 000012C0
```

The patched halfword is the immediate of `li r3, 600` at
`TMarDirector::direct+0x24` — the per-frame logic-tick budget the director
spends before it renders — so 2400/4800 run 4x/8x the game logic per rendered
frame. There is nothing to reimplement: this stays a masked halfword write,
applied while the bind is **held** and restored (from the captured original) on
release. Do not confuse it with Stage Intro Skip, which refills the same budget
mid-loop.

### Adding another bind-driven action

1. Append a row to `SUSAMUNE_BIND_LIST` (`binds_list.h`) and a matching row to
   `src/binds_descs.inc` with the default combo from the code's
   `<description>`. Binds also drive the menu toggle and savestate save/load,
   which are not gecko ports at all. The menu tab, the ini keys on both sides and the MEM2
   layout all follow automatically.
2. Write the behaviour in `actions.cpp` gated on `gBinds.wasPressed(...)` or
   `gBinds.isHeld(...)`, and call it from `actionsApply()`.

## Codes ported so far

- **Class A** — Infinite Lives, Unlock Nozzles, Unlock Yoshi, Any Fruit Opens
  Yoshi Eggs, Infinite Juice, Enable Exit Area Everywhere, FMV Skips, Intro
  Skip, Respawn One-Time Shines, Fruit Never Time Out, Fast Text (QoL); Mute
  Background Music, Shine Outfit, Shiny Shines, Shadow Mario HP Meter
  (Cosmetic).
- **Class B (`C2` hooks)** — Free Pause, Disable Blue Coin Flag, Deathless
  Blooper Surfing (QoL); Replace Episode Names (Cosmetic); Fast Piantissimo,
  Disable 3rd Chomplet Aggro, Never Pause IGT, Force Plaza Events, Stage Intro
  Skip, No Shine Get Animation (Misc).
- **Class C (multi-state)** — FLUDD in secrets (QoL, 3-state); Nozzle Lock
  (Misc, 4-state).
- **Bind-driven actions** — Regrab Last Held Object, Spawn Yoshi (4 colours),
  Fast Forward (4x / 8x). See "Bind-driven actions" above.

All toggles default Off / choice 0; binds default to the combinations their
gecko originals hardcode. See the per-feature comments in `features.cpp` /
`actions.cpp` for the exact gecko-line → patch/C mapping.

This completes both the "Simple On/Off Toggles or Select Options" and the
"Simple Actions/Binds" lists in `doc/gecko_codes.md`.

## Remaining

- **DPad leftovers (not toggles).** The DPad Functions code also carries
  *position save/load* (the `gpMarioPos`/`gpCamera` gecko-register blocks).
  Per `doc/gecko_codes.md` that belongs with the savestate / "Lite Savestate"
  work, so it is intentionally not ported here. (*Regrab last held object*,
  the other leftover, is now a bind — see above.)
- **Gecko-relocation caveat.** The launcher relocates some absolute
  heap-address gecko codes past the mod's arena reservation
  (`PatchSusamuneGeckoCodes`, see AGENTS.md). A **port carries the same
  problem**: most ports are fine (they patch `.text`/`.data` or read live
  pointers), but a patch row that targets a hardcoded heap address must add
  `SUSAMUNE_ARENA_RESERVE_SIZE` itself — that is what `FHEAP`/`FHEAPLO` in
  `features.cpp` are for. Fast Text is the case in point: its three
  instruction patches are plain `FWORD`s, but the `!!!` text it forces is
  planted directly in the loaded message buffer, so those rows are `FHEAP`s.
  PAL loads one message file per language at a different address, so its row
  is resolved at runtime from the language word (`resolveFastTextPalMsg`) —
  unresolved rows (`addr == 0`) are skipped by `applyPatches`.
