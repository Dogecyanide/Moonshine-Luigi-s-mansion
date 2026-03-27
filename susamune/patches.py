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

cpp_syms = [
    ("void J2DFillBox(int x, int y, int w, int h, JUtility::TColor color)", 0x8003703c),
    ("char TMarDirector::updateGameMode()", 0x800eaf70),
    ("void TMarDirector::setupObjects()", 0x8010c3ac),
    ("J2DTextBox::$$ctor1(ResFONT const*, char const*)", 0x800193b0),
    ("void J2DTextBox::draw(int, int)", 0x80019be4),
    ("J2DOrthoGraph::$$ctor1(int,int,int,int)", 0x80036c14),
    ("J2DOrthoGraph::$$dtor()", 0x80011760),
    ("void J2DGrafContext::setup2D()", 0x80035228),
]