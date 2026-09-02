#!/usr/bin/env python3
"""Host contracts for the authenticated GLMJ01 hardware diagnostic."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PATCHES_PATH = ROOT / "lm_diag" / "patches.py"

spec = importlib.util.spec_from_file_location("lm_diag_patches_test", PATCHES_PATH)
assert spec and spec.loader
lm_diag = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lm_diag)


class LuigiMansionDiagnosticContracts(unittest.TestCase):
    def test_authenticated_hook_contract(self) -> None:
        hooks = [
            (entry["lmj"], entry["sym"], entry["type"].name, entry["expected"])
            for entry in lm_diag.patches
        ]
        self.assertEqual(
            hooks,
            [
                (0x801D5B5C, "getArenaLo", "B", 0x806DFF38),
                (0x8000776C, "diagnosticCopyDisp", "BL", 0x481E8CF1),
                (0x80007828, "diagnosticCopyDisp", "BL", 0x481E8C35),
            ],
        )

    def test_revision_checks_cover_presenter_and_getter(self) -> None:
        self.assertEqual(
            lm_diag.checks,
            [
                {"addr": 0x801D5B60, "expected": 0x4E800020},
                {"addr": 0x8000B62C, "expected": 0x4BFFC1BD},
                {"addr": 0x80007870, "expected": 0x481CCFC1},
            ],
        )
        addresses = [entry["lmj"] for entry in lm_diag.patches]
        addresses.extend(entry["addr"] for entry in lm_diag.checks)
        self.assertEqual(len(addresses), len(set(addresses)))

    def test_mem1_reservation_contract(self) -> None:
        self.assertEqual(lm_diag.game_id["lmj"], 0x474C4D4A)
        self.assertEqual(lm_diag.base_addr["lmj"], 0x804B8400)
        self.assertEqual(lm_diag.mod_region_size, 0x80000)
        self.assertEqual(lm_diag.arena_reserve, 0x82000)
        self.assertLessEqual(
            lm_diag.mod_attachment_heap_offset + lm_diag.mod_attachment_heap_size,
            lm_diag.mod_region_size - lm_diag.mod_scratch_size,
        )


if __name__ == "__main__":
    unittest.main()
