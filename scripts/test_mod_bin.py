#!/usr/bin/env python3
"""Host contracts for the launcher mod-bin capacity boundaries."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
GEN_PATH = ROOT / "scripts" / "gen_mod_bin.py"

spec = importlib.util.spec_from_file_location("gen_mod_bin_test", GEN_PATH)
assert spec and spec.loader
gen_mod_bin = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen_mod_bin)


def manifest(code_size: int, write_count: int) -> dict:
    return {
        "code": "00" * code_size,
        "game_id": 0x474D534A,
        "base_addr": 0x80426020,
        "region_reserve": 0x82000,
        "writes": [(0x80000000, 0)] * write_count,
    }


def authenticated_manifest(code_size: int, write_count: int) -> dict:
    out = manifest(code_size, 0)
    out["game_id"] = 0x474C4D4A
    out["base_addr"] = 0x804B8400
    out["writes"] = [(0x80000000, 0x12345678, 0x60000000)] * write_count
    out["checks"] = [(0x80000004, 0x4E800020)]
    return out


class ModBinCapacityTests(unittest.TestCase):
    def test_shared_capacity_constants(self) -> None:
        self.assertEqual(gen_mod_bin.BLOB_MAX_SIZE, 0x50000)
        self.assertEqual(gen_mod_bin.STAGED_FILE_MAX_SIZE, 0x5F000)

    def test_current_write_count_fits_at_raw_cap(self) -> None:
        packed = gen_mod_bin.build_mod_bin(manifest(0x50000, 22))
        self.assertEqual(len(packed), 0x500D0)

    def test_raw_cap_is_strict(self) -> None:
        with self.assertRaisesRegex(ValueError, "MEM1 working cap"):
            gen_mod_bin.build_mod_bin(manifest(0x50004, 0))

    def test_staged_file_ceiling_is_end_exclusive(self) -> None:
        exact_writes = (0x5F000 - gen_mod_bin.HEADER_SIZE - 0x50000) // 8
        self.assertEqual(
            len(gen_mod_bin.build_mod_bin(manifest(0x50000, exact_writes))),
            0x5F000,
        )
        with self.assertRaisesRegex(ValueError, "reset-safe ceiling"):
            gen_mod_bin.build_mod_bin(manifest(0x50000, exact_writes + 1))

    def test_authenticated_records_and_footer(self) -> None:
        packed = gen_mod_bin.build_mod_bin(authenticated_manifest(4, 1))
        header = struct.unpack(">8I", packed[:gen_mod_bin.HEADER_SIZE])
        self.assertEqual(header[1], gen_mod_bin.VERSION_AUTH)
        self.assertEqual(header[5], 2)  # one write plus one check
        body = packed[gen_mod_bin.HEADER_SIZE:-gen_mod_bin.AUTH_FOOTER_SIZE]
        footer = struct.unpack(">I", packed[-4:])[0]
        self.assertEqual(footer, zlib.crc32(packed[:-4]) & 0xFFFFFFFF)
        check_addr = struct.unpack(">I", body[-12:][0:4])[0]
        self.assertEqual(check_addr & gen_mod_bin.WRITE_FLAG_CHECK_ONLY, 1)

        # A malformed header can preserve total file size by moving one
        # 12-byte V2 record from the write list into code. The CRC must still
        # reject that repartition instead of silently dropping the first hook.
        repartitioned = bytearray(packed)
        struct.pack_into(">I", repartitioned, 16, header[4] + 12)
        struct.pack_into(">I", repartitioned, 20, header[5] - 1)
        self.assertNotEqual(
            footer,
            zlib.crc32(repartitioned[:-gen_mod_bin.AUTH_FOOTER_SIZE])
            & 0xFFFFFFFF,
        )

    def test_authenticated_write_address_must_be_aligned(self) -> None:
        bad = authenticated_manifest(4, 1)
        bad["writes"][0] = (0x80000001, 0x12345678, 0x60000000)
        with self.assertRaisesRegex(ValueError, "write address is not word-aligned"):
            gen_mod_bin.build_mod_bin(bad)

    def test_authentication_check_address_must_be_aligned(self) -> None:
        bad = authenticated_manifest(4, 1)
        bad["checks"][0] = (0x80000005, 0x4E800020)
        with self.assertRaisesRegex(ValueError, "check address is not word-aligned"):
            gen_mod_bin.build_mod_bin(bad)


if __name__ == "__main__":
    unittest.main()
