#!/usr/bin/env python3
"""Host contracts for per-IL Stage Loader targets and safe UI glyphs."""

from __future__ import annotations

from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "susamune" / "susamune_cfg.h"
MEM2 = ROOT / "include" / "susamune" / "mem2_map.h"
KERNEL = ROOT / "launcher" / "kernel" / "SusamuneCfg.c"
TARGETS = ROOT / "src" / "stage_targets.cpp"
MENU = ROOT / "src" / "menu.cpp"
SETTINGS = ROOT / "src" / "settings.cpp"
STAGE_LOADER = ROOT / "src" / "stage_loader.cpp"
ILING = ROOT / "src" / "iling.cpp"

MAGIC = 0x53544746
VERSION = 1
SLOTS = 128
UNSET = -1
MAX_QF = 0x000AF9B0
MAILBOX_SIZE = 0x240
FILE_SIZE = 0x220
LIVE_SIZE = 0x200

FILE_HEADER = struct.Struct(">IHHIII12s")
TARGET_ARRAY = struct.Struct(">" + "i" * SLOTS)


def hash_word(value: int, word: int) -> int:
    return ((value ^ (word & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF


def checksum(game_id: int, generation: int, targets: tuple[int, ...]) -> int:
    value = 2166136261
    value = hash_word(value, (VERSION << 16) | SLOTS)
    value = hash_word(value, game_id)
    value = hash_word(value, generation)
    for target in targets:
        value = hash_word(value, target)
    return value


def build_file(
    targets: tuple[int, ...], *, game_id: int = 0x474D534A,
    generation: int = 1, version: int = VERSION,
) -> bytes:
    if len(targets) != SLOTS:
        raise ValueError("wrong target count")
    digest = checksum(game_id, generation, targets)
    raw = FILE_HEADER.pack(
        MAGIC, version, SLOTS, game_id, generation, digest, bytes(12)
    ) + TARGET_ARRAY.pack(*targets)
    if len(raw) != FILE_SIZE:
        raise AssertionError("target journal layout changed")
    return raw


def parse_file(raw: bytes, game_id: int = 0x474D534A) -> tuple[int, tuple[int, ...]]:
    if len(raw) != FILE_SIZE:
        raise ValueError("wrong file size")
    magic, version, slots, stored_game, generation, digest, reserved = (
        FILE_HEADER.unpack_from(raw)
    )
    if version != VERSION:
        raise RuntimeError("unknown version must be preserved")
    targets = TARGET_ARRAY.unpack_from(raw, FILE_HEADER.size)
    if (
        magic != MAGIC
        or slots != SLOTS
        or stored_game != game_id
        or reserved != bytes(12)
        or digest != checksum(stored_game, generation, targets)
        or any(target < UNSET or target > MAX_QF for target in targets)
    ):
        raise ValueError("invalid target journal")
    return generation, targets


def generation_is_newer(candidate: int, current: int) -> bool:
    delta = (candidate - current) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


class StageTargetJournalTests(unittest.TestCase):
    def test_layout_and_round_trip(self) -> None:
        targets = tuple(
            UNSET if slot % 3 == 0 else 120 * slot for slot in range(SLOTS)
        )
        raw = build_file(targets, generation=7)
        generation, decoded = parse_file(raw)
        self.assertEqual(FILE_HEADER.size, 0x20)
        self.assertEqual(TARGET_ARRAY.size, LIVE_SIZE)
        self.assertEqual(MAILBOX_SIZE, 0x40 + LIVE_SIZE)
        self.assertEqual(generation, 7)
        self.assertEqual(decoded, targets)

    def test_corruption_and_out_of_range_values_are_rejected(self) -> None:
        raw = bytearray(build_file((UNSET,) * SLOTS))
        raw[-1] ^= 1
        with self.assertRaises(ValueError):
            parse_file(bytes(raw))

        invalid = [UNSET] * SLOTS
        invalid[42] = MAX_QF + 1
        with self.assertRaises(ValueError):
            parse_file(build_file(tuple(invalid)))

    def test_future_version_is_not_treated_as_disposable_corruption(self) -> None:
        raw = build_file((UNSET,) * SLOTS, version=VERSION + 1)
        with self.assertRaises(RuntimeError):
            parse_file(raw)

    def test_generation_comparison_wraps_safely(self) -> None:
        self.assertTrue(generation_is_newer(0, 0xFFFFFFFF))
        self.assertTrue(generation_is_newer(9, 8))
        self.assertFalse(generation_is_newer(8, 8))
        self.assertFalse(generation_is_newer(7, 8))


class StageTargetSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.mem2 = MEM2.read_text(encoding="utf-8")
        cls.kernel = KERNEL.read_text(encoding="utf-8")
        cls.targets = TARGETS.read_text(encoding="utf-8")
        cls.menu = MENU.read_text(encoding="utf-8")
        cls.settings = SETTINGS.read_text(encoding="utf-8")
        cls.stage_loader = STAGE_LOADER.read_text(encoding="utf-8")
        cls.iling = ILING.read_text(encoding="utf-8")

    def test_shared_abi_matches_the_v1_journal(self) -> None:
        for contract in (
            "#define SUSAMUNE_STAGE_TARGET_VERSION        1u",
            "#define SUSAMUNE_STAGE_TARGET_SLOT_COUNT     128u",
            "#define SUSAMUNE_STAGE_TARGET_UNSET          (-1)",
            "sizeof(struct SusamuneStageTargetsCfg) == SUSAMUNE_STAGE_TARGETS_CFG_SIZE",
            "sizeof(struct SusamuneStageTargetsFile) == 0x220",
            "__builtin_offsetof(struct SusamuneStageTargetsCfg, targets) == 0x40",
        ):
            self.assertIn(contract, self.header)

    def test_target_window_is_derived_and_guarded_against_split_mailbox(self) -> None:
        console_base = 0x92EF5400 + 0x100
        dolphin_base = 0x70FF4800 + 0x100
        self.assertEqual(console_base, 0x92EF5500)
        self.assertEqual(dolphin_base, 0x70FF4900)
        self.assertLessEqual(console_base + MAILBOX_SIZE + LIVE_SIZE, 0x92EF8800)
        self.assertLessEqual(dolphin_base + MAILBOX_SIZE + LIVE_SIZE, 0x70FF8800)
        self.assertIn("SUSAMUNE_CONSOLE_STAGE_LOADER_QUEUE_PPC_BASE +", self.mem2)
        self.assertIn("SUSAMUNE_DOLPHIN_STAGE_LOADER_QUEUE_PPC_BASE +", self.mem2)
        self.assertIn("SUSAMUNE_SPLIT_STATS_CFG_OFFSET 0x8800u", self.header)
        self.assertIn("susamune_split_stats_stage_target_gap_check", self.header)
        self.assertNotIn("0x6D40", self.mem2)

    def test_kernel_uses_regional_dual_generation_files(self) -> None:
        self.assertIn('"%s/susamune_stage_targets_%s_a.bin"', self.kernel)
        self.assertIn('"%s/susamune_stage_targets_%s_b.bin"', self.kernel)
        self.assertIn("PbGenerationIsNewer(file.generation", self.kernel)
        self.assertIn("file->version != SUSAMUNE_STAGE_TARGET_VERSION", self.kernel)
        self.assertIn("f_sync(&f)", self.kernel)

    def test_ui_loads_only_committed_routes_and_saves_only_edits(self) -> None:
        self.assertIn("mStreakEntry = entry;", self.menu)
        self.assertGreaterEqual(
            self.menu.count("mTargetQf = StageTargets::get(mStreakEntry);"), 3
        )
        self.assertEqual(
            self.menu.count("StageTargets::set(mStreakEntry, mTargetQf);"), 2
        )
        self.assertIn("sTargets[slot] == targetQf", self.targets)
        self.assertIn("return validEntry(entry) ? pbSlot(entry) : -1;", self.iling)

    def test_save_ack_is_serviced_after_leaving_stage_loader_tab(self) -> None:
        menu_update = self.menu.index("void Menu::update(TMarioGamePad *pad)")
        global_service = self.menu.index("StageTargets::service(this);", menu_update)
        self.assertGreater(global_service, menu_update)
        self.assertNotIn("StageTargets::service(menu);", self.menu)

    def test_dolphin_backend_never_publishes(self) -> None:
        start = self.targets.index("bool publish()")
        end = self.targets.index("}  // namespace", start)
        publish = self.targets[start:end]
        self.assertIn("#if IS_EMULATOR\n    return false;", publish)
        self.assertIn("if (sBackend)", self.targets)

    def test_regional_ui_avoids_repurposed_percent_and_slash_glyphs(self) -> None:
        self.assertIn("if (code == '/') code = 0x815e;", self.menu)
        self.assertIn("s = fontSafeText(s);", self.menu)
        self.assertNotIn('"25%\\0"', self.settings)
        self.assertNotIn('"Fast Any%"', self.stage_loader)
        self.assertIn('"25 pct\\0"', self.settings)
        self.assertIn('"Fast Any percent"', self.stage_loader)
        self.assertNotIn("%%", self.menu)
        self.assertNotIn("%%", self.stage_loader)


if __name__ == "__main__":
    unittest.main()
