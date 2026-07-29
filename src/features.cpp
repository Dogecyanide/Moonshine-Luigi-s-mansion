// =====================================================================
// features.cpp
//
// The patch tables behind the ported gecko codes. A Feature is a SettingId
// plus Patch rows; each Patch overwrites (part of) one word in game memory.
// The original word is captured from the live game on first apply, so "Off"
// restores the retail instruction without hardcoding it per region.
//
// Addresses use SUSAMUNE_MEM1_ADDR(jp, us, pal) so one table serves all three
// revisions, and live here rather than addresses.hxx since each is meaningless
// outside its feature row.
//
// Source codes are in ../../src/gct-generator/Codes.xml; see
// doc/gecko_porting.md for the porting method and when NOT to use these
// tables (anything with real control flow belongs in C instead).
// =====================================================================

#include "susamune/features.hxx"
#include "susamune/settings.hxx"
#include "susamune/addresses.hxx"  // SUSAMUNE_MEM1_ADDR
#include "Dolphin/OS.h"            // DCFlushRange, ICInvalidateRange

namespace {

// One masked write to a 32-bit word. want = (orig & ~mask) | value.
//   - mask 0xFFFFFFFF -> replace the whole word (gecko 04 / C6 / instr).
//   - mask 0xFFFF0000 -> replace the high halfword only (gecko 02 at a
//     word-aligned address, e.g. turning a conditional branch unconditional).
struct Patch {
    u32 addr;
    u32 mask;
    u32 value;
};

struct Feature {
    SettingId    id;
    const Patch *patches;
    u8           num;
};

// Whole-word patch: `val` is the on-state word.
#define FWORD(jp, us, pal, val) \
    { SUSAMUNE_MEM1_ADDR(jp, us, pal), 0xFFFFFFFFu, (val) }
// Masked patch: replace only the bits in `mask` with `val`.
#define FMASK(jp, us, pal, mask, val) \
    { SUSAMUNE_MEM1_ADDR(jp, us, pal), (mask), (val) }

// Patch at a hardcoded *heap* address (one region's, so these live inside a
// per-region #if). The mod raises the game's arena floor by
// SUSAMUNE_ARENA_RESERVE_SIZE, so every bottom-anchored heap allocation sits
// that much higher than the address a gecko code hardcodes -- add it back.
// PatchSusamuneGeckoCodes() in the launcher does the same to the .gct when
// these codes are run as gecko instead of ported.
#define FHEAP(addr, val) \
    { (addr) + SUSAMUNE_ARENA_RESERVE_SIZE, 0xFFFFFFFFu, (val) }
#define FHEAPMASK(addr, mask, val) \
    { (addr) + SUSAMUNE_ARENA_RESERVE_SIZE, (mask), (val) }

// A patch row whose address is filled in at runtime (PAL Fast Text, below).
// applyPatches() skips rows that are still unresolved.
#define FUNRESOLVED() { 0u, 0xFFFFFFFFu, 0u }

// ---------------------------------------------------------------------
// Patch tables, one per feature. Addresses are (jp=GMSJ01, us=GMSE01,
// pal=GMSP01); the on-state words come straight from Codes.xml.
// ---------------------------------------------------------------------

// Infinite Lives: nop the store that decrements the life counter.
//   jp 040EBD80 / us 04298814 / pal 042906AC  = 60000000 (nop)
const Patch kInfiniteLives[] = {
    FWORD(0x800EBD80, 0x80298814, 0x802906AC, 0x60000000),
};

// Unlock Nozzles: force the "is this nozzle box unlocked?" query to return
// 1 (li r3,1 ; blr).
//   jp 040E79F8 / us 0429443C / pal 0428C254 = 38600001 (li r3,1)
//   jp 040E79FC / us 04294440 / pal 0428C258 = 4E800020 (blr)
const Patch kUnlockNozzles[] = {
    FWORD(0x800E79F8, 0x8029443C, 0x8028C254, 0x38600001),
    FWORD(0x800E79FC, 0x80294440, 0x8028C258, 0x4E800020),
};

// Unlock Yoshi: two gecko C6 "insert branch" writes that skip the yoshi-lock
// checks. C6 writes a `b` to the target; here the targets are addr+0x34 and
// addr+0x1C, so the encoded branches are the same on every region.
//   jp C6193F58->80193F8C / us C61BBF70->801BBFA4 / pal C61B3E28->801B3E5C
//     = b +0x34 = 48000034
//   jp C6193F9C->80193FB8 / us C61BBFB4->801BBFD0 / pal C61B3E6C->801B3E88
//     = b +0x1C = 4800001C
const Patch kUnlockYoshi[] = {
    FWORD(0x80193F58, 0x801BBF70, 0x801B3E28, 0x48000034),
    FWORD(0x80193F9C, 0x801BBFB4, 0x801B3E6C, 0x4800001C),
};

// Any Fruit Opens Yoshi Eggs: nop the check that the offered fruit matches.
//   jp 041948E8 / us 041BC900 / pal 041B47B8 = 60000000 (nop)
const Patch kAnyFruitYoshi[] = {
    FWORD(0x801948E8, 0x801BC900, 0x801B47B8, 0x60000000),
};

// Infinite Juice: nop the store that drains Yoshi's juice.
//   jp 0414DB88 / us 0426E810 / pal 0426659C = 60000000 (nop)
const Patch kInfiniteJuice[] = {
    FWORD(0x8014DB88, 0x8026E810, 0x8026659C, 0x60000000),
};

// Enable Exit Area Everywhere: C6 branch (b +0xC) skipping the check that
// gates the "Exit Area" pause option to normal stages.
//   jp C6218054->80218060 / us C6156B78->80156B84 / pal C614BB94->8014BBA0
//     = b +0xC = 4800000C
const Patch kExitAreaEverywhere[] = {
    FWORD(0x80218054, 0x80156B78, 0x8014BB94, 0x4800000C),
};

// FMV Skips: force the "have I seen this FMV?" checks to return 1 so any FMV
// can be skipped on first view.
//   jp 0410AF5C / us 042B5EF4 / pal 042ADE20 = 38600001 (li r3,1)
//   jp 0410AFC0 / us 042B5E8C / pal 042ADE88 = 38600001 (li r3,1)
const Patch kFmvSkips[] = {
    FWORD(0x8010AF5C, 0x802B5EF4, 0x802ADE20, 0x38600001),
    FWORD(0x8010AFC0, 0x802B5E8C, 0x802ADE88, 0x38600001),
};

// Respawn One-Time Shines: a C6-style branch plus two gecko 02 halfword
// writes that flip conditional branches to unconditional (high half -> 4800).
//   jp 041BF378 / us 041E792C / pal 041DF804 = 48000050 (b +0x50)
//   jp 021BFA48 / us 021E7FFC / pal 021DFED4 = 4800 (hi16) -> b
//   jp 021D72E8 / us 021FF85C / pal 021F7740 = 4800 (hi16) -> b
const Patch kRespawnShines[] = {
    FWORD(0x801BF378, 0x801E792C, 0x801DF804, 0x48000050),
    FMASK(0x801BFA48, 0x801E7FFC, 0x801DFED4, 0xFFFF0000, 0x48000000),
    FMASK(0x801D72E8, 0x801FF85C, 0x801F7740, 0xFFFF0000, 0x48000000),
};

// Fruit Never Time Out: overwrite the fruit despawn-timer limit (a datum in
// sdata) with INT_MAX so fruit never expires.
//   jp 044091A8 / us 0440C918 / pal 04404078 = 7FFFFFFF
const Patch kFruitNeverTimeout[] = {
    FWORD(0x804091A8, 0x8040C918, 0x80404078, 0x7FFFFFFF),
};

// Mute Background Music: replace the BGM volume load with fsub f1,f1,f1 (=0).
//   jp 0417FF58 / us 04016A34 / pal 04016A90 = FC210828
const Patch kMuteBgm[] = {
    FWORD(0x8017FF58, 0x80016A34, 0x80016A90, 0xFC210828),
};

// Shine Outfit: force Mario's outfit id to the shine-shirt value.
//   jp 04120D1C / us 04241FD4 / pal 04239C88 = 60000004 (ori r0,r0,4)
//   jp 04120D20 / us 04241FD8 / pal 04239C8C = B01D0004 (sth r0,4(r29))
//   jp 0412C9B0 / us 0424D4DC / pal 04245268 = 60000000 (nop)
const Patch kShineOutfit[] = {
    FWORD(0x80120D1C, 0x80241FD4, 0x80239C88, 0x60000004),
    FWORD(0x80120D20, 0x80241FD8, 0x80239C8C, 0xB01D0004),
    FWORD(0x8012C9B0, 0x8024D4DC, 0x80245268, 0x60000000),
};

// Shiny Shines: branch (b +0x4C) past the "already collected?" test so every
// shine renders yellow.
//   jp 04194E24 / us 041BCE3C / pal 041B4CF4 = 4800004C
const Patch kShinyShines[] = {
    FWORD(0x80194E24, 0x801BCE3C, 0x801B4CF4, 0x4800004C),
};

// Shadow Mario HP Meter: nop the instruction that suppresses the HP bar, so
// it shows whenever Shadow Mario is hit by water.
//   jp 04253748 / us 0403FD94 / pal 0403FBE4 = 60000000 (nop)
const Patch kShadowMarioHp[] = {
    FWORD(0x80253748, 0x8003FD94, 0x8003FBE4, 0x60000000),
};

// -- Companion static writes for the asm-hook codes below (kAsmHooks). These
//    are the non-C2 lines of those gecko codes; they share the code's setting
//    so the whole feature toggles together. --

// Free Pause: gecko C6 branch (b +0xC) that skips a pause-gate check, paired
// with the kFreePauseAsm hook.
//   jp C60EB06C->800EB078 / us C6297AB0->80297ABC / pal C628F948->8028F954
const Patch kFreePauseBranch[] = {
    FWORD(0x800EB06C, 0x80297AB0, 0x8028F948, 0x4800000C),
};

// Disable Blue Coin Flag: nop the store that sets the collected flag; paired
// with the kBlueCoinAsm hook (which zeroes the count field).
//   jp 040E7B20 / us 04294564 / pal 0428C37C = 60000000 (nop)
const Patch kBlueCoinNop[] = {
    FWORD(0x800E7B20, 0x80294564, 0x8028C37C, 0x60000000),
};

// Never Pause IGT: two blr writes that early-return the timer-pause paths;
// paired with the kNeverPauseAsm hook.
//   jp 04092968 / us 04348048 / pal 043402A4 = 4E800020 (blr)
//   jp 0420CA84 / us 0414B628 / pal 041402B8 = 4E800020 (blr)
const Patch kNeverPauseIgt[] = {
    FWORD(0x80092968, 0x80348048, 0x803402A4, 0x4E800020),
    FWORD(0x8020CA84, 0x8014B628, 0x801402B8, 0x4E800020),
};

// Fast Text (from DPad Functions, mode bit 0x008). Forces the dialog/talk
// system to a fixed short message ("!!!") instead of the real text, by
// replacing three load instructions (Talk2D2 / EventWatcher) with immediates.
// Off restores the originals (captured), so this is a plain BOOL patch.
//   jp 80215290 / us 80153DA0 / pal 80148D20 = 38000000 (li r0, 0)
//   jp 80214610 / us 8015317C / pal 80147F98 = 38005000 (li r0, 0x5000)
//   jp 800E4888 / us 80291340 / pal 802890CC = 60000000 (nop)
//
// The forced message is only short *text* because the gecko's unconditional
// tail lines plant it in the loaded message buffer; without them the fixed
// message id resolves to whatever happens to sit there. Those writes target a
// hardcoded heap address, so they need the arena-reserve fixup (FHEAP).
//   jp 028D8A7E 00028149 -> "!" (Shift-JIS 8149) x3 at 808D8A7E..83
//      048D8A84 00000000 -> terminator
//   us 048D3A3C 21000000 -> "!" + terminator (US/PAL fonts map ASCII directly)
//   pal one buffer per language, resolved at runtime -- see kFastTextPalMsg.
#if defined(SUSAMUNE_VERSION_JP)
#define FAST_TEXT_MSG_ROWS                            \
    FHEAPMASK(0x808D8A7Cu, 0x0000FFFFu, 0x00008149u), \
    FHEAP(0x808D8A80u, 0x81498149u),                  \
    FHEAP(0x808D8A84u, 0x00000000u),
#elif defined(SUSAMUNE_VERSION_US)
#define FAST_TEXT_MSG_ROWS FHEAP(0x808D3A3Cu, 0x21000000u),
#else
#define FAST_TEXT_MSG_ROWS FUNRESOLVED(),
#endif

// Not const: PAL's message-buffer row is filled in at runtime.
Patch gFastText[] = {
    FWORD(0x80215290, 0x80153DA0, 0x80148D20, 0x38000000),
    FWORD(0x80214610, 0x8015317C, 0x80147F98, 0x38005000),
    FWORD(0x800E4888, 0x80291340, 0x802890CC, 0x60000000),
    FAST_TEXT_MSG_ROWS
};
#undef FAST_TEXT_MSG_ROWS

#if defined(SUSAMUNE_VERSION_PAL)
// PAL ships one message file per language and each loads at its own address,
// so the gecko compares a language word before writing. Indexed by that word;
// values are the gecko's, addresses still unrelocated (FHEAP is not usable in
// a plain table, the reserve is added in resolveFastTextPalMsg).
//   0474E87C 21000000 / 0474E9F4 21210000 / 0474ED38 00000000 /
//   0474EE04 A1000000 / 0474EBDC 21210000
const struct {
    u32 addr;
    u32 val;
} kFastTextPalMsg[] = {
    { 0x8074E87Cu, 0x21000000u },  // English  "!"
    { 0x8074E9F4u, 0x21210000u },  // German   "!!"
    { 0x8074ED38u, 0x00000000u },  // French   (terminator only)
    { 0x8074EE04u, 0xA1000000u },  // Spanish  "\xA1"
    { 0x8074EBDCu, 0x21210000u },  // Italian  "!!"
};

// 20570B7C in dpad_pal.txt: the language word the five writes are gated on.
#define FAST_TEXT_PAL_LANG_ADDR 0x80570B7Cu
#endif

// Force Plaza Events: three "b +0x18" branches and two nops that reroute the
// plaza-event setup; paired with the kForcePlazaAsm hook.
//   jp 0410C4C8 / us 042B7810 / pal 042AF7E0 = 48000018 (b +0x18)
//   jp 0410C514 / us 042B785C / pal 042AF82C = 48000018 (b +0x18)
//   jp 0410C57C / us 042B78C4 / pal 042AF894 = 48000018 (b +0x18)
//   jp 0410C5A8 / us 042B78F0 / pal 042AF8C0 = 60000000 (nop)
//   jp 0410C5F8 / us 042B7940 / pal 042AF910 = 60000000 (nop)
const Patch kForcePlaza[] = {
    FWORD(0x8010C4C8, 0x802B7810, 0x802AF7E0, 0x48000018),
    FWORD(0x8010C514, 0x802B785C, 0x802AF82C, 0x48000018),
    FWORD(0x8010C57C, 0x802B78C4, 0x802AF894, 0x48000018),
    FWORD(0x8010C5A8, 0x802B78F0, 0x802AF8C0, 0x60000000),
    FWORD(0x8010C5F8, 0x802B7940, 0x802AF910, 0x60000000),
};

#define FEAT(id, arr) { id, arr, (u8)(sizeof(arr) / sizeof((arr)[0])) }

const Feature kFeatures[] = {
    FEAT(SETTING_INFINITE_LIVES, kInfiniteLives),
    FEAT(SETTING_UNLOCK_NOZZLES, kUnlockNozzles),
    FEAT(SETTING_UNLOCK_YOSHI, kUnlockYoshi),
    FEAT(SETTING_ANY_FRUIT_YOSHI, kAnyFruitYoshi),
    FEAT(SETTING_INFINITE_JUICE, kInfiniteJuice),
    FEAT(SETTING_EXIT_AREA_EVERYWHERE, kExitAreaEverywhere),
    FEAT(SETTING_FMV_SKIPS, kFmvSkips),
    FEAT(SETTING_RESPAWN_SHINES, kRespawnShines),
    FEAT(SETTING_FRUIT_NEVER_TIMEOUT, kFruitNeverTimeout),
    FEAT(SETTING_MUTE_BGM, kMuteBgm),
    FEAT(SETTING_SHINE_OUTFIT, kShineOutfit),
    FEAT(SETTING_SHINY_SHINES, kShinyShines),
    FEAT(SETTING_SHADOW_MARIO_HP, kShadowMarioHp),
    // Companion static writes for asm-hook features (see kAsmHooks).
    FEAT(SETTING_FREE_PAUSE, kFreePauseBranch),
    FEAT(SETTING_DISABLE_BLUE_COIN, kBlueCoinNop),
    FEAT(SETTING_NEVER_PAUSE_IGT, kNeverPauseIgt),
    FEAT(SETTING_FORCE_PLAZA_EVENTS, kForcePlaza),
    FEAT(SETTING_FAST_TEXT, gFastText),
};

const int kNumFeatures = (int)(sizeof(kFeatures) / sizeof(kFeatures[0]));

// Per-patch mutable state, flattened across every feature in table order.
// Captured lazily on first apply: `orig` is the retail word, `last` is the
// word we most recently wrote (so we can skip untouched patches).
#define PATCH_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
const int kNumPatches =
    PATCH_COUNT(kInfiniteLives) + PATCH_COUNT(kUnlockNozzles) +
    PATCH_COUNT(kUnlockYoshi) + PATCH_COUNT(kAnyFruitYoshi) +
    PATCH_COUNT(kInfiniteJuice) + PATCH_COUNT(kExitAreaEverywhere) +
    PATCH_COUNT(kFmvSkips) + PATCH_COUNT(kRespawnShines) +
    PATCH_COUNT(kFruitNeverTimeout) + PATCH_COUNT(kMuteBgm) +
    PATCH_COUNT(kShineOutfit) + PATCH_COUNT(kShinyShines) +
    PATCH_COUNT(kShadowMarioHp) + PATCH_COUNT(kFreePauseBranch) +
    PATCH_COUNT(kBlueCoinNop) + PATCH_COUNT(kNeverPauseIgt) +
    PATCH_COUNT(kForcePlaza) + PATCH_COUNT(gFastText);
struct PatchState {
    u32  orig;
    u32  last;
    bool captured;
};
PatchState gPatchState[kNumPatches];

// =====================================================================
// Asm-hook features: ports of the gecko C2 ("insert asm") codes.
//
// A C2 branches from a game site into a block of asm and back to site+4. Each
// cave below holds that asm verbatim, including the trailing 00000000
// placeholder every C2 ends with, which is patched in place to `b -> site+4`
// on first apply (the code handler does the same). Enabling writes
// `b site->cave`; disabling restores the captured original.
//
// The caves are mutable *initialized* arrays, so they load pre-populated with
// the blob -- no const source, no copy. Region-specific words inside the asm
// are built with SUSAMUNE_MEM1_ADDR so one array serves all three revisions.
// =====================================================================

// Fast Piantissimo (asm identical across regions).
u32 gCaveFastPiant[] = {
    0x8BFA007C, 0x23FF000C,
    0x57FFFFBE, 0x00000000,  // <- becomes b -> site+4
};

// Never Pause IGT (asm identical across regions).
u32 gCaveNeverPause[] = {
    0x80030030, 0x2C000000,
    0x4C820020, 0x7C0802A6,
    0x60000000, 0x00000000,
};

// Deathless Blooper Surfing. Word 0 is an sdata (r13-relative) load whose
// offset differs per region; word 8 is a cmplwi against a region-specific
// address low half.
u32 gCaveDeathless[] = {
    SUSAMUNE_MEM1_ADDR(0x818D9A10u, 0x818D9D48u, 0x818D9C70u), 0x812C0018u,
    0x814C0014u, 0x554A103Au,
    0x7D495214u, 0x858AFFFCu,
    0x800C0000u, 0x6C00803Cu,
    SUSAMUNE_MEM1_ADDR(0x280060C0u, 0x2800E6C8u, 0x28005EB8u), 0x40A20010u,
    0xA00C00F2u, 0x7000FFF6u,
    0xB00C00F2u, 0x7C0A4840u,
    0x4181FFDCu, 0x00000000u,
};

// Replace Episode Names with their ID. Word 0 loads a region-specific global.
u32 gCaveReplaceEp[] = {
    SUSAMUNE_MEM1_ADDR(0x80AD97D0u, 0x80AD9FA0u, 0x80AD9EC8u), 0x88A500DFu,
    0x38A50031u, 0x54A5403Eu,
    0xB0A60000u, 0x38800080u,
    0x60000000u, 0x00000000u,
};

// Disable Blue Coin Flag (asm half). Word 1 loads a region-specific global.
u32 gCaveBlueCoin[] = {
    0x7CA00039u, SUSAMUNE_MEM1_ADDR(0x80AD97D0u, 0x80AD9FA0u, 0x80AD9EC8u),
    0x38800000u, 0x908500D4u,
    0x60000000u, 0x00000000u,
};

// Free Pause (asm half). Words 8/9 are lis/ori loading a region-specific
// return address, rebuilt from SUSAMUNE_MEM1_ADDR (ori => no sign fixup).
#define FREE_PAUSE_ADDR SUSAMUNE_MEM1_ADDR(0x800EB038u, 0x80297A7Cu, 0x8028F914u)
u32 gCaveFreePause[] = {
    0x887F007Cu, 0x2803000Fu,
    0x41820028u, 0x807F0018u,
    0x80630000u, 0x806300D4u,
    0x546307FFu, 0x41820014u,
    0x3C600000u | (FREE_PAUSE_ADDR >> 16), 0x60630000u | (FREE_PAUSE_ADDR & 0xFFFFu),
    0x7C6803A6u, 0x4E800020u,
    0x881F0124u, 0x00000000u,
};
#undef FREE_PAUSE_ADDR

// -- No Shine Get Animation ------------------------------------------------
//
// The grab must play out in full (immobile, jump, landing) and then simply
// stop: control back to Mario, shine neither banked nor left floating over his
// head. Four hooks in TMario::winDemo / TShine::touchPlayer do that; the fifth
// upstream hook (clearing the touch timestamp on stage load) is featuresOnStage
// Load() below. Translated:
//
//   winDemo, phase 0, "landed" branch:
//     +0x88  gpMarDirector->fireGetStar(shine)   -> touch frame = dir->unk58
//     +0xA4  shine->receiveMessage(mario, TAKE)  -> shine->unk64 &= ~1
//     +0xAC  mario->mSubState = 1                -> mState = IDLE, mSubState = 0
//   TShine::touchPlayer entry:
//            (prologue)                          -> return early unless more
//                                                   than 4 frames since the
//                                                   last touch
//
// Dropping the TAKE message is what stops the collected-shine animation from
// following Mario around; clearing bit 0 of the shine's flags puts its
// collision back so it can be grabbed again. That in turn is why touchPlayer
// needs the 4-frame debounce -- without it Mario re-enters the grab on the very
// next frame while still standing in the shine.
//
// The touch timestamp lives at a fixed scratch address (addresses.hxx) rather
// than in a mod global: a cave has no way to find one, so a global would have
// to be patched into every lis/ori that references it.
#define SHINE_TOUCH_LIS (0x3D800000u | (SUSAMUNE_ADDR_SHINE_TOUCH_FRAME >> 16))
#define SHINE_TOUCH_ORI (0x618C0000u | (SUSAMUNE_ADDR_SHINE_TOUCH_FRAME & 0xFFFFu))

// winDemo+0x88, replacing `bl fireGetStar` (r3 = gpMarDirector).
u32 gCaveShineNoFire[] = {
    SHINE_TOUCH_LIS, SHINE_TOUCH_ORI,  // lis/ori r12, &touch frame
    0x81630058u,               // lwz r11, 0x58(r3)   director->unk58 (frame)
    0x916C0000u,               // stw r11, 0(r12)
    0x00000000u,
};

// TShine::touchPlayer entry, replacing `mflr r0`. Safe to return from here:
// nothing has been pushed yet, so LR still holds the caller's return address.
u32 gCaveShineTouch[] = {
    SHINE_TOUCH_LIS, SHINE_TOUCH_ORI,  // lis/ori r12, &touch frame
    0x800C0000u,               // lwz r0, 0(r12)      last touch frame
    SUSAMUNE_MEM1_ADDR(0x816D97E8u, 0x816D9FB8u, 0x816D9EE0u),  // lwz r11,gpMarDirector(r13)
    0x816B0058u,               // lwz r11, 0x58(r11)  current frame
    0x7C005850u,               // subf r0, r0, r11    elapsed
    0x28000004u,               // cmplwi r0, 4
    0x916C0000u,               // stw r11, 0(r12)     remember this touch
    0x4C810020u,               // blelr               <= 4 frames -> do nothing
    0x7C0802A6u,               // mflr r0             (displaced original)
    0x00000000u,
};
#undef SHINE_TOUCH_LIS
#undef SHINE_TOUCH_ORI

// winDemo+0xA4, replacing the `receiveMessage(mario, TAKE)` call (r3 = shine).
u32 gCaveShineKeep[] = {
    0x80030064u,               // lwz r0, 0x64(r3)
    0x5400003Cu,               // rlwinm r0,r0,0,0,30   clear bit 0
    0x90030064u,               // stw r0, 0x64(r3)
    0x00000000u,
};

// winDemo+0xAC, replacing `mSubState = 1` so Mario never enters winDemo's
// second phase (which re-asserts the animation and stops his process).
u32 gCaveShineRelease[] = {
    0x3C000C40u, 0x60000201u,  // lis/ori r0, 0x0C400201 (STATE_IDLE)
    0x901F007Cu,               // stw r0, 0x7C(r31)   mario->mState
    0x38000000u, 0x901F0084u,  // li r0,0 ; stw r0, 0x84(r31)  mSubState(+timer)
    0x00000000u,
};

// -- Stage Intro Skip -------------------------------------------------------
//
// Not a fast-forward: it runs the intro to completion inside a single frame.
// TMarDirector::direct spends its tick budget in a loop and ends the loop by
// setting mGameState |= 0x4000; the first hook sits on exactly that store and,
// while the intro is playing, refills the budget and branches *past* it, so the
// loop keeps running game logic (and never reaches the draw pass) until the
// intro-text state advances. The second hook makes changeState take its
// "player pressed skip" path on its own. Translated:
//
//   direct+0x158, replacing `mGameState |= 0x4000`:
//     s = mConsole->unk94->unk2BC            (intro-text state)
//     if (mCurState == 1 && s < 3) { mTickBudget += 20; i = 0; }  // keep going
//     else if (mCurState == 1 && s == 3) { gpApplication.mFader->unk18 = 0; }
//     else                                 { mGameState |= 0x4000; }  // normal
//
//   changeState+0x1CC, replacing the `andi.` that tests the skip buttons:
//     treat "no button pressed" as false when unk2BC == 0, i.e. auto-skip.
//
// unk2BC's offset and the fader load differ per revision (PAL moves the field
// to 0x8DC), so those words come from the upstream per-region sources.
#define SIS_CONSOLE_OFF SUSAMUNE_MEM1_ADDR(0x02BCu, 0x02B8u, 0x08DCu)
u32 gCaveStageIntro[] = {
    0x899A0064u,                       // lbz r12, 0x64(r26)   mCurState
    0x2C0C0001u,                       // cmpwi r12, 1
    0x40A20040u,                       // bne -> original
    0x819A0074u,                       // lwz r12, 0x74(r26)   mConsole
    0x818C0094u,                       // lwz r12, 0x94(r12)   ->unk94
    0x816C0000u | SIS_CONSOLE_OFF,     // lwz r11, unk2BC(r12)
    0x2C0B0003u,                       // cmpwi r11, 3
    0x41A1002Cu,                       // bgt -> original
    0x41A00018u,                       // blt -> keep-going
    SUSAMUNE_MEM1_ADDR(0x3D80803Eu, 0x3D80803Fu, 0x3D80803Eu),  // lis r12, fader@ha
    SUSAMUNE_MEM1_ADDR(0x818C6034u, 0x818C9734u, 0x818C10F4u),  // lwz r12, fader@l
    0x39600000u,                       // li r11, 0
    0x916C0018u,                       // stw r11, 0x18(r12)   fader->unk18 = 0
    0x48000014u,                       // b -> original
    0x3863000Fu,                       // addi r3, r3, 15      (r3 = pre-dec budget)
    0x907A0054u,                       // stw r3, 0x54(r26)    refill
    0x3B800000u,                       // li r28, 0            reset tick counter
    0x48000008u,                       // b -> skip the original store
    0xB01A004Cu,                       // sth r0, 0x4C(r26)    (displaced original)
    0x00000000u,
};

// changeState+0x1CC, replacing `andi. r0, r0, 0x61`.
u32 gCaveStageIntroSkipBtn[] = {
    0x807F0074u,                       // lwz r3, 0x74(r31)   mConsole
    0x80630094u,                       // lwz r3, 0x94(r3)
    0x80630000u | SIS_CONSOLE_OFF,     // lwz r3, unk2BC(r3)
    0x2C830000u,                       // cmpwi cr1, r3, 0
    0x70000061u,                       // andi. r0, r0, 0x61  (displaced original)
    0x4C423102u,                       // crandc eq, eq, cr1.eq
    0x60000000u,                       // nop
    0x00000000u,
};
#undef SIS_CONSOLE_OFF

// Force Plaza Events (asm half). Word 0 loads a region-specific global.
u32 gCaveForcePlaza[] = {
    SUSAMUNE_MEM1_ADDR(0x806D97D0u, 0x806D9FA0u, 0x806D9EC8u), 0x899D0001u,
    0x558BF7BCu, 0x7D8C5B78u,
    0x558C16FAu, 0x3D60FFF3u,
    0x616BFF01u, 0x5D6B6636u,
    0x99630070u, 0x00000000u,
};

struct AsmHook {
    SettingId id;
    u32       site;   // game address whose instruction becomes b->cave
    u32      *cave;    // asm block; its trailing word is patched to b->site+4
    u8        n;       // word count of cave
};

#define HOOK(id, jp, us, pal, caveArr) \
    { id, SUSAMUNE_MEM1_ADDR(jp, us, pal), caveArr, \
      (u8)(sizeof(caveArr) / sizeof(u32)) }

const AsmHook kAsmHooks[] = {
    HOOK(SETTING_FAST_PIANTISSIMO, 0x80256A14u, 0x80043064u, 0x80042EDCu,
         gCaveFastPiant),
    HOOK(SETTING_NEVER_PAUSE_IGT, 0x8009292Cu, 0x8034800Cu, 0x80340268u,
         gCaveNeverPause),
    HOOK(SETTING_DEATHLESS_BLOOPER, 0x801397D0u, 0x8025A340u, 0x802520CCu,
         gCaveDeathless),
    HOOK(SETTING_REPLACE_EPISODE_NAMES, 0x80232C70u, 0x801727B8u, 0x80168758u,
         gCaveReplaceEp),
    HOOK(SETTING_DISABLE_BLUE_COIN, 0x800FA12Cu, 0x802A6728u, 0x8029E680u,
         gCaveBlueCoin),
    HOOK(SETTING_FREE_PAUSE, 0x800EAF90u, 0x802979D4u, 0x8028F86Cu,
         gCaveFreePause),
    HOOK(SETTING_FORCE_PLAZA_EVENTS, 0x8010C41Cu, 0x802B7764u, 0x802AF734u,
         gCaveForcePlaza),
    // No Shine Get Animation: four sites, all gated on the one setting.
    HOOK(SETTING_NO_SHINE_ANIM, 0x80120540u, 0x80241400u, 0x8023918Cu,
         gCaveShineNoFire),
    HOOK(SETTING_NO_SHINE_ANIM, 0x80195304u, 0x801BD334u, 0x801B51ECu,
         gCaveShineTouch),
    HOOK(SETTING_NO_SHINE_ANIM, 0x8012055Cu, 0x8024141Cu, 0x802391A8u,
         gCaveShineKeep),
    HOOK(SETTING_NO_SHINE_ANIM, 0x80120564u, 0x80241424u, 0x802391B0u,
         gCaveShineRelease),
    // Stage Intro Skip: direct+0x158 and changeState+0x1CC.
    HOOK(SETTING_STAGE_INTRO_SKIP, 0x800ECF14u, 0x80299990u, 0x80291828u,
         gCaveStageIntro),
    HOOK(SETTING_STAGE_INTRO_SKIP, 0x800EC5D0u, 0x8029904Cu, 0x80290EE4u,
         gCaveStageIntroSkipBtn),
};

const int kNumHooks = (int)(sizeof(kAsmHooks) / sizeof(kAsmHooks[0]));

struct HookState {
    u32  orig;    // retail word at the site (captured)
    u32  last;    // word we last wrote at the site
    u32  onWord;  // b site->cave, computed once the cave is placed
    bool inited;
};
HookState gHookState[kNumHooks];

#if defined(SUSAMUNE_VERSION_PAL)
// Fill in Fast Text's message-buffer row (the last row of gFastText) from the
// live language word. Only called while the setting is on, because the word is
// in the heap: before the message data is loaded it reads as whatever was
// there, and 0 (English) is indistinguishable from cleared heap.
//
// The language word is a heap address like the buffers themselves, so it
// should move with the arena reservation -- but the launcher's .gct fixup
// (PatchSusamuneGeckoCodes) relocates only the buffer writes and works on
// console, so try the unreserved address as well rather than pick one blind.
void resolveFastTextPalMsg() {
    Patch &row = gFastText[sizeof(gFastText) / sizeof(gFastText[0]) - 1];
    if (row.addr != 0) {
        return;
    }

    u32 lang = *reinterpret_cast<volatile u32 *>(FAST_TEXT_PAL_LANG_ADDR +
                                                 SUSAMUNE_ARENA_RESERVE_SIZE);
    if (lang > 4) {
        lang = *reinterpret_cast<volatile u32 *>(FAST_TEXT_PAL_LANG_ADDR);
    }
    if (lang > 4) {
        return;  // not a language yet; try again next frame
    }

    row.addr  = kFastTextPalMsg[lang].addr + SUSAMUNE_ARENA_RESERVE_SIZE;
    row.value = kFastTextPalMsg[lang].val;
}
#endif

void applyPatches() {
#if defined(SUSAMUNE_VERSION_PAL)
    if (gSettings.getBool(SETTING_FAST_TEXT)) {
        resolveFastTextPalMsg();
    }
#endif
    int idx = 0;
    for (int f = 0; f < kNumFeatures; f++) {
        const Feature &feat = kFeatures[f];
        if (idx + feat.num > kNumPatches) {
            return;
        }
        bool on = gSettings.getBool(feat.id);
        for (int j = 0; j < feat.num; j++, idx++) {
            const Patch &p = feat.patches[j];
            PatchState  &s = gPatchState[idx];

            if (p.addr == 0) {
                continue;  // unresolved (PAL Fast Text message buffer)
            }

            if (!s.captured) {
                s.orig     = *reinterpret_cast<volatile u32 *>(p.addr);
                s.last     = s.orig;
                s.captured = true;
            }

            u32 want = on ? ((s.orig & ~p.mask) | p.value) : s.orig;
            if (want != s.last) {
                writeGameCode(p.addr, want);
                s.last = want;
            }
        }
    }
}

void applyHooks() {
    for (int k = 0; k < kNumHooks; k++) {
        const AsmHook &h = kAsmHooks[k];
        HookState     &s = gHookState[k];

        if (!s.inited) {
            // The cave already holds the asm (loaded with the blob); patch its
            // trailing placeholder to `b -> site+4` in place, then flush the
            // whole cave so the core can execute it.
            u32 backAddr    = reinterpret_cast<u32>(&h.cave[h.n - 1]);
            h.cave[h.n - 1] = branchWord(backAddr, h.site + 4);

            DCFlushRange(h.cave, h.n * 4);
            ICInvalidateRange(h.cave, h.n * 4);

            s.orig   = *reinterpret_cast<volatile u32 *>(h.site);
            s.onWord = branchWord(h.site, reinterpret_cast<u32>(&h.cave[0]));
            s.last   = s.orig;
            s.inited = true;
        }

        u32 want = gSettings.getBool(h.id) ? s.onWord : s.orig;
        if (want != s.last) {
            writeGameCode(h.site, want);
            s.last = want;
        }
    }
}

// =====================================================================
// Choice features: multi-state options carved out of the multi-feature gecko
// codes (Nozzle Lock, DPad Functions' FLUDD-in-secrets). Choice 0 must be the
// game default -- it restores the captured original; choice c>=1 writes
// vals[c-1].
// =====================================================================

struct ChoicePatch {
    u32        addr;
    u32        mask;
    const u32 *vals;  // vals[c-1] for choice c>=1; choice 0 uses captured orig
};

struct ChoiceFeature {
    SettingId          id;
    const ChoicePatch *patches;
    u8                 num;
};

// Nozzle Lock. One site: the game's "current nozzle" load becomes li r31,<id>.
//   jp 041494D4 / us 04269F50 / pal 04261CDC
//   Rocket=3BE00001, Turbo=3BE00005, Hover=3BE00004 (li r31, id); Unlocked=orig
const u32 kNozzleVals[] = { 0x3BE00001u, 0x3BE00005u, 0x3BE00004u };
const ChoicePatch kNozzlePatches[] = {
    { SUSAMUNE_MEM1_ADDR(0x801494D4u, 0x80269F50u, 0x80261CDCu), 0xFFFFFFFFu,
      kNozzleVals },
};

// FLUDD in secrets (from DPad Functions, mode bits 0x401/0x402/0x404). Two
// sites in TRedCoinSwitch::load. Value words are region-identical; only the
// addresses differ. vals = { No FLUDD (nop the load), All secrets (branch past
// the gate) }; Completed (default) = the original beq, i.e. captured orig.
const u32 kFluddBtn0[] = { 0x60000000u, 0x48000018u };
const u32 kFluddBtn1[] = { 0x60000000u, 0x48000014u };
const ChoicePatch kFluddPatches[] = {
    { SUSAMUNE_MEM1_ADDR(0x80198784u, 0x801C0910u, 0x801B87C8u), 0xFFFFFFFFu, kFluddBtn0 },
    { SUSAMUNE_MEM1_ADDR(0x800EC0F4u, 0x80298B88u, 0x80290A20u), 0xFFFFFFFFu, kFluddBtn1 },
};

#define CHOICE(id, arr) { id, arr, (u8)(sizeof(arr) / sizeof((arr)[0])) }

const ChoiceFeature kChoiceFeatures[] = {
    CHOICE(SETTING_NOZZLE_LOCK, kNozzlePatches),
    CHOICE(SETTING_FLUDD_SECRETS, kFluddPatches),
};

const int kNumChoiceFeatures =
    (int)(sizeof(kChoiceFeatures) / sizeof(kChoiceFeatures[0]));

const int kNumChoicePatches =
    PATCH_COUNT(kNozzlePatches) + PATCH_COUNT(kFluddPatches);
PatchState gChoiceState[kNumChoicePatches];

void applyChoices() {
    int idx = 0;
    for (int f = 0; f < kNumChoiceFeatures; f++) {
        const ChoiceFeature &cf = kChoiceFeatures[f];
        if (idx + cf.num > kNumChoicePatches) {
            return;
        }
        u8                   c  = gSettings.get(cf.id);
        for (int j = 0; j < cf.num; j++, idx++) {
            const ChoicePatch &p = cf.patches[j];
            PatchState        &s = gChoiceState[idx];

            if (!s.captured) {
                s.orig     = *reinterpret_cast<volatile u32 *>(p.addr);
                s.last     = s.orig;
                s.captured = true;
            }

            u32 want = (c == 0) ? s.orig
                                : ((s.orig & ~p.mask) | p.vals[c - 1]);
            if (want != s.last) {
                writeGameCode(p.addr, want);
                s.last = want;
            }
        }
    }
}

}  // namespace

void writeGameCode(u32 addr, u32 word) {
    *reinterpret_cast<volatile u32 *>(addr) = word;
    DCFlushRange(reinterpret_cast<void *>(addr), 4);
    ICInvalidateRange(reinterpret_cast<void *>(addr), 4);
}

u32 branchWord(u32 from, u32 to) {
    return 0x48000000u | ((to - from) & 0x03FFFFFCu);
}

#undef PATCH_COUNT

void featuresApply() {
    applyPatches();
    applyHooks();
    applyChoices();
}

void featuresOnStageLoad() {
    // Upstream clears its shine-touch timestamp from a hook in setupObjects;
    // ours lives here. Without it a stale frame number carries into the next
    // stage and can swallow the first shine touch.
    *reinterpret_cast<volatile u32 *>(SUSAMUNE_ADDR_SHINE_TOUCH_FRAME) = 0;
}
