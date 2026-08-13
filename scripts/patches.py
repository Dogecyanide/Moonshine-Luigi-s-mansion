from enum import Enum

class PatchType(Enum):
    B = 1
    BL = 2
    W32 = 3

patches = [
    # changeState__12TMarDirectorFv + 0x2c0: updateGameMode() call.
    {'jp': 0x800ec6c4, 'us': 0x80299140, 'pal': 0x80290fd8, 'sym': 'onUpdateGameMode', 'type': PatchType.BL},
    # gameLoop__12TApplicationFv + 0x210: director->direct() call.
    {'jp': 0x800f9b64, 'us': 0x802a6160, 'pal': 0x8029e070, 'sym': 'onUpdate', 'type': PatchType.BL, 'nop_count': 3},
    # direct__12TMarDirectorFv + 0x80: setupObjects() call.
    {'jp': 0x800ece3c, 'us': 0x802998b8, 'pal': 0x80291750, 'sym': 'onSetup', 'type': PatchType.BL},
    # TMario::winDemo + 0x88: preserve fireGetStar while publishing one
    # attempt-counter event. No Shine Get Animation temporarily replaces this
    # call site and publishes the same event from its cave.
    {'jp': 0x80120540, 'us': 0x80241400, 'pal': 0x8023918c, 'sym': 'onFireGetStar', 'type': PatchType.BL},
    # gameLoop__12TApplicationFv + 0x3bc: THPPlayerDrawDone() call.
    {'jp': 0x800f9d10, 'us': 0x802a630c, 'pal': 0x8029e21c, 'sym': 'afterDraw', 'type': PatchType.BL},
    # main + 0x1c: the gpApplication.initialize() call. main() is just
    # initialize(); proc(); finalize(), and everything the game does -- the
    # Nintendo logo, the progressive-mode prompt, the title screen, gameplay --
    # happens inside proc(). Hooking the initialize() call gives us the earliest
    # point at which the heap and gamepads exist but no app state has run yet,
    # which is where settings must be live: featuresApply() runs from the
    # gameLoop hook, and proc() runs gameLoop for the logo and title states too,
    # so anything later leaves every feature reading zeroed BSS through boot.
    # main is at 0x80005600 with size 0x44 in all three region maps, so the call
    # site is main+0x1c everywhere.
    {'jp': 0x8000561c, 'us': 0x8000561c, 'pal': 0x8000561c, 'sym': 'onAppInit', 'type': PatchType.BL},
    # CARDMount: retry only an encoding mismatch with the opposite ANSI/SJIS
    # standard, selecting the format actually recorded in the mounted card.
    {'jp': 0x800a31fc, 'us': 0x803588dc, 'pal': 0x80350afc,
     'sym': 'susamuneCardMount', 'type': PatchType.B},
    # evGetSystemFlag: virtualize the box-game flags read by Delfino's
    # Sunscript without changing the actual save data.
    {'jp': 0x800e64e8, 'us': 0x80292fa0, 'pal': 0x8028ad2c,
     'sym': 'susamuneGetSystemFlag', 'type': PatchType.BL},
    # TYoshi::ride and TItemNozzle::touchPlayer: preserve the retail event,
    # then optionally request the same save UI for repeat pickups.
    {'jp': 0x8014f818, 'us': 0x802704bc, 'pal': 0x80268248,
     'sym': 'susamuneFireRideYoshi', 'type': PatchType.BL},
    {'jp': 0x80193e68, 'us': 0x801bbe80, 'pal': 0x801b3d38,
     'sym': 'susamuneFireGetNozzle', 'type': PatchType.BL},
#   {'jp': 0x800fa110, 'us': ..., 'pal': ..., 'sym': 'onFinishAppState', 'type': PatchType.BL},
    # insert NOPs to speed up boot process
    # initialize__12TApplicationFv + 0x2c / +0x40.
    {'jp': 0x800fadf4, 'us': 0x802a73f0, 'pal': 0x8029f46c, 'val': 0x60000000, 'type': PatchType.W32},
    {'jp': 0x800fae08, 'us': 0x802a7404, 'pal': 0x8029f480, 'val': 0x60000000, 'type': PatchType.W32},
    # Report a raised arena floor so the root heap starts above the mod's
    # region (see getArenaLo in src/main.cpp). Replaces OSGetArenaLo's body.
    {'jp': 0x8008dcbc, 'us': 0x8034339c, 'pal': 0x8033b51c, 'sym': 'getArenaLo', 'type': PatchType.B},
    # Replace TSpineEnemy::goToRandomNextGraphNode with a native wrapper. It
    # preserves the original path for every enemy, only selecting a fixed
    # Chomplet / Chain Chomp graph node when Pattern Selector is enabled.
    {'jp': 0x8024f2dc, 'us': 0x8003b6ac, 'pal': 0x8003b4fc,
     'sym': 'susamuneGoToRandomNextGraphNode', 'type': PatchType.B},
]

# The mod is linked into a region carved from the BOTTOM of the game's heap
# arena, at __ArenaLo. getArenaLo() (hooked onto OSGetArenaLo above) adds
# arena_reserve to the runtime __OSArenaLo, leaving [__ArenaLo,
# __ArenaLo + mod_region_size) free for the mod's code + data. The top of the
# arena is deliberately left alone: the apploader stores the FST there. The
# game's stack is untouched.
arena_lo = {
    'jp': 0x80426020,
    'us': 0x80429800,
    'pal': 0x80420d60,
}  # __ArenaLo, from maps/<vers>.map

# Size of the carved region. Comes out of the ~19 MiB heap, so it can be
# generous; the mod must fit within it. MUST match
# SUSAMUNE_MOD_REGION_SIZE in mod_bin.h.
mod_region_size = 0x20000

# Tail of the region reserved for the asm caves' fixed-address scratch, which
# the blob must not grow into. MUST match SUSAMUNE_SCRATCH in mod_bin.h.
mod_scratch_size = 0x40

# Ceiling enforced on the linked blob by every link_mod.py mode.
mod_blob_max_size = mod_region_size - mod_scratch_size

# OSInit returns the debug stack to the arena when no debug monitor is present
# (BI2DebugFlag < 2), so the runtime __OSArenaLo is this far BELOW the
# __ArenaLo the blob links at. Uniform across JP/US/PAL; link_mod.py verifies
# it against _stack_addr/__ArenaLo in the map for the version being built.
debug_stack_size = 0x2000

# What getArenaLo() adds to __OSArenaLo, and therefore the amount every
# bottom-anchored heap allocation shifts up by. MUST match
# SUSAMUNE_ARENA_RESERVE_SIZE in mod_bin.h.
arena_reserve = mod_region_size + debug_stack_size

# Base address to link code against, i.e. where we insert the code.
base_addr = {v: a for v, a in arena_lo.items()}

# Full disc game id (bytes 0..3 of the disc header), used by the launcher to
# apply the injection only to the intended game.
game_id = {
    'jp': 0x474D534A,   # "GMSJ"
    'us': 0x474D5345,   # "GMSE"
    'pal': 0x474D5350,  # "GMSP"
}

# Metadata for the launcher's meta.xml (region label + disc image name).
region = {'jp': 'JP', 'us': 'US', 'pal': 'PAL'}
disc_name = {'jp': 'GMSJ01', 'us': 'GMSE01', 'pal': 'GMSP01'}
