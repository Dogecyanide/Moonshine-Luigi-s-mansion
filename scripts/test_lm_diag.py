#!/usr/bin/env python3
"""Host contracts for the authenticated GLMJ01 hardware diagnostic."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PATCHES_PATH = ROOT / "lm_diag" / "patches.py"
STATE_SOURCE = (ROOT / "lm_diag" / "src" / "lm_state.cpp").read_text(
    encoding="utf-8"
)
DIAG_SOURCE = (ROOT / "lm_diag" / "src" / "lm_diag.cpp").read_text(
    encoding="utf-8"
)
CRASH_SOURCE = (ROOT / "lm_diag" / "src" / "lm_crash.cpp").read_text(
    encoding="utf-8"
)
KERNEL_CRASH_SOURCE = (ROOT / "launcher" / "kernel" / "SusamuneCrash.c").read_text(
    encoding="utf-8"
)

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

    def test_revision_checks_cover_presenter_input_and_crash_setter(self) -> None:
        self.assertEqual(
            lm_diag.checks,
            [
                {"addr": 0x801D5B60, "expected": 0x4E800020},
                {"addr": 0x8000B62C, "expected": 0x4BFFC1BD},
                {"addr": 0x80007870, "expected": 0x481CCFC1},
                {"addr": 0x801D20B0, "expected": 0x387D0018},
                {"addr": 0x801D20B4, "expected": 0x48012849},
                {"addr": 0x801D4124, "expected": 0x800D1594},
                {"addr": 0x801D4128, "expected": 0x906D1594},
                {"addr": 0x801D412C, "expected": 0x7C030378},
                {"addr": 0x801D4130, "expected": 0x4E800020},
                {"addr": 0x801DAE98, "expected": 0x7C0802A6},
                {"addr": 0x801DAE9C, "expected": 0x90010004},
                {"addr": 0x801DAEA0, "expected": 0x9421FFF0},
                {"addr": 0x801DAED8, "expected": 0x7C0802A6},
                {"addr": 0x801DAEDC, "expected": 0x90010004},
                {"addr": 0x801DAEE0, "expected": 0x9421FFF0},
                {"addr": 0x8018B810, "expected": 0x93ED12F0},
                {"addr": 0x8018D4E4, "expected": 0x7C0802A6},
                {"addr": 0x8018D510, "expected": 0x806301E8},
                {"addr": 0x8018D54C, "expected": 0x800DF94C},
                {"addr": 0x8018D5F0, "expected": 0x901E0064},
                {"addr": 0x8018D5F8, "expected": 0x389D0800},
                {"addr": 0x8018D61C, "expected": 0x38BE0064},
                {"addr": 0x8018D62C, "expected": 0x4BFFF1F1},
                {"addr": 0x8018D630, "expected": 0x801E0050},
                {"addr": 0x804A03A8, "expected": 0x803E3CF8},
            ],
        )
        addresses = [entry["lmj"] for entry in lm_diag.patches]
        addresses.extend(entry["addr"] for entry in lm_diag.checks)
        self.assertEqual(len(addresses), len(set(addresses)))

    def test_mem1_reservation_contract(self) -> None:
        self.assertEqual(lm_diag.game_id["lmj"], 0x474C4D4A)
        self.assertEqual(lm_diag.base_addr["lmj"], 0x804B8400)
        self.assertEqual(lm_diag.dol_size["lmj"], 0x00394924)
        self.assertEqual(lm_diag.dol_min["lmj"], 0x00003100)
        self.assertEqual(lm_diag.dol_max["lmj"], 0x004A6400)
        self.assertEqual(lm_diag.mod_region_size, 0x80000)
        self.assertEqual(lm_diag.arena_reserve, 0x82000)
        self.assertLessEqual(
            lm_diag.mod_attachment_heap_offset + lm_diag.mod_attachment_heap_size,
            lm_diag.mod_region_size - lm_diag.mod_scratch_size,
        )

    def test_state_slot_and_split_heap_contract(self) -> None:
        self.assertIn(
            "kSnapshotBase = SUSAMUNE_MEM2_SNAPSHOT_PPC_BASE", STATE_SOURCE
        )
        self.assertIn(
            "kSnapshotCapacity = SUSAMUNE_MEM2_SNAPSHOT_SIZE", STATE_SOURCE
        )
        self.assertIn("kHeapMetadataStart = 0x3Cu", STATE_SOURCE)
        self.assertIn("kHeapMetadataEnd = 0x84u", STATE_SOURCE)
        self.assertIn("kExpHeapAlignment = 16u", STATE_SOURCE)
        self.assertIn("kSnapshotVersion = 3u", STATE_SOURCE)
        self.assertIn("kInGameFlagsBase = 0x803C7CA0u", STATE_SOURCE)
        self.assertIn("kInGameFlagsOffset = 0x659u", STATE_SOURCE)
        self.assertIn("kInGameFlagsSize = 0x20u", STATE_SOURCE)
        self.assertIn("header->magic = 0u", STATE_SOURCE)
        self.assertIn("void initializeSlot()", STATE_SOURCE)
        self.assertIn("initializeSlot();", STATE_SOURCE)
        self.assertLess(
            STATE_SOURCE.index("header->magic = 0u"),
            STATE_SOURCE.index("header->magic = kSnapshotMagic"),
        )
        self.assertNotIn("copyWords(reinterpret_cast<void *>(kMem1Start)", STATE_SOURCE)

    def test_state_controls_and_resource_gates(self) -> None:
        self.assertIn("kDPadLeft = 0x0001u", STATE_SOURCE)
        self.assertIn("kDPadRight = 0x0002u", STATE_SOURCE)
        self.assertIn("kDvdBusyPredicateAddr = 0x80006A5Cu", STATE_SOURCE)
        self.assertIn("kAramList0Global + 8u", STATE_SOURCE)
        self.assertIn("kAramList1Global + 8u", STATE_SOURCE)
        self.assertIn("kCardBlockGlobal = 0x80495960u", STATE_SOURCE)
        self.assertIn("kCardControlStride = 0x108u", STATE_SOURCE)
        self.assertIn("kCardResultBusy = 0xFFFFFFFFu", STATE_SOURCE)
        self.assertIn("kCurrentHeapGroupGlobal = 0x80498AE8u", STATE_SOURCE)
        self.assertIn("headerMatchesLive", STATE_SOURCE)
        self.assertIn("buildIdentity(&live, true)", STATE_SOURCE)
        self.assertIn("ioIdle(true)", STATE_SOURCE)
        self.assertIn('return "CARD0";', STATE_SOURCE)
        self.assertIn('return "MODE";', STATE_SOURCE)
        self.assertIn('return "AUDIO";', STATE_SOURCE)

    def test_state_quiesces_audio_and_scheduler(self) -> None:
        self.assertIn("kAudioBasicGlobal = 0x804A1DD0u", STATE_SOURCE)
        self.assertIn("kAudioStaticObject = 0x803E3CF8u", STATE_SOURCE)
        self.assertIn("kAudioVtable = 0x80383FB0u", STATE_SOURCE)
        self.assertIn("kAudioBootstrapSoundId = 0x80000800u", STATE_SOURCE)
        self.assertIn("kAudioChangeSoundSceneAddr = 0x8018D4E4u", STATE_SOURCE)
        self.assertIn("!quiesceAudio(preflight)", STATE_SOURCE)
        self.assertEqual(STATE_SOURCE.count("!quiesceAudio(preflight)"), 2)
        self.assertIn("validAudioBootstrap(identity.audioBasic)", STATE_SOURCE)
        self.assertNotIn("AudioStopSoundHandleFn", STATE_SOURCE)
        self.assertNotIn("kAudioStopSoundHandleAddr", STATE_SOURCE)
        self.assertIn("kOSDisableSchedulerAddr = 0x801DAE98u", STATE_SOURCE)
        self.assertIn("kOSEnableSchedulerAddr = 0x801DAED8u", STATE_SOURCE)

    def test_state_transactions_leave_phase_breadcrumbs(self) -> None:
        self.assertIn("kEventStateSavePhase = 0x110u", STATE_SOURCE)
        self.assertIn("kEventStateLoadPhase = 0x111u", STATE_SOURCE)
        self.assertGreaterEqual(
            STATE_SOURCE.count("LMCrash::note(kEventStateSavePhase"), 8
        )
        self.assertGreaterEqual(
            STATE_SOURCE.count("LMCrash::note(kEventStateLoadPhase"), 6
        )
        self.assertIn("272=save-phase", KERNEL_CRASH_SOURCE)
        self.assertIn("273=load-phase", KERNEL_CRASH_SOURCE)

    def test_frozen_transactions_do_not_take_heap_mutexes(self) -> None:
        save_frozen = STATE_SOURCE.split(
            "const FreezeState freeze = freezeBegin();", 1
        )[1].split("freezeEnd(freeze);", 1)[0]
        load_frozen = STATE_SOURCE.rsplit(
            "const FreezeState freeze = freezeBegin();", 1
        )[1].split("freezeEnd(freeze);", 1)[0]
        self.assertNotIn("heapsHealthy", save_frozen)
        self.assertNotIn("heapsHealthy", load_frozen)
        self.assertNotIn("ioIdle", save_frozen)
        self.assertNotIn("ioIdle", load_frozen)

    def test_overlay_is_painted_before_state_transaction(self) -> None:
        diag = DIAG_SOURCE.split(
            'extern "C" void diagnosticCopyDisp', 1
        )[1]
        self.assertLess(diag.index("drawRawHeartbeat"),
                        diag.index("LMState::tick"))

    def test_state_gates_uncaptured_allocator_epochs(self) -> None:
        self.assertIn("header->rootFreeHead == live.rootFreeHead", STATE_SOURCE)
        self.assertIn("header->systemFreeHead == live.systemFreeHead", STATE_SOURCE)
        self.assertIn("header->currentHeap == live.currentHeap", STATE_SOURCE)
        self.assertIn(
            "header->currentHeapGroup == live.currentHeapGroup", STATE_SOURCE
        )
        self.assertNotIn("writeByte(live.systemHeap", STATE_SOURCE)
        self.assertNotIn("writeWord(kCurrentHeapGlobal", STATE_SOURCE)

    def test_lm_crash_callback_contract(self) -> None:
        self.assertIn("kSetPreUserCallbackAddr = 0x801D4124u", CRASH_SOURCE)
        self.assertIn("kPreUserCallbackAddr = 0x804A2074u", CRASH_SOURCE)
        self.assertIn("SUSAMUNE_CRASH_PPC_PTR", CRASH_SOURCE)
        self.assertIn("sPreviousHandler(exception, context, dsisr, dar)", CRASH_SOURCE)


if __name__ == "__main__":
    unittest.main()
