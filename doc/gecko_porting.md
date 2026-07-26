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

## Three implementation classes

1. **Class A — static memory / instruction writes** (`04`/`02`/`00`/`C6`, and
   `06` data writes to a fixed datum). The code just overwrites game memory
   with a constant. Handled by the `kFeatures` patch table in `features.cpp`.
2. **Class B — asm injections** (`C2`/`C0`, and `06` code-caves that are jumped
   into). The logic lives in asm that runs with a hooked function's register
   state. **The `C2` case is implemented** via `kAsmHooks` in `features.cpp`
   (reproduce the asm verbatim in a mod cave; toggle a branch at the site — see
   "Class B: asm hooks" below). The `06` code-cave and stateful variants
   (Intro Skip, Stage Intro Skip, No Shine Get Animation) are **not yet built**.
3. **Class C — multi-state options** carved out of a big multi-feature code
   (DPad Functions, Nozzle Lock). These use the gecko VM to store a mode byte
   and apply different writes per mode. **Implemented** as `kChoiceFeatures` /
   plain patches in `features.cpp` (Nozzle Lock, FLUDD-in-secrets, Fast Text) —
   see "Class C: multi-state options" below.

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
4. **Add a `SettingId`** in `settings.hxx` (in its category's group) and a row
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

All default Off / choice 0. See the per-feature comments in `features.cpp` for
the exact gecko-line → patch/asm mapping.

## Remaining from the "simple toggles" list

- **`06` code-caves & stateful `C2`.** *Intro Skip* and *Stage Intro Skip* use
  `06` (write an instruction blob into a fixed game address) plus branches into
  it, and *Stage Intro Skip* / *No Shine Get Animation* are stateful (a gecko
  mode byte toggled by binds, `28…`/`E0…` conditionals). Porting: for `06`,
  write the blob to the target address as a multi-word patch and add the
  branches (all restorable like Class A); for the stateful ones, model the mode
  as a `SettingId` and apply the corresponding word set.

  **Intro Skip specifically** patches `TGCLogoDir::direct_nlogo + 0x24`,
  `TGCLogoDir::direct + 0x114`, and writes a 5-instruction cave over
  `TApplication::proc + 0x248` that forces `mCurrentScene` to stage 15 /
  episode 0 and branches back to `proc + 0x108`. All three run before the
  first `gameLoop()`, so it cannot be applied from `featuresApply()` where the
  other codes live — it needs the `onAppInit` hook (`main + 0x1c`, see
  AGENTS.md). Settings persistence now exists, so the code is finally useful:
  without it, skipping the logo means never being able to reach the
  progressive/60Hz prompt, which is why the original gecko code carries that
  warning.
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
