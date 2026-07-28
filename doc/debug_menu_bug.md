# Console boot corruption at mod offset 0x6000

**Status: layout fix built; console confirmation pending.**

The developer menu, hard reset, and green hang were three effects of moving
different state across the same address, not three separate bugs. In the first
bad JP build, `gLoadMenu` landed at `0x8042c020`, exactly
`SUSAMUNE_ADDR_MOD_BASE + 0x6000`. Removing it moved `gSavestateMgr` into the
same area and changed the symptom to a reset. The later explicit-initialisation
experiment enlarged the image again and changed the symptom to a green hang.
That experiment and the broad cache invalidation are both removed.

The current fix keeps the complete injected image below the observed
`base + 0x6000` corruption boundary:

- feature patch state is sized to the 32 ordinary patches and 3 choice patches
  that actually exist, rather than reserving space for 64 and 16;
- the menu tab array and status strings are sized to their actual requirements;
- the mod scratch slot is now the final 16 bytes below the boundary; and
- launcher manifest generation rejects a blob larger than `0x5ff0`, while the
  arena reservation remains `0x8000`.

The JP image is now `0x5fbc` bytes, ending 52 bytes before the scratch slot.
US and PAL also pass the ceiling check. The dead `gLoadMenu`/app-state-9 path
remains removed; normal menu warps still use game-mode state 9 inside
`TMarDirector`.

The temporary `SUSAMUNE_DIAG_NO_MENU_INIT` option was removed. The investigation
below is retained as the record that led to the layout diagnosis.

## Symptom

On console (Nintendont), the game boots into the **debug menu**
(`TMenuDirector`, `TApplication::APP_STATE_MENU` = 9) instead of the title
screen. Reproducible on every boot.

**Dolphin is unaffected.**

## Environment

- Appeared during the configurable-binds work (`2578e62`); the previous commit
  `1c087b4` boots correctly on console.
- Independent of `intro_skip` (`= 0` in `susamune.ini` does not help).
- Independent of `susamune.ini` entirely: deleting it, so every setting and
  bind falls back to the compiled-in defaults, does **not** help.
- `-DENABLE_MENU=OFF` **does** fix it.
- `-DENABLE_MENU=ON -DSUSAMUNE_DIAG_NO_MENU_INIT=ON` (menu compiled in, all its
  per-frame paths live, but `Menu` never constructed) does **not** fix it.

## Established facts

**App state 9 is unreachable in retail.** `APP_STATE_MENU` appears exactly once
in the decomp — the `case` label in `TApplication::proc()`. Nothing assigns it.
`TMarDirector::unkB4`, the only variable that feeds an arbitrary app state back
through `desiredAppState` (`MarDirectorDirect.cpp`, `STATE_UNK9`/`STATE_UNK12`),
is only ever assigned `APP_STATE_BOOT`/`DONE`/`GAMEPLAY`/`MOVIE`/`TITLE`
(2/4/5/6/8). So the 9 is not a code path anyone wrote; it is corruption, or a
value the mod produced.

**How an app state is chosen.** `proc()` does `mAppState = gameLoop()`, and
`gameLoop`'s general branch is `nextState = mDirector->direct()` — which is the
site the mod's `onUpdate` replaces. So `onUpdate`'s return value *is* the next
app state. During `APP_STATE_BOOT`/`NLOGO`, `gameLoop` uses a different,
unhooked `direct()` call, so `onUpdate` is not involved in the logo states.

**The mod contains exactly two literal 9s**, both reachable only from
`main.cpp`:
- `onUpdate`: `if (gLoadMenu) { gLoadMenu = 0; return 9; }` — an *app* state.
- `onUpdateGameMode`: `state = 9` in the `Warp::pending()` block — a *game*
  state (`STATE_UNK9`), whose handler then does `desiredAppState = unkB4`.

**Why Dolphin differs.** Not a hardware difference. `Settings::init()` is
`resetDefaults()` under `#if IS_EMULATOR`, so on Dolphin every setting and bind
is always the compiled-in default and `susamune.ini` is never read. Persisted
configuration can only ever be wrong on console. (This also explains the
earlier red herring where Intro Skip looked responsible: it is Off by
construction on Dolphin.)

## Eliminated

Each of these was checked, not assumed.

| Hypothesis | How it was ruled out |
|---|---|
| `gLoadMenu` is corrupted into a nonzero value | Scanned every store instruction in the linked blob for one targeting its address. None. Checked with **both** `lis`/displacement decompositions — the first scan used only `lis (addr>>16)`, which is wrong when the low half has bit 15 set, so it was redone with the correct `ha16` form. |
| `Warp::pending()` is spuriously true | Same scan for `sPending` (`0x8042c1f4`, found by disassembling `Warp::pending()`): exactly two writers, `Warp::request+0x28` and `Warp::execute+0x2c`. `request` is only reachable from the warp tabs, which only run while the menu is open. |
| The mod's `.bss` is not zeroed on console | The launcher memcpy's `size` bytes from the blob. Manifest `size` covers through the end of `.bss` (`.bss` ends `0x8042c2a5`, blob covers to `0x8042c2a8`), and those bytes are all zero in `susamune.bin`. |
| The mod overruns its reserved arena window | Blob ends `0x8042c278`; window is `[0x80426020, 0x8042e020)`. ~7.5 KiB spare. |
| A persisted bind opens the menu | Deleting `susamune.ini` leaves the default menu bind `Y+Start`, which cannot fire at boot. Still broken. |
| Intro Skip | `intro_skip = 0` does not help. The jump-table write was also verified correct: slot `0x803b4084` is state 4 (`APP_STATE_DONE`, the boot intro movie), overwritten with `proc+0x108`, which is the `APP_STATE_GAMEPLAY` case body and slot `0x803b4088`'s own target. |
| `J2DTextBox` layout mismatch corrupting `sMenuBuf` | The mod's header matches the decomp field-for-field (`0xEC`–`0x124`). |
| `-fsigned-char` is wrong | The decomp — a matching build — compiles with `-char signed`. The flag is correct; without it every `s8` in the game headers reads as `0..255`. |
| The "known good" build was stale w.r.t. `-fsigned-char` | Flag changes go through Ninja's per-output command hash, not depfiles. Measured: adding one flag rebuilds all 8 TUs. |

## Real bugs found and fixed along the way

These were genuine and are fixed, but **none of them is the cause** — the
symptom survives all of them.

1. **`s8` is unsigned on this target** (`typedef char s8`). `features.cpp`'s
   `AsmHook::slotLis` used `-1` as a sentinel, so `if (h.slotLis >= 0)` was
   always true and every asm hook wrote `cave[255]`/`cave[256]` — 1 KiB past its
   cave. Latent for months; adding `binds.cpp`/`actions.cpp` shifted
   `.data.rel.ro` so the overrun landed on `kAsmHooks[9].site`. Fixed by
   `-fsigned-char` plus deleting the `slotLis` mechanism outright.
2. **Mod scratch in the practice-code region.** `SUSAMUNE_ADDR_SHINE_TOUCH_FRAME`
   was `0x817f0540`, which is *above* `__ArenaHi` (`0x81700000`) — the
   apploader's FST and Nintendont's cheat/code-handler area. That range is only
   free when a `.gct` and its handler have lowered the arena top. Moved to
   `SUSAMUNE_ADDR_MOD_SCRATCH`, the top 16 bytes of the mod's own reserved
   window. **Note `SUSAMUNE_ADDR_QF_TIMER_RESET` (`0x817f00b3`) is still in that
   region** — it is deliberate interop with the QF gecko code, but it is the same
   class of stray write and fires on every warp.
3. **Header dependency tracking never worked.** The compiler is forced before
   `project()` and its check skipped, so CMake never identified it and left
   `CMAKE_DEPFILE_FLAGS_CXX` empty: the Ninja rule asked for a depfile the
   compile command was never told to write. **Editing a header rebuilt nothing.**
   Fixed by setting the flags explicitly. Any console test before this fix may
   have run mixed-vintage objects.

## Historical narrowing before the layout diagnosis

With `SUSAMUNE_DIAG_NO_MENU_INIT=ON`, `gMenu` stays null, so `gMenu->update()`
and `gMenu->draw()` are no-ops behind their existing null guards. The only
`ENABLE_MENU` code still live is the block in `onUpdateGameMode`:

```cpp
if (director->mCurState != state && state == 0x5 &&
    gBinds.wasPressed(BIND_MENU_TOGGLE)) {
    state = director->mCurState;
}
if (Warp::pending()) { Warp::execute(); ...; director->moveStage(); state = 9; }
```

That build still fails, so on a pure code-path reading the culprit is in there.

**But the code-path reading may be wrong.** `menu.cpp` is compiled regardless of
`ENABLE_MENU`, and there is no `--gc-sections`, so the two builds differ by only
**176 bytes** (`0x6288` vs `0x61d8`) — not zero. A layout-sensitive memory
corruption would also be "fixed" by `ENABLE_MENU=OFF` simply by moving the
victim. This project has already produced two layout-sensitive bugs (both in the
table above), so this is not a remote possibility.

The two hypotheses at that point were:

- **A.** Something in `onUpdateGameMode`'s block. Candidates: `mCurState`'s
  offset in the mod's `TMarDirector` header being wrong, so
  `state = director->mCurState` assigns garbage as the director's next game
  state; or calling `gBinds.wasPressed()` / `Warp::pending()` from inside
  `changeState` (this hook sits at `changeState+0x2c0`, replacing
  `bl updateGameMode`).
- **B.** A layout-sensitive overrun somewhere else entirely, where
  `ENABLE_MENU=OFF`'s 176-byte shift moves the damage somewhere harmless.

## Diagnostic plan before the layout evidence

**Instrument rather than reason.** Analysis found the first two bugs quickly and
has produced nothing for several rounds since; `ENABLE_MENU=OFF` yielded more in
one boot than four rounds of disassembly.

The savestate status line renders independently of the menu (`sStatusBuf`,
drawn from `afterDraw`), so it is a usable console output channel. Concretely:

1. Write a distinct marker to it from each of the three `ENABLE_MENU` sites, and
   from `onUpdate` immediately before `return 9`. That answers "which site runs"
   directly instead of by elimination.
2. If no marker fires, hypothesis A is dead and it is B — a corruption hunt.
   Dump `unkB4` and `mCurState` to the status line each frame; if `unkB4` reads 9
   the corruption is in the stage heap, if it reads sanely the 9 is coming from
   somewhere else entirely.
3. To distinguish A from B cheaply first: build `ENABLE_MENU=ON` with only the
   `state = 9` line changed to `state = director->mCurState`. If the debug menu
   stops, it is A and specifically the warp path; if it persists, it is B.

## Reproducing the build states

```
# broken
cmake -B build -G Ninja -DENABLE_MENU=ON
# works
cmake -B build -G Ninja -DENABLE_MENU=OFF
# still broken (menu compiled in, never constructed)
cmake -B build -G Ninja -DENABLE_MENU=ON -DSUSAMUNE_DIAG_NO_MENU_INIT=ON
```

`SUSAMUNE_DIAG_NO_MENU_INIT` was a temporary diagnostic option and has since
been removed.
