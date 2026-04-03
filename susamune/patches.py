from enum import Enum

class PatchType(Enum):
    B = 1
    BL = 2 
    W32 = 3

patches = [
    {'jp': 0x800ec6c4, 'us': 0x00000000, 'pal': 0x00000000, 'sym': 'onUpdateGameMode', 'type': PatchType.BL},
    {'jp': 0x800f9b64, 'us': 0x00000000, 'pal': 0x00000000, 'sym': 'onUpdate', 'type': PatchType.BL, 'nop_count': 3},
    {'jp': 0x800ece3c, 'us': 0x00000000, 'pal': 0x00000000, 'sym': 'onSetup', 'type': PatchType.BL},
    {'jp': 0x800f9d10, 'us': 0x00000000, 'pal': 0x00000000, 'sym': 'afterDraw', 'type': PatchType.BL},
    # insert NOPs to speed up boot process
    {'jp': 0x800fadf4, 'us': 0x00000000, 'pal': 0x00000000, 'val': 0x60000000, 'type': PatchType.W32},
    {'jp': 0x800fae08, 'us': 0x00000000, 'pal': 0x00000000, 'val': 0x60000000, 'type': PatchType.W32},
]