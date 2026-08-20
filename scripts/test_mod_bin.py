#!/usr/bin/env python3
"""Host contracts for the launcher mod-bin capacity boundaries."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


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


if __name__ == "__main__":
    unittest.main()
