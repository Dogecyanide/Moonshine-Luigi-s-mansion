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
]

# Base address to link code against, i.e. where we will insert the code.
base_addr = {'jp': 0x80414020, 'us': None, 'pal': None}