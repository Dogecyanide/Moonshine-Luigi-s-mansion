#ifndef SUSAMUNE_ADDRESSES_HXX
#define SUSAMUNE_ADDRESSES_HXX

// Region-specific MEM1 layout for the supported retail revisions. Keep every
// game-memory address here; see the corresponding maps/<version>.map file.
// The build selects exactly one SUSAMUNE_VERSION_* definition in CMake.
#if (defined(SUSAMUNE_VERSION_JP) + defined(SUSAMUNE_VERSION_US) + \
     defined(SUSAMUNE_VERSION_PAL)) != 1
#error "Define exactly one SUSAMUNE_VERSION_JP, SUSAMUNE_VERSION_US, or SUSAMUNE_VERSION_PAL"
#endif

#if defined(SUSAMUNE_VERSION_JP)
#define SUSAMUNE_MEM1_ADDR(jp, us, pal) (jp)
#define SUSAMUNE_GAME_VERSION 1u
#elif defined(SUSAMUNE_VERSION_US)
#define SUSAMUNE_MEM1_ADDR(jp, us, pal) (us)
#define SUSAMUNE_GAME_VERSION 2u
#elif defined(SUSAMUNE_VERSION_PAL)
#define SUSAMUNE_MEM1_ADDR(jp, us, pal) (pal)
#define SUSAMUNE_GAME_VERSION 3u
#endif

// OS and TApplication globals.
#define SUSAMUNE_ADDR_OS_ARENA_LO \
    SUSAMUNE_MEM1_ADDR(0x80408d08u, 0x8040ce48u, 0x804045a8u)
#define SUSAMUNE_ADDR_APPLICATION \
    SUSAMUNE_MEM1_ADDR(0x803e6000u, 0x803e9700u, 0x803e10c0u)
#define SUSAMUNE_ADDR_APPLICATION_GAMEPAD(index) \
    (SUSAMUNE_ADDR_APPLICATION + 0x20u + 4u * (index))
#define SUSAMUNE_ADDR_APPLICATION_FADER \
    (SUSAMUNE_ADDR_APPLICATION + 0x34u)

// Mutable static storage that belongs to the game, not JSystem or the OS.
//
// These are deliberately NOT linker-section ends. In each retail build the
// game globals are followed by JSystem / runtime globals in the same section;
// restoring those would desynchronise the renderer, DSP, heap, and OS state
// from their live hardware state. The end values are the first system-owned
// symbol in the corresponding map section.
#define SUSAMUNE_ADDR_GAME_BSS_START \
    SUSAMUNE_MEM1_ADDR(0x803f1c50u, 0x803fb2a0u, 0x803f2a40u)
#define SUSAMUNE_ADDR_GAME_BSS_END \
    SUSAMUNE_MEM1_ADDR(0x80400b8cu, 0x803fd548u, 0x803f4ce8u)
#define SUSAMUNE_ADDR_GAME_SDATA_START \
    SUSAMUNE_MEM1_ADDR(0x80409008u, 0x8040c778u, 0x80403ed8u)
#define SUSAMUNE_ADDR_GAME_SDATA_END \
    SUSAMUNE_MEM1_ADDR(0x804097acu, 0x8040cc00u, 0x80404360u)
#define SUSAMUNE_ADDR_GAME_SBSS_START \
    SUSAMUNE_MEM1_ADDR(0x8040a208u, 0x8040e090u, 0x80405758u)
#define SUSAMUNE_ADDR_GAME_SBSS_END \
    SUSAMUNE_MEM1_ADDR(0x8040b45cu, 0x8040e228u, 0x80405900u)
#define SUSAMUNE_ADDR_LIBC_RAND_SEED \
    SUSAMUNE_MEM1_ADDR(0x80408cf0u, 0x8040ce30u, 0x80404590u)

// Root-heap object pointers. The pointer variables are static, while their
// targets live outside the stage heap and must be captured separately.
#define SUSAMUNE_ADDR_RUMBLE_MANAGER \
    SUSAMUNE_MEM1_ADDR(0x8040a248u, 0x8040e0d0u, 0x80405798u)
#define SUSAMUNE_ADDR_FLAG_MANAGER_INSTANCE \
    SUSAMUNE_MEM1_ADDR(0x8040a290u, 0x8040e160u, 0x80405828u)
#define SUSAMUNE_ADDR_TIME_REC_INSTANCE \
    SUSAMUNE_MEM1_ADDR(0x8040a2f8u, 0x8040e1c8u, 0x804058a0u)

// `sGameInit`, the static bitfield TApplication::gameLoop uses to leave the
// boot-logo state (Application.cpp). Bit 0 = the logo director reported it is
// finished, bit 1 = the async setup thread has been joined; the state advances
// only once both are set. Intro Skip pre-sets bit 0 so the logo is never
// directed -- see onAppInit in main.cpp.
#define SUSAMUNE_ADDR_GAME_INIT_FLAGS \
    SUSAMUNE_MEM1_ADDR(0x8040a2c0u, 0x8040e190u, 0x80405858u)

// TApplication::proc's app-state switch is a jump table indexed by mContext.
// These are the entry for the intro-movie state (4) and the address of the
// stage case (5) that entry is repointed at for Intro Skip -- see onAppInit.
// Both read out of the table in each region's DOL.
#define SUSAMUNE_ADDR_APP_STATE_JUMP_INTRO \
    SUSAMUNE_MEM1_ADDR(0x803b4084u, 0x803df434u, 0x803d6cf0u)
#define SUSAMUNE_ADDR_APP_PROC_STAGE_CASE \
    SUSAMUNE_MEM1_ADDR(0x800f9ea4u, 0x802a64a0u, 0x8029e3c0u)

// This is an optional Quick-Freeze practice-code heap flag, not a game-map symbol.
#define SUSAMUNE_ADDR_QF_TIMER_RESET \
    SUSAMUNE_MEM1_ADDR(0x817f00b3u, 0x817f00b3u, 0x817f00b3u)

// Scratch for the mod's asm caves. A cave can only reach a *fixed* address --
// it has no way to find a mod global -- so this is the top 16 bytes of the
// mod's own reserved arena window, [arena_lo, arena_lo + mod_region_size) from
// scripts/patches.py, which getArenaLo() keeps the game's heap out of. The
// blob starts at arena_lo and is far short of it (~0x6050 of 0x8000); if it
// ever grows into this, the manifest's size check is the thing to tighten.
//
// Do NOT put mod scratch in the practice codes' region at 0x817f0000+: that
// sits ABOVE __ArenaHi (0x81700000), where the apploader's FST and, on
// console, Nintendont's cheat/code-handler area live. It is only free when a
// .gct and its handler have been loaded and have lowered the arena top --
// susamune reserves nothing there.
#define SUSAMUNE_ADDR_MOD_SCRATCH \
    SUSAMUNE_MEM1_ADDR(0x8042e010u, 0x804317f0u, 0x80428d50u)

// One word: the frame of the last TShine::touchPlayer, for No Shine Get
// Animation's debounce (features.cpp).
#define SUSAMUNE_ADDR_SHINE_TOUCH_FRAME (SUSAMUNE_ADDR_MOD_SCRATCH + 0x0u)

#endif // SUSAMUNE_ADDRESSES_HXX
