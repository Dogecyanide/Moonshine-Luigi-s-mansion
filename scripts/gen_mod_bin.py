"""Pack the mod manifest produced by `link_mod.py launcher` into mod_<vers>.bin,
the file the launcher ships next to boot.dol and loads at runtime.

Format is struct SusamuneModHeader from include/susamune/mod_bin.h: a 32-byte
big-endian header followed by the code blob and hook records. Legacy V1 records
are (address, replacement). Authenticated V2 records are (address, expected,
replacement) and end with a CRC32 of every preceding file byte, including the
header.

Usage: gen_mod_bin.py MANIFEST.json -o mod_jp.bin
"""
import argparse
import json
import re
import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x534D4F44  # 'SMOD'
VERSION_LEGACY = 1
VERSION_AUTH = 2
HEADER_SIZE = 32
WRITE_FLAG_CHECK_ONLY = 1
AUTH_FOOTER_SIZE = 4


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
    checks = manifest.get("checks", [])
    widths = {len(write) for write in writes}
    if (not writes and not checks) or (widths == {2} and not checks):
        version = VERSION_LEGACY
        body = code + b"".join(
            struct.pack(">II", addr & 0xFFFFFFFF, val & 0xFFFFFFFF)
            for addr, val in writes)
        record_count = len(writes)
    elif widths <= {3} and all(len(write) == 3 for write in writes):
        version = VERSION_AUTH
        records = []
        for addr, expected, replacement in writes:
            if addr & 3:
                raise ValueError("authenticated write address is not word-aligned")
            records.append((
                addr & 0xFFFFFFFF,
                expected & 0xFFFFFFFF,
                replacement & 0xFFFFFFFF,
            ))
        for check in checks:
            if len(check) != 2:
                raise ValueError("authentication checks must be (address, expected) pairs")
            addr, expected = check
            if addr & 3:
                raise ValueError("authentication check address is not word-aligned")
            records.append((
                (addr | WRITE_FLAG_CHECK_ONLY) & 0xFFFFFFFF,
                expected & 0xFFFFFFFF,
                expected & 0xFFFFFFFF,
            ))
        body = code + b"".join(struct.pack(">III", *record) for record in records)
        record_count = len(records)
    else:
        raise ValueError("writes must be uniformly (address, value) or "
                         "(address, expected, value) records")

    footer_size = AUTH_FOOTER_SIZE if version == VERSION_AUTH else 0
    total = HEADER_SIZE + len(body) + footer_size
    header = struct.pack(
        ">8I",
        MAGIC,
        version,
        manifest["game_id"],
        manifest["base_addr"],
        len(code),
        record_count,
        manifest.get("region_reserve", 0),
        total,
    )
    assert len(header) == HEADER_SIZE
    if total > STAGED_FILE_MAX_SIZE:
        raise ValueError(
            f"mod bin is {total:#x} bytes, over the {STAGED_FILE_MAX_SIZE:#x} "
            "reset-safe ceiling (see SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE)")
    if total > STAGING_WINDOW_SIZE:
        raise ValueError("mod bin exceeds its MEM2 staging window")
    packed = header + body
    if version == VERSION_AUTH:
        packed += struct.pack(">I", zlib.crc32(packed) & 0xFFFFFFFF)
    assert len(packed) == total
    return packed


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
