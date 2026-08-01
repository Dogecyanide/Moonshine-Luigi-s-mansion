"""Pack the mod manifest produced by `link_mod.py launcher` into mod_<vers>.bin,
the file the launcher ships next to boot.dol and loads at runtime.

Format is struct SusamuneModHeader from include/susamune/mod_bin.h: a 32-byte
big-endian header, then the code blob, then the (addr, val) hook writes. The
writes travel with the code because their addresses are version-specific -- a
blob on its own is not applicable to anything.

Usage: gen_mod_bin.py MANIFEST.json -o mod_jp.bin
"""
import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC = 0x534D4F44  # 'SMOD'
VERSION = 1
HEADER_SIZE = 32
# SUSAMUNE_MEM2_MODBIN_SIZE (mem2_map.h): the loader refuses a larger file, so
# fail the build instead of shipping one that cannot be staged.
STAGING_WINDOW_SIZE = 0x20000


def build_mod_bin(manifest):
    code = bytes.fromhex(manifest["code"])
    if len(code) % 4:
        raise ValueError("code blob is not word-aligned")

    writes = manifest["writes"]
    body = code + b"".join(
        struct.pack(">II", addr & 0xFFFFFFFF, val & 0xFFFFFFFF) for addr, val in writes)

    header = struct.pack(
        ">8I",
        MAGIC,
        VERSION,
        manifest["game_id"],
        manifest["base_addr"],
        len(code),
        len(writes),
        manifest.get("region_reserve", 0),
        HEADER_SIZE + len(body),
    )
    assert len(header) == HEADER_SIZE
    total = HEADER_SIZE + len(body)
    if total > STAGING_WINDOW_SIZE:
        raise ValueError(
            f"mod bin is {total:#x} bytes, over the {STAGING_WINDOW_SIZE:#x} "
            "MEM2 staging window (see SUSAMUNE_MEM2_MODBIN_SIZE)")
    return header + body


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest", help="Mod manifest JSON from link_mod.py launcher")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args(argv)

    manifest = json.loads(Path(args.manifest).read_text())
    Path(args.output).write_bytes(build_mod_bin(manifest))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
