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

- A handful of constant writes → a table row (Class A/C). Cheap and clear.
- Anything with control flow, state, or ordering → **reimplement in C**. The
  three "stateful" codes (Intro Skip, Stage Intro Skip, No Shine Get Animation)
  are done this way and are a fraction of the size of their gecko originals.

Reimplementing needs the *semantics*, which come from `../../src/sms`, not from
staring at hex. Resolve the code's addresses to functions via
`maps/<vers>.map`, read those functions in the decomp, and then express the
intent. See "Reimplemented in C" below for three worked examples.

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
   with a constant. Handled by the `kFeatures` patch table in `features.cpp`.
2. **Class B — asm injections** (`C2`/`C0`, and `06` code-caves that are jumped
   into). The logic lives in asm that runs with a hooked function's register
   state. Implemented via `kAsmHooks` in `features.cpp` (reproduce the asm
   verbatim in a mod cave; toggle a branch at the site — see "Class B: asm
   hooks" below). Use this only when the asm is short and opaque; prefer C.
3. **Class C — multi-state options** carved out of a big multi-feature code
   (DPad Functions, Nozzle Lock). These use the gecko VM to store a mode byte
   and apply different writes per mode. Implemented as `kChoiceFeatures` /
   plain patches in `features.cpp` (Nozzle Lock, FLUDD-in-secrets, Fast Text) —
   see "Class C: multi-state options" below.
4. **Reimplemented in C** — no gecko bytes at all; the behaviour is written as
   mod logic driven by a setting. See "Reimplemented in C" below.

## Class A: the `features.cpp` mechanism

`src/features.cpp` holds a table of **features**. Each feature is a `SettingId`
plus a list of **patches**; a patch is a masked write to one 32-bit word:

```c
struct Patch { u32 addr; u32 mask; u32 value; };   // want = (orig & ~mask) | value
```

- `mask = 0xFFFFFFFF` → replace the whole word (a `04` / `C6` / instruction).
- `mask = 0xFFFF0000` → replace only the high half-word (a `02` at a
  word-aligned address, e.g. flipping a conditional branch to unconditional).

`featuresApply()` runs **every frame** from `onUpdate` (mirroring how the gecko
code handler re-applies each frame):

- The **original** word is captured live from the game the first time a patch
  is touched, and is what "Off" restores. This means we never hardcode the
  retail instruction per revision — turning a toggle off always writes back
  exactly what the game shipped, whatever region we're on.
- It early-outs on any patch whose target already holds the desired word, so a
  steady state costs only reads. When a word does change we `DCFlushRange` +
  `ICInvalidateRange` the 4 bytes so instruction patches take effect.

Because capture is lazy-on-first-apply and `featuresApply()` is the only writer
of these addresses, the first read is guaranteed to be the retail value. All
target addresses are in the main DOL's `.text`/`.data`, resident from boot, so
they're valid to read/write as soon as the game loop is running.

### Adding a Class-A code

1. **Find the code** in `Codes.xml`; read its `<description>` so you know the
   intended effect (and confirm it against the decomp if the asm is doing
   something subtle).
2. **Transcribe the three source columns** (`GMSJ01`, `GMSE01`, `GMSP01`).
   Ignore `GMSJ0A`. For each gecko line work out `(addr, mask, value)`:
   - `04AAAAAA V` → `FWORD(0x80<jp>, 0x80<us>, 0x80<pal>, 0xV)`
   - `02AAAAAA 0000V` at a word-aligned addr → `FMASK(…, 0xFFFF0000, 0xV0000)`
   - `C6AAAAAA T` → `FWORD(…, encodeBranch(addr,target))` (same constant across
     regions when the distances match — verify).
3. **Add a `Patch[]` table** for the code, commenting the raw gecko lines and
   what each patched word is (opcode mnemonic helps future readers).
4. **Add a `SettingId`** by appending a row to `SUSAMUNE_SETTING_LIST`
   (`settings_list.h` — the list is shared with the launcher and is
   **append-only**, since its order is the persisted layout) and a row
   in `kSettingDescs` (`settings.cpp`) — `name`, `SETTING_BOOL`, default `0`
   (Off), and the `SETTING_CAT_*` for the tab it belongs in.
5. **Add a `FEAT(SETTING_…, kYourTable)`** line to `kFeatures`.
6. Build for all three regions and sanity-check the effect in-game.

`kMaxPatches` (currently 64) bounds the flattened patch-state array; bump it if
the table outgrows it (there's a runtime guard, not a hard crash).

### Menu wiring

Settings render generically. `SettingDesc` carries a `SettingCategory`; the
menu builds one `CategorySettingsTab` per category (`menu.cpp`, wired in
`Menu::Menu()`), each of which filters `kSettingDescs` by its category and
renders/edits the matching rows (scrolling if they overflow). So a new toggle
in an existing category needs **no menu change** — just the settings row. A new
category means a new enum value + one `mTabs[…] = new (buf) CategorySettingsTab(
"Title", SETTING_CAT_…)` line and a static buffer next to the others.

Current tabs: `Warps`, `Stages`, `QoL`, `Cosmetic`, `Misc`, `Savestate`.

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

`kAsmHooks` in `features.cpp` does this toggleably:

- Each hook has a `site` and a mutable **cave** — one `u32[]` authored with the
  asm words verbatim, *including* the trailing `00000000` placeholder. It's an
  initialized (`.data`) array, so it loads pre-populated and icache-coherent
  with the blob; there is no separate const source and no copy. On first apply
  we patch its last word in place to `b -> site+4` (`branchWord()` uses the
  handler's exact encoding), then flush the cave. `inited` guards this so it
  runs once.
- Enabling writes `b site -> cave` at the site (`onWord`); disabling restores
  the captured original word. Same capture / early-out / `DCFlushRange` +
  `ICInvalidateRange` discipline as Class A (via `writeCode()`).
- The mod is linked in MEM1 within ±32 MB of game code, so both branches are
  reachable.
- **Region-specific asm:** where a `C2`'s asm embeds an address (an sda
  `r13`-relative load, or a `lis`/`ori` pair), build those words with
  `SUSAMUNE_MEM1_ADDR(...)` so one array serves all three revisions — see
  `kFreePauseAsm` (lis/ori rebuilt from the address) and `kDeathlessAsm`
  (per-region words selected directly).
- **Companion static writes:** several of these gecko codes are a `C2` *plus*
  some plain `04`/`C6` lines. Those go in `kFeatures` under the *same*
  `SettingId`, so the whole feature toggles together (e.g. `kNeverPauseIgt`,
  `kForcePlaza`, `kFreePauseBranch`).

### Adding a Class-B (`C2`) code

1. Add a `u32 gCaveFoo[]` (mutable, initialized) with the asm words **including**
   the trailing `00000000` placeholder. Bake any region-specific words via
   `SUSAMUNE_MEM1_ADDR`.
2. Add a `HOOK(SETTING_FOO, <jp>, <us>, <pal> site, gCaveFoo)` row.
3. If the code has non-`C2` lines, add them as a `kFeatures` entry under the
   same `SETTING_FOO`.
4. Add the `SettingId` + `kSettingDescs` row + build all three regions.

## Class C: multi-state options

Some gecko codes bundle several features behind a runtime **mode byte**: the
code reads controller input, ORs mode bits into a scratch address, and each
frame applies a different set of writes per mode bit via gecko conditionals
(`28…`/`E0…`). We don't reproduce the state machine — we expose each wanted
sub-feature as its own setting and apply its word set directly.

`kChoiceFeatures` in `features.cpp` handles `SETTING_CHOICE` sub-features:

- Each choice-feature maps a `SettingId` to `ChoicePatch{addr, mask, vals}`
  rows. By convention **choice 0 is the game default and restores the captured
  original**; choice `c ≥ 1` writes `vals[c-1]`. So the setting's choice-0 label
  must be the normal-behaviour state.
- Nozzle Lock (4-state: Unlocked/Rocket/Turbo/Hover) is one site forced to
  `li r31,<id>`. FLUDD-in-secrets (3-state: Completed/No FLUDD/All secrets) is
  two `TRedCoinSwitch::load` sites.
- A sub-feature that is just on/off (Fast Text) is a plain BOOL in `kFeatures` —
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

## Reimplemented in C

Three codes are ported as mod logic rather than as gecko bytes. Each is worth
reading as a template for the next one.

### Intro Skip (`main.cpp`)

*Skip the boot logos and the intro cutscene, landing on the title screen.*

Boot runs `BOOT -> NLOGO -> intro-movie -> stage(AREA_OPTION)`, and **that last
stage is the title screen** — it is an ordinary `TMarDirector` stage, not a menu
director. Both halves are set up in `onAppInit` because `proc()` has not started
yet and neither is reachable from the per-frame hooks: `onUpdate` replaces only
the *general* `director->direct()` call in `gameLoop`, and the logo/intro states
run before it ever fires (NLOGO calls `direct()` from a second, unhooked site).

1. **Logos** — `gameLoop` directs the logo director only while bit 0 of the
   static `sGameInit` is clear, and leaves NLOGO once `sGameInit == 3` (bit 0 =
   logo done, bit 1 = setup thread joined). Presetting bit 0 means the logo
   never runs. The logo *states* must still happen: their post-`gameLoop` tails
   call `initialize_boot/nlogoAfter`, and the latter creates the stage heap.
2. **Cutscene** — repoint `proc()`'s app-state jump table so the intro-movie
   state dispatches straight into the stage case, bypassing `TMovieDirector`.
3. **Destination** — the stage case reads `mCurrentScene`, which `proc()` fills
   from `mNextScene` at the end of every earlier iteration, so staging
   `AREA_OPTION` (15) in `onAppInit` is what makes it load the title screen.

Upstream does exactly this with three writes: two branches making
`TGCLogoDir::direct()` report "done" on frame 1 (equivalent to the `sGameInit`
preset), and a code cave over the intro-movie case body that sets
`mCurrentScene` and branches to the stage case.

> **Do not** instead let the movie director run and rewrite `gameLoop`'s
> returned state. Two separate failures come from that: returning *before*
> `direct()` skips `TMovieDirector`'s first-call setup (it joins `gSetupThread`
> and calls `initSound()`) and hangs boot on a black screen; letting it run and
> overriding the result afterwards still executes the movie state's own body,
> which overwrites `mNextScene` and lands you in the airstrip instead. Bypass
> the state, do not post-process it.

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

Upstream keeps the touch timestamp at a fixed scratch address; we own a
variable instead, and the cave's `lis`/`ori` pair is rewritten at init to point
at it (`slotLis` in `AsmHook`).

## Codes ported so far

- **Class A** — Infinite Lives, Unlock Nozzles, Unlock Yoshi, Any Fruit Opens
  Yoshi Eggs, Infinite Juice, Enable Exit Area Everywhere, FMV Skips, Respawn
  One-Time Shines, Fruit Never Time Out, Fast Text (QoL); Mute Background Music,
  Shine Outfit, Shiny Shines, Shadow Mario HP Meter (Cosmetic).
- **Class B (`C2` hooks)** — Free Pause, Disable Blue Coin Flag, Deathless
  Blooper Surfing (QoL); Replace Episode Names (Cosmetic); Fast Piantissimo,
  Never Pause IGT, Force Plaza Events (Misc).
- **Class C (multi-state)** — FLUDD in secrets (QoL, 3-state); Nozzle Lock
  (Misc, 4-state).
- **Reimplemented in C** — Intro Skip, Stage Intro Skip, No Shine Get Animation
  (Misc).

All default Off / choice 0. See the per-feature comments in `features.cpp` for
the exact gecko-line → patch/asm mapping.

This completes the "Simple On/Off Toggles or Select Options" list in
`doc/gecko_codes.md`.

## Remaining

- **DPad leftovers (not toggles).** The DPad Functions code also carries
  *position save/load* (the `gpMarioPos`/`gpCamera` gecko-register blocks) and
  *Regrab last held object* (`0x408`). Per `doc/gecko_codes.md` these belong
  elsewhere — the former with the savestate / "Lite Savestate" work, the latter
  as a configurable bind, not a menu toggle — so they are intentionally not
  ported here.
- **Gecko-relocation caveat.** The launcher already relocates some absolute
  heap-address gecko codes past the mod's arena reservation
  (`PatchSusamuneGeckoCodes`, see AGENTS.md). Native ports don't need that —
  they read live pointers — but keep it in mind if a code writes to a
  hardcoded heap address rather than a static `.text`/`.data` one.
