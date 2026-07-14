from enum import Enum

class PatchType(Enum):
    B = 1
    BL = 2
    W32 = 3

patches = [
    {'jp': 0x800ec6c4, 'us': None, 'pal': None, 'sym': 'onUpdateGameMode', 'type': PatchType.BL},
    {'jp': 0x800f9b64, 'us': None, 'pal': None, 'sym': 'onUpdate', 'type': PatchType.BL, 'nop_count': 3},
    {'jp': 0x800ece3c, 'us': None, 'pal': None, 'sym': 'onSetup', 'type': PatchType.BL},
    {'jp': 0x800f9d10, 'us': None, 'pal': None, 'sym': 'afterDraw', 'type': PatchType.BL},
#   {'jp': 0x800fa110, 'us': None, 'pal': None, 'sym': 'onFinishAppState', 'type': PatchType.BL},
    # insert NOPs to speed up boot process
    {'jp': 0x800fadf4, 'us': None, 'pal': None, 'val': 0x60000000, 'type': PatchType.W32},
    {'jp': 0x800fae08, 'us': None, 'pal': None, 'val': 0x60000000, 'type': PatchType.W32},
    # Report a raised arena floor so the root heap starts above the mod's
    # region (see getArenaLo in src/main.cpp). Replaces OSGetArenaLo's body.
    {'jp': 0x8008dcbc, 'us': None, 'pal': None, 'sym': 'getArenaLo', 'type': PatchType.B},
]

# The mod is linked into a region carved from the BOTTOM of the game's heap
# arena, at __ArenaLo. getArenaLo() (hooked onto OSGetArenaLo above) makes the
# root heap start at __OSArenaLo + mod_region_size, leaving [__ArenaLo,
# __ArenaLo + mod_region_size) free for the mod's code + data. The top of the
# arena is deliberately left alone: the apploader stores the FST there. The
# game's stack is untouched.
arena_lo = {'jp': 0x80426020, 'us': None, 'pal': None}  # __ArenaLo, from maps/<vers>.map

# Size of the carved region. Comes out of the ~19 MiB heap, so it can be
# generous; the mod must fit within it. MUST match kArenaReserve in main.cpp.
mod_region_size = 0x8000

# Base address to link code against, i.e. where we insert the code.
base_addr = {v: a for v, a in arena_lo.items()}

# Full disc game id (bytes 0..3 of the disc header), used by the launcher to
# apply the injection only to the intended game.
game_id = {'jp': 0x474D534A, 'us': None, 'pal': None}  # "GMSJ"

# Metadata for the launcher's meta.xml (region label + disc image name).
region = {'jp': 'JP', 'us': 'US', 'pal': 'PAL'}
disc_name = {'jp': 'GMSJ01', 'us': 'GMSE01', 'pal': 'GMSP01'}
