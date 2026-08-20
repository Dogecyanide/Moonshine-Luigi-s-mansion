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
import re
import struct
import sys
from pathlib import Path

MAGIC = 0x534D4F44  # 'SMOD'
VERSION = 1
HEADER_SIZE = 32


def shared_hex_define(name, header_name="mem2_map.h"):
    header = (Path(__file__).parent.parent / "include" / "susamune" /
              header_name)
    match = re.search(
        r"^#define\s+{}\s+(0x[0-9a-fA-F]+)u?\s*$".format(re.escape(name)),
        header.read_text(), re.M)
    if not match:
        raise RuntimeError("{} not found in {}".format(name, header))
    return int(match.group(1), 16)


# The loader refuses a larger file, so fail the build instead of shipping one
# that cannot be staged. Read the shared C header rather than duplicating it.
STAGING_WINDOW_SIZE = shared_hex_define("SUSAMUNE_MEM2_MODBIN_SIZE")
STAGED_FILE_MAX_SIZE = shared_hex_define("SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE")
BLOB_MAX_SIZE = shared_hex_define("SUSAMUNE_MOD_BLOB_MAX_SIZE", "mod_bin.h")


def build_mod_bin(manifest):
    code = bytes.fromhex(manifest["code"])
    if len(code) % 4:
        raise ValueError("code blob is not word-aligned")
    if len(code) > BLOB_MAX_SIZE:
        raise ValueError(
            f"code blob is {len(code):#x} bytes, over the {BLOB_MAX_SIZE:#x} "
            "MEM1 working cap")

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
    if total > STAGED_FILE_MAX_SIZE:
        raise ValueError(
            f"mod bin is {total:#x} bytes, over the {STAGED_FILE_MAX_SIZE:#x} "
            "reset-safe ceiling (see SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE)")
    if total > STAGING_WINDOW_SIZE:
        raise ValueError("mod bin exceeds its MEM2 staging window")
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
