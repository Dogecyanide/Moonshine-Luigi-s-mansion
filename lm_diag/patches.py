"""GLMJ01 revision-0 hooks and the proven MEM1 diagnostic reservation.

This deliberately has no relationship to scripts/patches.py, which remains
the inherited Super Mario Sunshine patch table.  The launcher build consumes
this file only when LM_DIAGNOSTIC is enabled.
"""

from enum import Enum


class PatchType(Enum):
    B = 1
    BL = 2
    W32 = 3


# Every modifying write carries its clean-GLMJ01 word.  The launcher kernel
# preflights the complete set before copying any code or applying any branch,
# so a different revision fails closed rather than partly injecting.
patches = [
    # Replace OSGetArenaLo's two-instruction getter.  A plain branch preserves
    # the caller's LR, so getArenaLo() returns directly to the retail caller.
    {
        "lmj": 0x801D5B5C,
        "sym": "getArenaLo",
        "type": PatchType.B,
        "expected": 0x806DFF38,
    },
    # Wrap both GXCopyDisp calls inside LMChangeFrameBuffer.  They are the two
    # branches of the retail presenter and both pass the completed XFB in r3.
    # The wrapper calls the original copy, waits for it, then draws directly
    # into the 640x480 YUYV buffer with JUTDirectPrint plus a raw heartbeat.
    {
        "lmj": 0x8000776C,
        "sym": "diagnosticCopyDisp",
        "type": PatchType.BL,
        "expected": 0x481E8CF1,
    },
    {
        "lmj": 0x80007828,
        "sym": "diagnosticCopyDisp",
        "type": PatchType.BL,
        "expected": 0x481E8C35,
    },
]

# Extra clean-DOL signatures that are authenticated but left untouched.  These
# bind the payload to the complete retail getter, the main-loop presenter call,
# LM's final JUTDirectPrint framebuffer handoff, the port-1 PADRead path, and
# the complete four-instruction JUTException callback setter.
checks = [
    {"addr": 0x801D5B60, "expected": 0x4E800020},
    {"addr": 0x8000B62C, "expected": 0x4BFFC1BD},
    {"addr": 0x80007870, "expected": 0x481CCFC1},
    {"addr": 0x801D20B0, "expected": 0x387D0018},
    {"addr": 0x801D20B4, "expected": 0x48012849},
    {"addr": 0x801D4124, "expected": 0x800D1594},
    {"addr": 0x801D4128, "expected": 0x906D1594},
    {"addr": 0x801D412C, "expected": 0x7C030378},
    {"addr": 0x801D4130, "expected": 0x4E800020},
]


# GLMJ01's linked arena floor and the retail debug-stack gap were recovered
# directly from the clean DOL (SHA-1 722005ea9c1eab54b114f814734d8f327e5614ee).
arena_lo = {"lmj": 0x804B8400}
base_addr = dict(arena_lo)

# Exact tuple produced by Nintendont's Patch.c DOL parser.  DOLSize uses the
# 0xE4-byte dolhdr struct, not main.dol's 0x100-byte padded on-disc header.
dol_size = {"lmj": 0x00394924}
dol_min = {"lmj": 0x00003100}
dol_max = {"lmj": 0x004A6400}

mod_region_size = 0x80000
debug_stack_size = 0x2000
arena_reserve = mod_region_size + debug_stack_size

# Keep the transport limits identical to Moonshine's established 512 KiB
# layout.  The diagnostic itself is tiny and does not create an attachment
# heap; these values are capacity boundaries, not allocations.
mod_scratch_size = 0x40
mod_mem1_working_cap_size = 0x50000
mod_attachment_heap_offset = 0x50000
mod_attachment_heap_size = 0x20000
mod_blob_max_size = mod_mem1_working_cap_size
mod_file_max_size = 0x5F000
mod_write_count = sum(1 + patch.get("nop_count", 0) for patch in patches)

assert mod_attachment_heap_offset == mod_mem1_working_cap_size
assert (
    mod_attachment_heap_offset + mod_attachment_heap_size
    <= mod_region_size - mod_scratch_size
)
assert (mod_attachment_heap_offset | mod_attachment_heap_size) & 31 == 0

game_id = {"lmj": 0x474C4D4A}  # "GLMJ"
region = {"lmj": "JP"}
disc_name = {"lmj": "GLMJ01"}
