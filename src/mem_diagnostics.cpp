#include "susamune/mem_diagnostics.hxx"

#include "Dolphin/printf.h"
#include "JSystem/JKernel/JKRHeap.hxx"
#include "JSystem/JUtility/JUTColor.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/menu.hxx"

namespace {

const u32 kModEnd = SUSAMUNE_ADDR_MOD_BASE + SUSAMUNE_MOD_REGION_SIZE;
const u32 kCanaryAddr = SUSAMUNE_ADDR_MOD_SCRATCH + 0x30u;
const u32 kCanary[4] = {
    0x53555341u, 0x4D454D31u, 0x43414E31u, 0x43414E32u,
};

JKRHeap *sStageHeap;
u32 sRootAddr;
u32 sStageInitialFree;
u32 sStageCurrentFree;
u32 sStageMinimumFree;
u8 sArea;
u8 sEpisode;
bool sFloorOk;
bool sCanaryOk;
bool sStageHeapOk;
bool sStageReady;
u8 sHeapCheckFrames;

volatile u32 *canaryWords() {
    return reinterpret_cast<volatile u32 *>(kCanaryAddr);
}

void sampleCanary() {
    for (u32 i = 0; i < 4; i++) {
        if (canaryWords()[i] != kCanary[i]) {
            sCanaryOk = false;
        }
    }
}

void resetStageSample() {
    sStageHeap = gpApplication.mCurrentHeap;
    sArea = gpApplication.mCurrentScene.mAreaID;
    sEpisode = gpApplication.mCurrentScene.mEpisodeID;
    if (!sStageHeap) {
        sStageInitialFree = 0;
        sStageCurrentFree = 0;
        sStageMinimumFree = 0;
        sStageHeapOk = false;
        sStageReady = false;
        return;
    }

    sStageCurrentFree = sStageHeap->getFreeSize();
    sStageInitialFree = sStageCurrentFree;
    sStageMinimumFree = sStageCurrentFree;
    sStageHeapOk = sStageHeap->check();
    sStageReady = true;
    sHeapCheckFrames = 0;
}

}  // namespace

void memDiagnosticsInit() {
    static_assert(SUSAMUNE_ADDR_QFT_TRANSITION_TARGET + sizeof(u16) <= kCanaryAddr,
                  "scratch fields overlap the MEM diagnostics canary");
    static_assert(kCanaryAddr + sizeof(kCanary) <= kModEnd,
                  "MEM diagnostics canary exceeds the mod region");
    for (u32 i = 0; i < 4; i++) {
        canaryWords()[i] = kCanary[i];
    }
    sCanaryOk = true;

    JKRHeap *root = JKRHeap::sRootHeap;
    sRootAddr = reinterpret_cast<u32>(root);
    sFloorOk = root && sRootAddr >= kModEnd &&
               reinterpret_cast<u32>(root->mStart) >= kModEnd;
    sStageHeap = nullptr;
    sStageHeapOk = false;
    sStageReady = false;
}

void memDiagnosticsOnStageSetup() {
    sampleCanary();
    resetStageSample();
}

void memDiagnosticsUpdate() {
    sampleCanary();
    if (!gpMarDirector || gpMarDirector->_260 == 0) {
        sStageReady = false;
        return;
    }
    if (!sStageReady) {
        return;
    }
    JKRHeap *heap = gpApplication.mCurrentHeap;
    if (heap != sStageHeap ||
        gpApplication.mCurrentScene.mAreaID != sArea ||
        gpApplication.mCurrentScene.mEpisodeID != sEpisode) {
        sStageReady = false;
        sStageHeapOk = false;
        return;
    }
    if (!heap) {
        return;
    }

    sStageCurrentFree = heap->getFreeSize();
    if (sStageCurrentFree < sStageMinimumFree) {
        sStageMinimumFree = sStageCurrentFree;
    }
    if (++sHeapCheckFrames >= 60) {
        sHeapCheckFrames = 0;
        if (!heap->check()) {
            sStageHeapOk = false;
        }
    }
}

void memDiagnosticsDraw(Menu *menu) {
    if (!menu) {
        return;
    }

    const bool ok = sFloorOk && sCanaryOk && (!sStageReady || sStageHeapOk);
    JUtility::TColor status = ok ? JUtility::TColor(120, 255, 120, 255)
                                 : JUtility::TColor(255, 100, 100, 255);
    char line[72];
    menu->fillBox(4, 4, 470, 36, JUtility::TColor(0, 0, 0, 190));
    snprintf(line, sizeof(line), "MEM R:%08lX E:%08lX F:%s C:%s",
             (unsigned long)sRootAddr, (unsigned long)kModEnd,
             sFloorOk ? "OK" : "BAD", sCanaryOk ? "OK" : "BAD");
    menu->drawText(line, 7, 5, 14, 14, status);
    snprintf(line, sizeof(line), "ST %u/%u F:%luK I:%luK M:%luK H:%s",
             (unsigned)sArea, (unsigned)sEpisode,
             (unsigned long)(sStageCurrentFree >> 10),
             (unsigned long)(sStageInitialFree >> 10),
             (unsigned long)(sStageMinimumFree >> 10),
             !sStageReady ? "WAIT" : sStageHeapOk ? "OK" : "BAD");
    menu->drawText(line, 7, 22, 14, 14, status);
}
