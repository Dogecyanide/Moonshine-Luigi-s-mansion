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
CRASH_HEADER = (ROOT / "include" / "susamune" / "crash_report.h").read_text(
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
                (0x8000B534, "diagnosticFrameBegin", "BL", 0x4BFFC1A5),
                (0x8000B544, "diagnosticMainSceneStep", "BL", 0x4BFFFD05),
                (0x8000B268, "diagnosticFirstPosMatrix", "BL", 0x481E99C9),
                (0x8000B34C, "diagnosticLastNrmMatrix", "BL", 0x481E9921),
                (0x8000B35C, "diagnosticSceneDraw", "BL", 0x4E800021),
                (0x8000BD1C, "diagnosticMainDrawBD1C", "BL", 0x480009E5),
                (0x8000BD24, "diagnosticMainDrawBD24", "BL", 0x48000741),
                (0x8000BD2C, "diagnosticMainDrawBD2C", "BL", 0x48000C41),
                (0x8000BD34, "diagnosticMainDrawBD34", "BL", 0x48000E75),
                (0x8000BD3C, "diagnosticMainDrawBD3C", "BL", 0x48179671),
                (0x8000BD44, "diagnosticMainDrawBD44", "BL", 0x48044F29),
                (0x8000BD58, "diagnosticMainDrawBD58", "BL", 0x48003191),
                (0x8000BD6C, "diagnosticMainDrawBD6C", "BL", 0x4BFFFE49),
                (0x8000BDA4, "diagnosticMainDrawBDA4", "BL", 0x481074B9),
                (0x8000BDC4, "diagnosticMainDrawBDC4", "BL", 0x4BFFBF75),
                (0x8000BDC8, "diagnosticMainDrawBDC8", "BL", 0x48107505),
                (0x8000BDDC, "diagnosticMainDrawBDDC", "BL", 0x4BFFF425),
                (0x8000BDE4, "diagnosticMainDrawBDE4", "BL", 0x4BFFBF55),
                (0x8000BDE8, "diagnosticMainDrawBDE8", "BL", 0x48107A51),
                (0x8000BDF0, "diagnosticMainDrawBDF0", "BL", 0x48107685),
                (0x8000BBC8, "diagnosticNormalDrawBBC8", "BL", 0x4BFFDE91),
                (0x8000BBCC, "diagnosticNormalDrawBBCC", "BL", 0x4814AF41),
                (0x8000BBD4, "diagnosticNormalDrawBBD4", "BL", 0x480653D5),
                (0x8000BBE4, "diagnosticNormalDrawBBE4", "BL", 0x48053AD5),
                (0x8000BBF4, "diagnosticNormalDrawBBF4", "BL", 0x48053AC5),
                (0x8000BBF8, "diagnosticNormalDrawBBF8", "BL", 0x48051281),
                (0x8000BBFC, "diagnosticNormalDrawBBFC", "BL", 0x48052705),
                (0x8000BC00, "diagnosticNormalDrawBC00", "BL", 0x4800578D),
                (0x8000BC10, "diagnosticNormalDrawBC10", "BL", 0x48005859),
                (0x8000BC14, "diagnosticNormalDrawBC14", "BL", 0x480058D1),
                (0x8000BC18, "diagnosticNormalDrawBC18", "BL", 0x480543ED),
                (0x8000BC20, "diagnosticNormalDrawBC20", "BL", 0x480545CD),
                (0x8000BC2C, "diagnosticNormalDrawBC2C", "BL", 0x480545C1),
                (0x8000BC30, "diagnosticNormalDrawBC30", "BL", 0x48054DDD),
                (0x8000BC34, "diagnosticNormalDrawBC34", "BL", 0x4814AF91),
                (0x8000BC3C, "diagnosticNormalDrawBC3C", "BL", 0x4806536D),
                (0x8000BC44, "diagnosticNormalDrawBC44", "BL", 0x48065365),
                (0x8000BC48, "diagnosticNormalDrawBC48", "BL", 0x480057C9),
                (0x8000BC64, "diagnosticNormalDrawBC64", "BL", 0x48005655),
                (0x8000BC6C, "diagnosticNormalDrawBC6C", "BL", 0x480057FD),
                (0x8000BC74, "diagnosticNormalDrawBC74", "BL", 0x48051299),
                (0x8000BC7C, "diagnosticNormalDrawBC7C", "BL", 0x4BFFFDE9),
                (0x8000BC88, "diagnosticNormalDrawBC88", "BL", 0x48052FAD),
                (0x8000BC8C, "diagnosticNormalDrawBC8C", "BL", 0x4BFFDDCD),
                (0x8000BC90, "diagnosticNormalDrawBC90", "BL", 0x4BFFBC6D),
                (0x8000BC94, "diagnosticNormalDrawBC94", "BL", 0x4805166D),
                (0x8000BC9C, "diagnosticNormalDrawBC9C", "BL", 0x480326DD),
                (0x8000BCA4, "diagnosticNormalDrawBCA4", "BL", 0x4BFFF031),
                (0x8000BCB4, "diagnosticNormalDrawBCB4", "BL", 0x48005605),
                (0x8000BCB8, "diagnosticNormalDrawBCB8", "BL", 0x480520B1),
                (0x8000BCBC, "diagnosticNormalDrawBCBC", "BL", 0x4803A511),
                (0x8000BCC4, "diagnosticNormalDrawBCC4", "BL", 0x480652E5),
                (0x8000BCC8, "diagnosticNormalDrawBCC8", "BL", 0x48037E91),
                (0x8000BCD0, "diagnosticNormalDrawBCD0", "BL", 0x480652D9),
                (0x8000BA78, "diagnosticPerViewDrawBA78", "BL", 0x48051889),
                (0x8000BA7C, "diagnosticPerViewDrawBA7C", "BL", 0x48054589),
                (0x8000BA88, "diagnosticPerViewDrawBA88", "BL", 0x481E7D05),
                (0x8000BA94, "diagnosticPerViewDrawBA94", "BL", 0x48005A51),
                (0x8000BAA4, "diagnosticPerViewDrawBAA4", "BL", 0x481797D1),
                (0x8000BAA8, "diagnosticPerViewDrawBAA8", "BL", 0x48054371),
                (0x8000BAB0, "diagnosticPerViewDrawBAB0", "BL", 0x4805473D),
                (0x8000BAB4, "diagnosticPerViewDrawBAB4", "BL", 0x4814B111),
                (0x8000BABC, "diagnosticPerViewDrawBABC", "BL", 0x480654ED),
                (0x8000BAC0, "diagnosticPerViewDrawBAC0", "BL", 0x4802C0A5),
                (0x8000BAC4, "diagnosticPerViewDrawBAC4", "BL", 0x480AC9E5),
                (0x8000BAC8, "diagnosticPerViewDrawBAC8", "BL", 0x481552F5),
                (0x8000BAD0, "diagnosticPerViewDrawBAD0", "BL", 0x48005A15),
                (0x8000BAD8, "diagnosticPerViewDrawBAD8", "BL", 0x48054715),
                (0x8000BAE0, "diagnosticPerViewDrawBAE0", "BL", 0x480544D1),
                (0x8000BAE4, "diagnosticPerViewDrawBAE4", "BL", 0x4BFFDF75),
                (0x8000BAE8, "diagnosticPerViewDrawBAE8", "BL", 0x48051819),
                (0x8000BB00, "diagnosticPerViewDrawBB00", "BL", 0x4BFFCA2D),
                (0x8000BB04, "diagnosticPerViewDrawBB04", "BL", 0x481552E1),
                (0x8000BB08, "diagnosticPerViewDrawBB08", "BL", 0x48117719),
                (0x8000BB0C, "diagnosticPerViewDrawBB0C", "BL", 0x48123001),
                (0x8000BB10, "diagnosticPerViewDrawBB10", "BL", 0x4811F6C1),
                (0x8000BB20, "diagnosticPerViewDrawBB20", "BL", 0x48152CD9),
                (0x8000BB24, "diagnosticPerViewDrawBB24", "BL", 0x48054EE9),
                (0x8000BB2C, "diagnosticPerViewDrawBB2C", "BL", 0x480546C1),
                (0x8000BB34, "diagnosticPerViewDrawBB34", "BL", 0x48065475),
                (0x8000BB38, "diagnosticPerViewDrawBB38", "BL", 0x48000665),
                (0x8000BB3C, "diagnosticPerViewDrawBB3C", "BL", 0x480517C5),
                (0x8000BB44, "diagnosticPerViewDrawBB44", "BL", 0x481E7A01),
                (0x8000BB54, "diagnosticPerViewDrawBB54", "BL", 0x481E7A31),
                (0x8000BB58, "diagnosticPerViewDrawBB58", "BL", 0x48052DD5),
                (0x8000BB60, "diagnosticPerViewDrawBB60", "BL", 0x481E79E5),
                (0x8000BB64, "diagnosticPerViewDrawBB64", "BL", 0x48052A99),
                (0x8000BB70, "diagnosticPerViewDrawBB70", "BL", 0x4BFFC9BD),
                (0x8000BB74, "diagnosticPerViewDrawBB74", "BL", 0x48005A71),
                (0x8000BB84, "diagnosticPerViewDrawBB84", "BL", 0x48179715),
                (0x8000BB8C, "diagnosticPerViewDrawBB8C", "BL", 0x4806541D),
                (0x8000BB9C, "diagnosticPerViewDrawBB9C", "BL", 0x481E4061),
                (0x8000B360, "diagnosticOrthoReset", "BL", 0x4BFFC59D),
                (0x8000B5EC, "diagnosticPreMainUpdate", "BL", 0x4BFFF6B9),
                (0x8000B608, "diagnosticPostMainUpdate", "BL", 0x4BFFC9FD),
                (0x8000B62C, "diagnosticChangeFrameBuffer", "BL", 0x4BFFC1BD),
                (0x8000B640, "diagnosticConditionalTail", "BL", 0x4BFFC975),
                (0x8000B65C, "diagnosticLoopTailSync", "BL", 0x4BFFFD1D),
                (0x8000B660, "diagnosticLoopTailClock", "BL", 0x4BFFA7A5),
                (0x8000B714, "diagnosticGameLoop", "BL", 0x4BFFFDD5),
                (0x8000B728, "diagnosticOuterCleanup", "BL", 0x4BFFF551),
                (0x8000B744, "diagnosticOuterRestart", "BL", 0x4BFFA92D),
            ],
        )

    def test_revision_checks_cover_presenter_input_and_crash_setter(self) -> None:
        self.assertEqual(
            lm_diag.checks,
            [
                {"addr": 0x801D5B60, "expected": 0x4E800020},
                {"addr": 0x80007870, "expected": 0x481CCFC1},
                {"addr": 0x8000B350, "expected": 0x806D8038},
                {"addr": 0x8000B354, "expected": 0x8183001C},
                {"addr": 0x8000B358, "expected": 0x7D8803A6},
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
        self.assertIn("kSnapshotVersion = 5u", STATE_SOURCE)
        self.assertIn("kRendererStateStart = 0x80398770u", STATE_SOURCE)
        self.assertIn("kRendererStateEnd = 0x803989E0u", STATE_SOURCE)
        self.assertIn(
            "{kRendererStateStart, kRendererStateEnd - kRendererStateStart}",
            STATE_SOURCE,
        )
        self.assertIn("kInGameFlagsBase = 0x803C7CA0u", STATE_SOURCE)
        self.assertIn("kInGameFlagsOffset = 0x659u", STATE_SOURCE)
        self.assertIn("kInGameFlagsSize = 0x20u", STATE_SOURCE)
        self.assertIn("kMainLoopStateBase = 0x80398A40u", STATE_SOURCE)
        self.assertIn("kMainLoopStateSize = 0x08u", STATE_SOURCE)
        self.assertIn("kMainLoopSceneGlobal = 0x804A0C20u", STATE_SOURCE)
        self.assertIn("kMainLoopExitGlobal = 0x804A0C28u", STATE_SOURCE)
        self.assertIn("kMainDrawStateGlobal = 0x804A0C44u", STATE_SOURCE)
        self.assertIn("kMatrixArrayGlobal = 0x804A17B8u", STATE_SOURCE)
        self.assertIn("kBooleanArrayGlobal = 0x804A17BCu", STATE_SOURCE)
        self.assertIn("kSimpleModelerGlobal = 0x804A17D0u", STATE_SOURCE)
        self.assertIn("kMapColGlobal = 0x804A17D8u", STATE_SOURCE)
        self.assertIn("kEnTypesManagerGlobal = 0x804A17E8u", STATE_SOURCE)
        self.assertIn("kGameSdata0Start = 0x80498AF8u", STATE_SOURCE)
        self.assertIn("kGameSdata0End = 0x80498B18u", STATE_SOURCE)
        self.assertIn("kGameSdata1Start = 0x80498B20u", STATE_SOURCE)
        self.assertIn("kGameSdata1End = 0x804A03A8u", STATE_SOURCE)
        self.assertIn("kGameSbss0Start = 0x804A0C00u", STATE_SOURCE)
        self.assertIn("kGameSbss0End = 0x804A0C90u", STATE_SOURCE)
        self.assertIn("kGameSbss1Start = 0x804A0CB0u", STATE_SOURCE)
        self.assertIn("kGameSbss1End = 0x804A1D10u", STATE_SOURCE)
        self.assertIn("kStateStaticsSize == 0x8C30u", STATE_SOURCE)
        self.assertIn("kHeapDataOffset == 0x8D80u", STATE_SOURCE)
        self.assertIn("captureStaticRanges();", STATE_SOURCE)
        self.assertIn("restoreStaticRanges();", STATE_SOURCE)
        self.assertIn("storeStaticRanges();", STATE_SOURCE)
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
        self.assertIn('return "LOOP";', STATE_SOURCE)
        self.assertIn('return "EXIT";', STATE_SOURCE)
        self.assertIn('return "PEND";', STATE_SOURCE)
        self.assertIn('return "DRAW";', STATE_SOURCE)
        self.assertIn('return "GROOT";', STATE_SOURCE)
        self.assertIn("identity->mainLoopMode != 2u", STATE_SOURCE)
        self.assertIn(
            "identity->mainLoopPendingScene != identity->mainLoopScene",
            STATE_SOURCE,
        )
        self.assertIn(
            "header->mainDrawState == live.mainDrawState", STATE_SOURCE
        )
        self.assertIn("identity->mainDrawState > 7u", STATE_SOURCE)
        self.assertIn("header->mainDrawState <= 7u", STATE_SOURCE)
        self.assertIn("kGameStaticRootGlobals[i]", STATE_SOURCE)
        self.assertIn("header->simpleModeler == live.simpleModeler", STATE_SOURCE)
        self.assertIn("header->mapCol == live.mapCol", STATE_SOURCE)
        self.assertIn("header->enTypesManager == live.enTypesManager", STATE_SOURCE)

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
            STATE_SOURCE.count("traceSavePhase("), 8
        )
        self.assertGreaterEqual(
            STATE_SOURCE.count("traceLoadPhase("), 16
        )
        self.assertIn("LMCrash::note(kEventStateSavePhase", STATE_SOURCE)
        self.assertIn("LMCrash::note(kEventStateLoadPhase", STATE_SOURCE)
        self.assertIn("272=save-phase", KERNEL_CRASH_SOURCE)
        self.assertIn("273=load-phase", KERNEL_CRASH_SOURCE)

    def test_hard_hang_phase_journal_contract(self) -> None:
        self.assertIn("SUSAMUNE_PHASE_TRACE_OFFSET", CRASH_HEADER)
        self.assertIn("sizeof(struct SusamunePhaseTrace) ==", CRASH_HEADER)
        self.assertIn("SUSAMUNE_PHASE_TRACE_PPC_PTR", CRASH_SOURCE)
        self.assertIn("sequence - 1u", CRASH_SOURCE)
        self.assertIn("SUSAMUNE_PHASE_TRACE_PHYS_PTR", KERNEL_CRASH_SOURCE)
        self.assertIn("PollPhaseTrace();", KERNEL_CRASH_SOURCE)
        self.assertIn("f_sync(&dbgfile)", (ROOT / "launcher" / "kernel" /
                                           "vsprintf.c").read_text(encoding="utf-8"))
        self.assertIn("presenterEnter();", DIAG_SOURCE)
        self.assertIn("presenterAfterTick();", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x88u);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x8Au);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x8Cu);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x8Eu);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x90u);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x92u);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x94u);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x96u);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x98u);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0x9Au);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0xA0u);", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0xA2u);", DIAG_SOURCE)
        self.assertIn("postLoadDetail(0xA4u", DIAG_SOURCE)
        self.assertIn("postLoadMilestone(0xA6u);", DIAG_SOURCE)
        self.assertIn("0xB0u, 0xB1u", DIAG_SOURCE)
        self.assertIn("0xC0u, 0xC1u", DIAG_SOURCE)
        self.assertIn("0xD0u, 0xD1u", DIAG_SOURCE)

    def test_post_load_trace_spans_multiple_restored_frames(self) -> None:
        self.assertIn("kPostLoadTraceFrameLimit = 8u", STATE_SOURCE)
        self.assertIn("u32 sPostLoadTraceFrame", STATE_SOURCE)
        load_success = STATE_SOURCE.split(
            "sStatus = LMState::Status::Loaded", 1
        )[1].split("traceLoadPhase(0x7Fu", 1)[0]
        self.assertLess(load_success.index("sPostLoadTraceFrame = 0u"),
                        load_success.index("sPostLoadTraceState = 1u"))
        presenter_enter = STATE_SOURCE.split("void presenterEnter()", 1)[1]
        self.assertLess(presenter_enter.index("sPostLoadTraceFrame >="),
                        presenter_enter.index("++sPostLoadTraceFrame"))
        after_tick = STATE_SOURCE.split("void presenterAfterTick()", 1)[1]
        self.assertLess(after_tick.index("tracePostLoadPhase(0x86u"),
                        after_tick.index("sPostLoadTraceState = 1u"))

    def test_state_transaction_runs_after_complete_retail_presenter(self) -> None:
        wrapper = DIAG_SOURCE.split(
            'extern "C" void diagnosticChangeFrameBuffer', 1
        )[1].split('extern "C" void diagnosticFrameBegin', 1)[0]
        self.assertLess(wrapper.index("kLMChangeFrameBufferAddr"),
                        wrapper.index("LMState::tick"))
        copy_wrapper = DIAG_SOURCE.split(
            'extern "C" void diagnosticCopyDisp', 1
        )[1]
        self.assertNotIn("LMState::tick", copy_wrapper)

    def test_frozen_transactions_do_not_take_heap_mutexes(self) -> None:
        save_frozen = STATE_SOURCE.split(
            "traceSavePhase(0x60u", 1
        )[1].split("freezeEnd(freeze);", 1)[0]
        load_frozen = STATE_SOURCE.split(
            "traceLoadPhase(0x60u", 1
        )[1].split("freezeEnd(freeze, true);", 1)[0]
        self.assertNotIn("heapsHealthy", save_frozen)
        self.assertNotIn("heapsHealthy", load_frozen)
        self.assertNotIn("ioIdle", save_frozen)
        self.assertNotIn("ioIdle", load_frozen)

    def test_overlay_is_painted_before_state_transaction(self) -> None:
        copy_wrapper = DIAG_SOURCE.split(
            'extern "C" void diagnosticCopyDisp', 1
        )[1]
        self.assertIn("drawRawHeartbeat", copy_wrapper)
        presenter_wrapper = DIAG_SOURCE.split(
            'extern "C" void diagnosticChangeFrameBuffer', 1
        )[1].split('extern "C" void diagnosticFrameBegin', 1)[0]
        self.assertLess(presenter_wrapper.index("kLMChangeFrameBufferAddr"),
                        presenter_wrapper.index("LMState::tick"))

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
