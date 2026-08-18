#!/usr/bin/env python3
"""Host contract tests for Stage Loader custom-playlist journals."""

from __future__ import annotations

from pathlib import Path
import re
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "susamune" / "susamune_cfg.h"
KERNEL = ROOT / "launcher" / "kernel" / "SusamuneCfg.c"

MAGIC = 0x53504C46
VERSION = 1
SLOTS = 7
CAPACITY = 120
ROUTES = 121
FILE_SIZE = 896
_HEADER = struct.Struct(">IHBBII16s")


def hash_word(value: int, word: int) -> int:
    return ((value ^ word) * 16777619) & 0xFFFFFFFF


def checksum(generation: int, counts: bytes, entries: bytes) -> int:
    value = 2166136261
    value = hash_word(value, (VERSION << 16) | (SLOTS << 8) | CAPACITY)
    value = hash_word(value, generation)
    for slot in range(SLOTS):
        value = hash_word(value, counts[slot])
        start = slot * CAPACITY
        for entry in entries[start:start + CAPACITY]:
            value = hash_word(value, entry)
    return value


def build_file(
    playlists: list[list[int]], *, generation: int = 1,
    version: int = VERSION,
) -> bytes:
    counts = bytearray(SLOTS)
    entries = bytearray(SLOTS * CAPACITY)
    for slot, playlist in enumerate(playlists):
        counts[slot] = len(playlist)
        start = slot * CAPACITY
        entries[start:start + len(playlist)] = bytes(playlist)
    digest = checksum(generation, counts, entries)
    result = _HEADER.pack(
        MAGIC, version, SLOTS, CAPACITY, generation, digest, bytes(16)
    ) + counts + entries + bytes(17)
    if len(result) != FILE_SIZE:
        raise AssertionError("playlist file layout changed")
    return result


def validate(raw: bytes) -> bool:
    if len(raw) != FILE_SIZE:
        return False
    magic, version, slots, capacity, generation, digest, reserved0 = (
        _HEADER.unpack_from(raw)
    )
    if (magic, version, slots, capacity) != (
        MAGIC, VERSION, SLOTS, CAPACITY
    ):
        return False
    if any(reserved0) or any(raw[-17:]):
        return False
    counts = raw[32:32 + SLOTS]
    entries = raw[32 + SLOTS:32 + SLOTS + SLOTS * CAPACITY]
    if digest != checksum(generation, counts, entries):
        return False
    for slot, count in enumerate(counts):
        if count > CAPACITY:
            return False
        start = slot * CAPACITY
        if any(entry >= ROUTES for entry in entries[start:start + count]):
            return False
        if any(entries[start + count:start + CAPACITY]):
            return False
    return True


def generation_is_newer(candidate: int, current: int) -> bool:
    delta = (candidate - current) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


class PlaylistFormatTests(unittest.TestCase):
    def test_exact_layout_and_empty_slots(self) -> None:
        raw = build_file([[] for _ in range(SLOTS)])
        self.assertEqual(len(raw), FILE_SIZE)
        self.assertTrue(validate(raw))

    def test_full_slot_and_duplicate_routes(self) -> None:
        playlist = [120, 0, 0] + list(range(117))
        self.assertEqual(len(playlist), CAPACITY)
        self.assertTrue(validate(build_file([playlist] + [[]] * 6)))

    def test_active_route_must_exist(self) -> None:
        raw = bytearray(build_file([[0]] + [[]] * 6))
        raw[32 + SLOTS] = ROUTES
        counts = raw[32:32 + SLOTS]
        entries = raw[32 + SLOTS:32 + SLOTS + SLOTS * CAPACITY]
        struct.pack_into(">I", raw, 12, checksum(1, counts, entries))
        self.assertFalse(validate(raw))

    def test_torn_payload_and_header_are_rejected(self) -> None:
        raw = bytearray(build_file([[1, 2, 3]] + [[]] * 6))
        raw[100] ^= 0x80
        self.assertFalse(validate(raw))
        self.assertFalse(validate(raw[:-1]))

    def test_reserved_and_inactive_bytes_must_be_zero(self) -> None:
        raw = bytearray(build_file([[1]] + [[]] * 6))
        raw[16] = 1
        self.assertFalse(validate(raw))

        raw = bytearray(build_file([[1]] + [[]] * 6))
        raw[32 + SLOTS + 1] = 2
        counts = raw[32:32 + SLOTS]
        entries = raw[32 + SLOTS:32 + SLOTS + SLOTS * CAPACITY]
        struct.pack_into(">I", raw, 12, checksum(1, counts, entries))
        self.assertFalse(validate(raw))

    def test_future_version_is_not_adopted(self) -> None:
        self.assertFalse(validate(build_file([[]] * 7, version=2)))

    def test_generation_wrap_order(self) -> None:
        self.assertTrue(generation_is_newer(0, 0xFFFFFFFF))
        self.assertFalse(generation_is_newer(0xFFFFFFFF, 0))
        self.assertFalse(generation_is_newer(7, 7))

    def test_shared_constants_and_safety_paths_are_present(self) -> None:
        header = HEADER.read_text(encoding="utf-8")
        kernel = KERNEL.read_text(encoding="utf-8")
        expected = {
            "SUSAMUNE_STAGE_PLAYLIST_COUNT": SLOTS,
            "SUSAMUNE_STAGE_PLAYLIST_CAPACITY": CAPACITY,
            "SUSAMUNE_STAGE_PLAYLIST_ROUTE_COUNT": ROUTES,
        }
        for name, value in expected.items():
            match = re.search(rf"#define\s+{name}\s+(\d+)u", header)
            self.assertIsNotNone(match, name)
            self.assertEqual(int(match.group(1)), value, name)
        self.assertIn("f_sync(&f)", kernel)
        self.assertIn("PB_READ_UNSAFE", kernel)
        self.assertIn("StagePlaylistChecksum", kernel)


if __name__ == "__main__":
    unittest.main(verbosity=2)
