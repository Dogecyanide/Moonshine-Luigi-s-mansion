// =====================================================================
// qft_timer.cpp
//
// Native port of Noki Doki / sup39's Quarterframe Timer 1.5 timing and freeze
// hooks from BitPatty/gct-generator (Apache-2.0). The original renderer is
// replaced by Sunshine's HUD timer plus a compact three-decimal readout.
// =====================================================================

#include "susamune/qft_timer.hxx"

#include "Dolphin/OS.h"
#include "Dolphin/printf.h"
#include "SMS/GC2D/GCConsole2.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/features.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"
#include "susamune/susamune_cfg.h"

namespace {

  enum SunshineVisibility {
    SUNSHINE_ALWAYS,
    SUNSHINE_SHINE_ONLY,
    SUNSHINE_HIDDEN,
  };

  enum CompactVisibility {
    COMPACT_ALWAYS,
    COMPACT_ON_FREEZE,
    COMPACT_HIDDEN,
  };

  enum StopReason {
    STOP_NONE,
    STOP_SHINE,
    STOP_BOWSER,
    STOP_CUSTOM,
  };

  struct TimerState {
    u8 stopped;
    u8 restart;
    u8 stopReason;
    u8 pad;
    s32 offsetQf;
    s32 freezeQf;
    s32 freezeFrames;
  };

  static_assert(sizeof(TimerState) == 16, "QFT scratch layout changed");
  static_assert(__builtin_offsetof(TimerState, offsetQf) == 4, "QFT offset layout changed");

  volatile TimerState *const sState =
    reinterpret_cast<volatile TimerState *>(SUSAMUNE_ADDR_QFT_STATE);
  volatile s32 *const sDeathQf =
    reinterpret_cast<volatile s32 *>(SUSAMUNE_ADDR_QFT_DEATH_QF);
  volatile s32 *const sPlantQf =
    reinterpret_cast<volatile s32 *>(SUSAMUNE_ADDR_QFT_PLANT_QF);
  volatile s32 *const sTransitionQf =
    reinterpret_cast<volatile s32 *>(SUSAMUNE_ADDR_QFT_TRANSITION_QF);
  volatile u16 *const sTransitionTarget =
    reinterpret_cast<volatile u16 *>(SUSAMUNE_ADDR_QFT_TRANSITION_TARGET);

  const s32 kMaxQf          = SUSAMUNE_ILING_PB_MAX_QF;
  const u8 kFreezeFrames[] = {0, 15, 30, 60, 90, 150};

  bool sStageReady;
  bool sStagePending;
  bool sResetRequested;
  bool sFinalConsumed;
  bool sBigShown;
  bool sRetailTimerOwned;
  u32 sAttemptSerial;
  TMarDirector *sStageDirector;
  TimerState sSavedState;
  bool sHaveSavedState;
  bool sSavedFinalConsumed;
  bool sSavedRetailTimerOwned;
  u32 sSavedAttemptSerial;

// PowerPC address materialisation for fixed scratch. D-form offsets are
// signed, so use @ha/@l rather than a plain high half.
#define PPC_HA(addr) (((addr) + 0x8000u) >> 16)
#define PPC_LO(addr) ((addr) & 0xFFFFu)
#define QFT_HA       PPC_HA(SUSAMUNE_ADDR_QFT_STATE)
#define QFT_OFF(field)                                                                             \
  ((PPC_LO(SUSAMUNE_ADDR_QFT_STATE) + __builtin_offsetof(TimerState, field)) & 0xFFFFu)

  const u32 kLoadDirectorR12 =
    SUSAMUNE_MEM1_ADDR(0x818D97E8u, 0x818D9FB8u, 0x818D9EE0u);

  // Core QFT lifecycle hooks. These are the non-rendering portions of QFT 1.5,
  // rebased from 0x817F00B2 to the mod's reserved scratch.
  u32 sCaveDirectorDtor[] = {
    0x3CA00000u | QFT_HA,            // lis r5,state@ha
    0xA0050000u | QFT_OFF(stopped),  // lhz r0,stopped(r5)
    0x2C000000u,
    0x40820014u,
    0x80050000u | QFT_OFF(offsetQf),  // lwz r0,offset(r5)
    0x80C3005Cu,
    0x7C003214u,
    0x90050000u | QFT_OFF(offsetQf),  // stw r0,offset(r5)
    0x7C0802A6u,                      // displaced mflr r0
    0x00000000u,
  };

  u32 sCaveShineStop[] = {
    0x3CA00000u | QFT_HA,
    0x80C50000u | QFT_OFF(offsetQf),
    0x8003005Cu,
    0x7CC60214u,
    0x38C60004u,
    0x54C6003Au,
    0x90C50000u | QFT_OFF(offsetQf),
    0x38C0FFFFu,
    0xB0C50000u | QFT_OFF(stopped),
    0x38000001u,
    0x98050000u | QFT_OFF(stopReason),
    0x00000000u,
  };

  u32 sCaveBowserStop[] = {
    0x3D000000u | QFT_HA,
    0x80C80000u | QFT_OFF(offsetQf),
    0x8003005Cu,
    0x7CC60214u,
    0x38C60004u,
    0x54C6003Au,
    0x90C80000u | QFT_OFF(offsetQf),
    0x38C0FFFFu,
    0xB0C80000u | QFT_OFF(stopped),
    0x38000002u,
    0x98080000u | QFT_OFF(stopReason),
    0x00000000u,
  };

  u32 sCaveRestartFromInit[] = {
    0x389C0001u,  // displaced addi r4,r28,1
    0x3CA00000u | QFT_HA,
    0x98850000u | QFT_OFF(restart),
    0x00000000u,
  };

  u32 sCaveFreezeTransitionA[] = {
    0x3CA00000u | QFT_HA,
    0x38600001u,
    0x98650000u | QFT_OFF(restart),
    0x807F005Cu,
    0x38630003u,
    0x5463003Au,
    0x90650000u | QFT_OFF(freezeQf),
    0x3860FFFFu,
    0x90650000u | QFT_OFF(freezeFrames),
    0x00000000u,
  };

  u32 sCaveFreezeTransitionB[] = {
    0x3CA00000u | QFT_HA, 0x98050000u | QFT_OFF(restart),
    0x801E005Cu,          0x30000004u,
    0x5400003Au,          0x90050000u | QFT_OFF(freezeQf),
    0x90050000u | PPC_LO(SUSAMUNE_ADDR_QFT_TRANSITION_QF),
    0x3800FFFFu,          0x90050000u | QFT_OFF(freezeFrames),
    0xB3850000u | PPC_LO(SUSAMUNE_ADDR_QFT_TRANSITION_TARGET),
    0x60000000u,          0x00000000u,
  };

  u32 sCaveDeath[] = {
    kLoadDirectorR12,
    0x800C005Cu,  // lwz r0,0x5c(r12)
    0x3D800000u | PPC_HA(SUSAMUNE_ADDR_QFT_DEATH_QF),
    0x900C0000u | PPC_LO(SUSAMUNE_ADDR_QFT_DEATH_QF),
    0x38000000u,  // r0 is live: the next retail instruction stores health
    0x907F0118u,  // displaced stw r3,0x118(r31)
    0x00000000u,
  };

  u32 sCavePlant[] = {
    0x80040020u,  // lwz r0,0x20(r4): nerve timer
    0x2C000000u,
    0x40820018u,  // only the first Die tick represents the final hit
    kLoadDirectorR12,
    0x816C005Cu,
    0x396BFFFFu,  // Die begins one QF after health reaches zero
    0x3D800000u | PPC_HA(SUSAMUNE_ADDR_QFT_PLANT_QF),
    0x916C0000u | PPC_LO(SUSAMUNE_ADDR_QFT_PLANT_QF),
    0x7C0802A6u,  // displaced mflr r0
    0x00000000u,
  };

  struct CoreHook {
    u32 site;
    u32 *cave;
    u8 words;
  };

#define CORE(jp, us, pal, caveArray)                                                               \
  {SUSAMUNE_MEM1_ADDR(jp, us, pal), caveArray, (u8)(sizeof(caveArray) / sizeof(u32))}

  const CoreHook kCoreHooks[] = {
    CORE(0x800EFA30u, 0x8029C520u, 0x802943FCu, sCaveDirectorDtor),
    CORE(0x800EDB30u, 0x8029A5ACu, 0x80292480u, sCaveShineStop),
    CORE(0x801D1F38u, 0x801FA380u, 0x801F2258u, sCaveBowserStop),
    CORE(0x800EBD78u, 0x8029880Cu, 0x802906A4u, sCaveRestartFromInit),
    CORE(0x800EC72Cu, 0x802991A8u, 0x80291040u, sCaveFreezeTransitionA),
    CORE(0x800ED8F0u, 0x8029A36Cu, 0x80292204u, sCaveFreezeTransitionB),
    CORE(0x801222D8u, 0x80243148u, 0x8023AED4u, sCaveDeath),
    CORE(0x8030AD28u, 0x800F9198u, 0x800F2838u, sCavePlant),
  };

  const u32 kLegacyRenderSite     = SUSAMUNE_MEM1_ADDR(0x802069E0u, 0x801441C0u, 0x80138DFCu);
  const u32 kLegacyRenderOriginal = SUSAMUNE_MEM1_ADDR(0x4BE2E849u, 0x481A74FDu, 0x481AAA69u);

#undef CORE

  // Shared freezer used by hooks that replace a function's final blr. QFT's
  // freeze is visual: it snapshots the current quarterframe while the real
  // clock continues in the director.
  u32 sFreezeCave[] = {
    SUSAMUNE_MEM1_ADDR(0x816D97E8u, 0x816D9FB8u, 0x816D9EE0u),
    0x3D800000u | QFT_HA,
    0x816B005Cu,
    0x916C0000u | QFT_OFF(freezeQf),
    0x3960001Eu,  // li r11,duration (patched)
    0x916C0000u | QFT_OFF(freezeFrames),
    0x4E800020u,
  };

  // Object take/drop sites are not returns. Call the shared freezer, replay the
  // displaced instruction, then branch back to the game.
  u32 sTakeCave[] = {
    0x3D800000u, 0x618C0000u, 0x7D8803A6u, 0x4E800021u, 0x801F0384u, 0x00000000u,
  };

  u32 sDropCave[] = {
    0x3D800000u, 0x618C0000u, 0x7D8803A6u, 0x4E800021u, 0x38000000u, 0x00000000u,
  };

  // These boss-event sites execute once, on the exact transition tick.
  u32 sPeteyWakeupCave[] = {
    0x3D800000u, 0x618C0000u, 0x7D8803A6u, 0x4E800021u, 0x387E0000u, 0x00000000u,
  };

  u32 sEelActivateCave[] = {
    0x3D800000u, 0x618C0000u, 0x7D8803A6u, 0x4E800021u, 0x38600001u, 0x00000000u,
  };

  u32 sEelToothCave[] = {
    0x3D800000u, 0x618C0000u, 0x7D8803A6u, 0x4E800021u, 0x38000000u, 0x00000000u,
  };

  // Blue coins freeze on the next whole frame; this is QFT's special case.
  u32 sBlueCoinCave[] = {
    0x7C030378u,
    0x80A3005Cu,
    0x38A50003u,
    0x54A0003Au,
    0x3CA00000u | QFT_HA,
    0x90050000u | QFT_OFF(freezeQf),
    0x3800001Eu,  // li r0,duration (patched)
    0x90050000u | QFT_OFF(freezeFrames),
    0x60000000u,
    0x00000000u,
  };

  enum FreezeHookKind {
    FREEZE_DIRECT,
    FREEZE_CAVE,
    FREEZE_GUARDED_CAVE,
  };

  struct FreezeHook {
    u32 site;
    u32 original;
    u32 *cave;
    SettingId setting;
    u8 kind;
    u8 words;
  };

#define DIRECT(settingId, jp, us, pal)                                                             \
  {SUSAMUNE_MEM1_ADDR(jp, us, pal), 0x4E800020u, nullptr, settingId, FREEZE_DIRECT, 0}
#define CAVE(settingId, jp, us, pal, originalWord, caveArray)                                      \
  {SUSAMUNE_MEM1_ADDR(jp, us, pal),      originalWord, caveArray, settingId, FREEZE_CAVE,          \
   (u8)(sizeof(caveArray) / sizeof(u32))}
#define GUARDED_CAVE(settingId, jp, us, pal, originalWord, caveArray)                              \
  {SUSAMUNE_MEM1_ADDR(jp, us, pal), originalWord, caveArray, settingId, FREEZE_GUARDED_CAVE,       \
   (u8)(sizeof(caveArray) / sizeof(u32))}

  const FreezeHook kFreezeHooks[] = {
    DIRECT(SETTING_TIMER_FREEZE_YELLOW_COIN, 0x80196CB0u, 0x801BEE10u, 0x801B6CC8u),
    DIRECT(SETTING_TIMER_FREEZE_RED_COIN, 0x801963C4u, 0x801BE524u, 0x801B63DCu),
    CAVE(SETTING_TIMER_FREEZE_BLUE_COIN, 0x80196128u, 0x801BE288u, 0x801B6140u, 0x7C030378u,
         sBlueCoinCave),
    DIRECT(SETTING_TIMER_FREEZE_ITEM, 0x80197208u, 0x801BF3C4u, 0x801B727Cu),
    DIRECT(SETTING_TIMER_FREEZE_TALK, 0x80214F00u, 0x80153A34u, 0x801489B4u),
    DIRECT(SETTING_TIMER_FREEZE_DEMO, 0x800ED89Cu, 0x8029A318u, 0x802921B0u),
    DIRECT(SETTING_TIMER_FREEZE_CLEANED, 0x8017A3D4u, 0x80215C6Cu, 0x8020DB50u),
    DIRECT(SETTING_TIMER_FREEZE_BOWSER, 0x801D3380u, 0x801FB7ACu, 0x801F3690u),
    DIRECT(SETTING_TIMER_FREEZE_YOSHI, 0x8014F830u, 0x802704D4u, 0x80268260u),
    CAVE(SETTING_TIMER_FREEZE_TAKE, 0x8011EAE4u, 0x8023F9A8u, 0x80237734u, 0x801F0384u, sTakeCave),
    CAVE(SETTING_TIMER_FREEZE_DROP, 0x80122964u, 0x802437D4u, 0x8023B560u, 0x38000000u, sDropCave),
    GUARDED_CAVE(SETTING_TIMER_FREEZE_PETEY_WAKEUP,
                 0x802A2EB8u, 0x8009009Cu, 0x8008973Cu, 0x387E0000u,
                 sPeteyWakeupCave),
    GUARDED_CAVE(SETTING_TIMER_FREEZE_EEL_ACTIVATE,
                 0x802E5590u, 0x800D3478u, 0x800CCB18u, 0x38600001u,
                 sEelActivateCave),
    GUARDED_CAVE(SETTING_TIMER_FREEZE_EEL_TOOTH,
                 0x802E92C4u, 0x800D71ACu, 0x800D084Cu, 0x38000000u,
                 sEelToothCave),
  };

#undef DIRECT
#undef CAVE
#undef GUARDED_CAVE

  const int kNumFreezeHooks = (int)(sizeof(kFreezeHooks) / sizeof(kFreezeHooks[0]));

  constexpr u32 kPutStatuses[]       = {0x80000387u};
  constexpr u32 kTripleStatuses[]    = {0x00000882u};
  constexpr u32 kSpinStatuses[]      = {0x00000895u, 0x00000896u};
  constexpr u32 kLedgeStatuses[]     = {0x3800034Bu};
  constexpr u32 kWallStatuses[]      = {0x02000886u};
  constexpr u32 kRopeStatuses[]      = {0x00000892u, 0x00000893u};
  constexpr u32 kBounceStatuses[]    = {0x00000884u};
  constexpr u32 kJumpStatuses[]      = {0x02000880u};
  constexpr u32 kDiveStatuses[]      = {0x0080088Au};
  constexpr u32 kDoubleStatuses[]    = {0x02000881u};
  constexpr u32 kDiveRolloutStatuses[] = {0x02000889u};
  constexpr u32 kDiveGetupStatuses[]   = {0x000008A6u};

  struct StatusFreeze {
    SettingId setting;
    const u32 *statuses;
    u8 count;
  };

#define STATUS(settingId, arr) {settingId, arr, (u8)(sizeof(arr) / sizeof((arr)[0]))}

  const StatusFreeze kStatusFreezes[] = {
    STATUS(SETTING_TIMER_FREEZE_PUT, kPutStatuses),
    STATUS(SETTING_TIMER_FREEZE_TRIPLE_JUMP, kTripleStatuses),
    STATUS(SETTING_TIMER_FREEZE_SPIN_JUMP, kSpinStatuses),
    STATUS(SETTING_TIMER_FREEZE_LEDGE_GRAB, kLedgeStatuses),
    STATUS(SETTING_TIMER_FREEZE_WALL_KICK, kWallStatuses),
    STATUS(SETTING_TIMER_FREEZE_ROPE_JUMP, kRopeStatuses),
    STATUS(SETTING_TIMER_FREEZE_BOUNCE, kBounceStatuses),
    STATUS(SETTING_TIMER_FREEZE_JUMP, kJumpStatuses),
    STATUS(SETTING_TIMER_FREEZE_DIVE, kDiveStatuses),
    STATUS(SETTING_TIMER_FREEZE_DOUBLE_JUMP, kDoubleStatuses),
    STATUS(SETTING_TIMER_FREEZE_DIVE_ROLLOUT, kDiveRolloutStatuses),
    STATUS(SETTING_TIMER_FREEZE_DIVE_GETUP, kDiveGetupStatuses),
  };

#undef STATUS

  const u32 kStatusSite    = SUSAMUNE_MEM1_ADDR(0x801335B8u, 0x802541C8u, 0x8024BF54u);
  const int kStatusCaveMax = 48;

  template <u32 N>
  constexpr int statusValueWords(const u32 (&statuses)[N]) {
    int words = 0;
    for (u32 i = 0; i < N; i++) {
      words += statuses[i] < 0x10000u ? 1 : 3;
    }
    return words;
  }

  constexpr int kStatusCount =
    sizeof(kPutStatuses) / sizeof(u32) + sizeof(kTripleStatuses) / sizeof(u32) +
    sizeof(kSpinStatuses) / sizeof(u32) + sizeof(kLedgeStatuses) / sizeof(u32) +
    sizeof(kWallStatuses) / sizeof(u32) + sizeof(kRopeStatuses) / sizeof(u32) +
    sizeof(kBounceStatuses) / sizeof(u32) + sizeof(kJumpStatuses) / sizeof(u32) +
    sizeof(kDiveStatuses) / sizeof(u32) + sizeof(kDoubleStatuses) / sizeof(u32) +
    sizeof(kDiveRolloutStatuses) / sizeof(u32) +
    sizeof(kDiveGetupStatuses) / sizeof(u32);
  constexpr int kStatusMaxWords =
    statusValueWords(kPutStatuses) + statusValueWords(kTripleStatuses) +
    statusValueWords(kSpinStatuses) + statusValueWords(kLedgeStatuses) +
    statusValueWords(kWallStatuses) + statusValueWords(kRopeStatuses) +
    statusValueWords(kBounceStatuses) + statusValueWords(kJumpStatuses) +
    statusValueWords(kDiveStatuses) + statusValueWords(kDoubleStatuses) +
    statusValueWords(kDiveRolloutStatuses) + statusValueWords(kDiveGetupStatuses) +
    (kStatusCount - 1) + 6;
  static_assert(kStatusMaxWords <= kStatusCaveMax,
                "enabled QFT status freezes exceed their generated cave");

  u32 sStatusCave[kStatusCaveMax];
  u32 sStatusSignature;
  u32 sGuardedHookMask;

  int sDuration = -1;

  void flushCode(void *addr, u32 size) {
    DCFlushRange(addr, size);
    ICInvalidateRange(addr, size);
  }

  void installCave(u32 site, u32 *cave, int words) {
    u32 back        = reinterpret_cast<u32>(&cave[words - 1]);
    cave[words - 1] = branchWord(back, site + 4);
    flushCode(cave, words * 4);
    writeGameCode(site, branchWord(site, reinterpret_cast<u32>(&cave[0])));
  }

  void ensureWord(u32 addr, u32 word) {
    if (*reinterpret_cast<volatile u32 *>(addr) != word) {
      writeGameCode(addr, word);
    }
  }

  void ensureCoreHooks() {
    for (u32 i = 0; i < sizeof(kCoreHooks) / sizeof(kCoreHooks[0]); i++) {
      const CoreHook &hook = kCoreHooks[i];
      ensureWord(hook.site, branchWord(hook.site, reinterpret_cast<u32>(&hook.cave[0])));
    }

    // A legacy QFT Gecko code re-installs its renderer every handler pass.
    // Keep its timing caves harmless but restore the displaced setup2D call
    // so only the native presentations are visible.
    ensureWord(kLegacyRenderSite, kLegacyRenderOriginal);
  }

  void pointAtFreezer(u32 *cave) {
    u32 addr = reinterpret_cast<u32>(&sFreezeCave[0]);
    cave[0]  = 0x3D800000u | (addr >> 16);      // lis r12,freezer@h
    cave[1]  = 0x618C0000u | (addr & 0xFFFFu);  // ori r12,r12,freezer@l
  }

  int freezeDuration() {
    u8 choice = gSettings.get(SETTING_TIMER_FREEZE_DURATION);
    if (choice >= sizeof(kFreezeFrames) / sizeof(kFreezeFrames[0])) {
      choice = 0;
    }
    return kFreezeFrames[choice];
  }

  u32 statusSignature(int duration) {
    if (duration == 0)
      return 0;
    u32 signature = 0;
    for (u32 i = 0; i < sizeof(kStatusFreezes) / sizeof(kStatusFreezes[0]); i++) {
      if (gSettings.getBool(kStatusFreezes[i].setting)) {
        signature |= 1u << i;
      }
    }
    return signature;
  }

  void appendStatusCompare(u32 status, int index, int &n) {
    u32 cr = index ? 0x00800000u : 0;
    if (status < 0x10000u) {
      sStatusCave[n++] = 0x281D0000u | cr | status;
    } else {
      sStatusCave[n++] = 0x3C000000u | (status >> 16);
      sStatusCave[n++] = 0x60000000u | (status & 0xFFFFu);
      sStatusCave[n++] = 0x7C1D0040u | cr;
    }
    if (index) {
      sStatusCave[n++] = 0x4C423382u;  // cr0.eq |= cr1.eq
    }
  }

  void rebuildStatusHook(u32 signature) {
    writeGameCode(kStatusSite, 0x38000000u);  // retail li r0,0
    if (signature == 0) {
      sStatusSignature = 0;
      return;
    }

    int n           = 0;
    int comparisons = 0;
    for (u32 i = 0; i < sizeof(kStatusFreezes) / sizeof(kStatusFreezes[0]); i++) {
      if (!(signature & (1u << i)))
        continue;
      const StatusFreeze &event = kStatusFreezes[i];
      for (int j = 0; j < event.count; j++) {
        appendStatusCompare(event.statuses[j], comparisons++, n);
      }
    }

    u32 freezer      = reinterpret_cast<u32>(&sFreezeCave[0]);
    sStatusCave[n++] = 0x3D800000u | (freezer >> 16);
    sStatusCave[n++] = 0x618C0000u | (freezer & 0xFFFFu);
    sStatusCave[n++] = 0x7D8803A6u;
    sStatusCave[n++] = 0x4D820021u;  // beqlrl
    sStatusCave[n++] = 0x38000000u;  // displaced li r0,0
    if (n >= kStatusCaveMax)
      return;
    sStatusCave[n] = branchWord(reinterpret_cast<u32>(&sStatusCave[n]), kStatusSite + 4);
    n++;

    flushCode(sStatusCave, n * 4);
    writeGameCode(kStatusSite, branchWord(kStatusSite, reinterpret_cast<u32>(&sStatusCave[0])));
    sStatusSignature = signature;
  }

  void applyFreezeConfig() {
    int duration = freezeDuration();
    if (duration != sDuration) {
      sDuration        = duration;
      sFreezeCave[4]   = 0x39600000u | (duration & 0xFFFFu);
      sBlueCoinCave[6] = 0x38000000u | (duration & 0xFFFFu);
      flushCode(sFreezeCave, sizeof(sFreezeCave));
      flushCode(sBlueCoinCave, sizeof(sBlueCoinCave));
    }

    for (int i = 0; i < kNumFreezeHooks; i++) {
      const FreezeHook &hook = kFreezeHooks[i];
      bool on                = duration != 0 && gSettings.getBool(hook.setting);

      u32 target = reinterpret_cast<u32>(&sFreezeCave[0]);
      if (hook.kind == FREEZE_GUARDED_CAVE &&
          !(sGuardedHookMask & (1u << i))) {
        continue;
      }
      if (hook.kind != FREEZE_DIRECT) {
        target = reinterpret_cast<u32>(&hook.cave[0]);
      }
      ensureWord(hook.site, on ? branchWord(hook.site, target) : hook.original);
    }

    u32 signature = statusSignature(duration);
    if (signature != sStatusSignature) {
      rebuildStatusHook(signature);
    } else {
      u32 word = signature ? branchWord(kStatusSite, reinterpret_cast<u32>(&sStatusCave[0]))
                           : 0x38000000u;
      ensureWord(kStatusSite, word);
    }
  }

  s32 clampQf(s32 value) {
    if (value < 0)
      return 0;
    if (value > kMaxQf)
      return kMaxQf;
    return value;
  }

  s32 liveQf() {
    if (sState->stopped)
      return clampQf(sState->offsetQf);
    if (!gpMarDirector || gpMarDirector != sStageDirector) {
      return clampQf(sState->offsetQf);
    }
    return clampQf(sState->offsetQf + gpMarDirector->unk5C);
  }

  s32 compactQf() {
    if (sState->stopped)
      return clampQf(sState->offsetQf);
    if (sState->freezeFrames != 0) {
      return clampQf(sState->offsetQf + sState->freezeQf);
    }
    return liveQf();
  }

  s32 sunshineQf() {
    // Loading zones hold the visible split while the real clock carries on.
    if (!sState->stopped && sState->freezeFrames == -1)
      return clampQf(sState->offsetQf + sState->freezeQf);
    return liveQf();
  }

  s32 qfToMillis(s32 qf) { return (qf * 1001) / 120; }

  s32 qfToRoundedCentis(s32 qf) {
    // One QF is 1001/120 ms. Add half a centisecond before dividing so the
    // Sunshine HUD's last digit rounds instead of truncating QFT precision.
    s32 centis = (qf * 1001 + 600) / 1200;
    if (centis > 599999)
      centis = 599999;
    return centis;
  }

  bool finalStop() { return sState->stopped && sState->stopReason != STOP_NONE; }

  void hideBigTimer() {
    if (!gpMarDirector || !gpMarDirector->mGCConsole)
      return;
    gpMarDirector->mGCConsole->startDisappearTimer();
    sBigShown = false;
  }

  void updateBigTimer() {
    if (!sStageReady || !gpMarDirector || gpMarDirector != sStageDirector ||
        !gpMarDirector->mGCConsole) {
      return;
    }
    if (gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState < TMarDirector::STATE_GAME_STARTING) {
      return;
    }

    TGCConsole2 *console = gpMarDirector->mGCConsole;
    if (console->mIsTimerMoving) {
      sRetailTimerOwned = true;
      sBigShown = false;
    }
    if (sRetailTimerOwned)
      return;

    u8 mode   = gSettings.get(SETTING_TIMER_SUNSHINE_VISIBILITY);
    bool want = mode == SUNSHINE_ALWAYS || (mode == SUNSHINE_SHINE_ONLY && finalStop());

    if (!want) {
      if (sBigShown)
        hideBigTimer();
      return;
    }

    if (!sBigShown) {
      gpMarDirector->mGCConsole->startAppearTimer(0, 0);
      sBigShown = true;
    }
    gpMarDirector->mGCConsole->setTimer(qfToRoundedCentis(sunshineQf()));
  }

}  // namespace

QFTTimer gQFTTimer;

void QFTTimer::init() {
  sState->stopped      = 0;
  sState->restart      = 1;
  sState->stopReason   = STOP_NONE;
  sState->pad          = 0;
  sState->offsetQf     = -4;
  sState->freezeQf     = 0;
  sState->freezeFrames = 0;
  *sDeathQf            = -1;
  *sPlantQf            = -1;
  *sTransitionTarget   = 0xFFFF;

  sStageReady     = false;
  sStagePending   = false;
  sResetRequested = true;
  sFinalConsumed = false;
  sBigShown       = false;
  sRetailTimerOwned = false;
  sAttemptSerial  = 0;
  sStageDirector  = nullptr;
  sHaveSavedState = false;

  for (u32 i = 0; i < sizeof(kCoreHooks) / sizeof(kCoreHooks[0]); i++) {
    const CoreHook &hook = kCoreHooks[i];
    installCave(hook.site, hook.cave, hook.words);
  }

  pointAtFreezer(sTakeCave);
  pointAtFreezer(sDropCave);
  pointAtFreezer(sPeteyWakeupCave);
  pointAtFreezer(sEelActivateCave);
  pointAtFreezer(sEelToothCave);
  installCave(kFreezeHooks[9].site, sTakeCave, sizeof(sTakeCave) / sizeof(sTakeCave[0]));
  installCave(kFreezeHooks[10].site, sDropCave, sizeof(sDropCave) / sizeof(sDropCave[0]));
  installCave(kFreezeHooks[2].site, sBlueCoinCave,
              sizeof(sBlueCoinCave) / sizeof(sBlueCoinCave[0]));

  sGuardedHookMask = 0;
  for (int i = 0; i < kNumFreezeHooks; i++) {
    const FreezeHook &hook = kFreezeHooks[i];
    if (hook.kind != FREEZE_GUARDED_CAVE ||
        *reinterpret_cast<volatile u32 *>(hook.site) != hook.original) {
      continue;
    }
    installCave(hook.site, hook.cave, hook.words);
    writeGameCode(hook.site, hook.original);
    sGuardedHookMask |= 1u << i;
  }

  // installCave writes the special-hook sites. Restore retail until their
  // individual settings are applied below.
  writeGameCode(kFreezeHooks[9].site, kFreezeHooks[9].original);
  writeGameCode(kFreezeHooks[10].site, kFreezeHooks[10].original);
  writeGameCode(kFreezeHooks[2].site, kFreezeHooks[2].original);

  sStatusSignature = 0;
  sDuration        = -1;
  applyFreezeConfig();
}

void QFTTimer::beginFrame() {
  if (sStagePending && gpMarDirector == sStageDirector && gpMarDirector->_260 != 0 &&
      gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING) {
    sStagePending = false;
    sStageReady   = true;
    if (sResetRequested || sState->restart) {
      sState->stopped    = 0;
      sState->restart    = 0;
      sState->stopReason = STOP_NONE;
      sState->offsetQf   = -4;
      sAttemptSerial++;
    }
    sResetRequested = false;
  }

  ensureCoreHooks();
  applyFreezeConfig();
  if (sState->freezeFrames > 0) {
    sState->freezeFrames--;
  }
}

void QFTTimer::onStageSetup(TMarDirector *director) {
  // File select and the plaza are boundaries between timed attempts. Keep an
  // explicit request outside QFT scratch because game transition hooks also
  // write the scratch restart byte during stage setup.
  u8 previousArea = gpApplication.mPrevScene.mAreaID;
  if (sState->stopped || previousArea == TGameSequence::AREA_OPTION ||
      previousArea == TGameSequence::AREA_DOLPIC) {
    sResetRequested = true;
  }

  sState->freezeFrames = 0;
  *sDeathQf            = -1;
  *sPlantQf            = -1;
  *sTransitionTarget   = 0xFFFF;
  sFinalConsumed = false;
  sStageReady    = false;
  sStagePending  = true;
  sBigShown      = false;
  sRetailTimerOwned = false;
  sStageDirector = director;
}

void QFTTimer::update() { updateBigTimer(); }

void QFTTimer::draw(Menu *menu) const {
  if (!menu || !sStageReady || !gpMarDirector || gpMarDirector != sStageDirector) {
    return;
  }

  u8 mode = gSettings.get(SETTING_TIMER_QFT_VISIBILITY);
  if (mode == COMPACT_HIDDEN)
    return;

  bool show = mode == COMPACT_ALWAYS;
  if (mode == COMPACT_ON_FREEZE) {
    if (sRetailTimerOwned) {
      // The retail timer owns the large panel; keep the full IL clock visible.
      show = true;
    } else if (sState->stopped) {
      // The Sunshine result owns Shine/Bowser finishes unless the user
      // hid it; avoid showing the same final time twice.
      show = gSettings.get(SETTING_TIMER_SUNSHINE_VISIBILITY) == SUNSHINE_HIDDEN;
    } else {
      show = sState->freezeFrames != 0;
    }
  }
  if (!show)
    return;

  s32 millis    = qfToMillis(compactQf());
  int minutes   = (int)(millis / 60000);
  int seconds   = (int)((millis / 1000) % 60);
  int remainder = (int)(millis % 1000);

  char text[20];
  snprintf(text, sizeof(text), "%d:%02d.%03d", minutes, seconds, remainder);

  const int size = 20;
  const int x    = 16;
  const int y    = 416;
  const int w    = Menu::textWidth(text, size);
  menu->fillBox(x - 2, y - 2, w + 4, size + 4, JUtility::TColor(0, 0, 0, 128));
  menu->drawText(text, x, y, size, size, JUtility::TColor(255, 255, 255, 255));
}

void QFTTimer::requestReset() {
  sResetRequested      = true;
  sFinalConsumed       = false;
  sState->restart      = 1;
  sState->freezeFrames = 0;
  *sDeathQf            = -1;
  *sPlantQf            = -1;
  *sTransitionTarget   = 0xFFFF;
}

u32 QFTTimer::attemptSerial() const { return sAttemptSerial; }

bool QFTTimer::consumeTransition(s32 *qf, u16 *target) {
  if (!qf || !target || *sTransitionTarget == 0xFFFF) {
    return false;
  }
  const s32 result = clampQf(sState->offsetQf + *sTransitionQf);
  *target = *sTransitionTarget;
  *sTransitionTarget = 0xFFFF;
  sState->offsetQf = result;
  sState->stopped = 1;
  sState->stopReason = STOP_CUSTOM;
  sFinalConsumed = true;
  *qf = result;
  return true;
}

static bool consumeStop(s32 *qf, u8 reason) {
  if (!qf || !sStageReady || gpMarDirector != sStageDirector ||
      sFinalConsumed || !sState->stopped || sState->stopReason != reason) {
    return false;
  }
  sFinalConsumed = true;
  *qf = clampQf(sState->offsetQf);
  return true;
}

bool QFTTimer::consumeShine(s32 *qf) { return consumeStop(qf, STOP_SHINE); }

bool QFTTimer::consumeBowser(s32 *qf) { return consumeStop(qf, STOP_BOWSER); }

static bool consumeCustomEvent(volatile s32 *eventQf, s32 *qf) {
  if (!qf || !sStageReady || gpMarDirector != sStageDirector ||
      sFinalConsumed || *eventQf < 0) {
    return false;
  }
  const s32 result = clampQf((sState->offsetQf + *eventQf + 4) & ~3);
  *eventQf          = -1;
  sState->offsetQf  = result;
  sState->stopped   = 1;
  sState->stopReason = STOP_CUSTOM;
  sFinalConsumed    = true;
  *qf               = result;
  return true;
}

bool QFTTimer::consumeCustom(bool death, s32 *qf) {
  return consumeCustomEvent(death ? sDeathQf : sPlantQf, qf);
}

void QFTTimer::onSavestateSaved() {
  sSavedState.stopped      = sState->stopped;
  sSavedState.restart      = sState->restart;
  sSavedState.stopReason   = sState->stopReason;
  sSavedState.pad          = 0;
  sSavedState.offsetQf     = sState->offsetQf;
  sSavedState.freezeQf     = sState->freezeQf;
  sSavedState.freezeFrames = sState->freezeFrames;
  sSavedFinalConsumed      = sFinalConsumed;
  sSavedRetailTimerOwned    = sRetailTimerOwned;
  sSavedAttemptSerial      = sAttemptSerial;
  sHaveSavedState          = true;
}

void QFTTimer::onSavestateLoaded() {
  if (!sHaveSavedState)
    return;
  sState->stopped      = sSavedState.stopped;
  sState->restart      = sSavedState.restart;
  sState->stopReason   = sSavedState.stopReason;
  sState->pad          = 0;
  sState->offsetQf     = sSavedState.offsetQf;
  sState->freezeQf     = sSavedState.freezeQf;
  sState->freezeFrames = sSavedState.freezeFrames;
  sFinalConsumed       = sSavedFinalConsumed;
  sRetailTimerOwned     = sSavedRetailTimerOwned;
  sAttemptSerial       = sSavedAttemptSerial;
  // Hook scratch is outside the snapshot. Drop events from the abandoned
  // future so a pre-finish state waits for the endpoint again.
  *sDeathQf            = -1;
  *sPlantQf            = -1;
  *sTransitionTarget   = 0xFFFF;
}

#undef QFT_OFF
#undef QFT_HA
#undef PPC_LO
#undef PPC_HA
