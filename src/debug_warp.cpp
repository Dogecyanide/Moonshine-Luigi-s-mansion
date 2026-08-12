#include "susamune/debug_warp.hxx"

#include "SMS/Manager/FlagManager.hxx"
#include "SMS/System/Application.hxx"

namespace Warp {

const char *const kStageNames[WARP_NUM_STAGES] = {
    "Airstrip",   "Delfino Plaza", "Bianco Hills",  "Ricco Harbor",   "Gelato Beach",
    "Pinna Park", "Sirena Beach",  "Sirena Hotel?", "Pianta Village", "Noki Bay",
};

const WarpDescriptor kPresets[] = {
    { "Delfino - Bianco Plant",      0x1,  0x0, 0x1, -1 },
    { "Delfino - Bianco Chase",      0x1,  0x1, 0x1, -1 },
    { "Delfino - Ricco Plant",       0x1,  0x5, 0x1, -1 },
    { "Pianta 1",                    0x8,  0x0, -1,  -1 },
    { "Pianta 2",                    0x8,  0x1, -1,  -1 },
    { "Pianta 3",                    0x8,  0x2, -1,  -1 },
    { "Delfino - Pianta Exit",       0x1,  0x5, 0x8, -1 },
    { "Pachinko",                    0x16, 0x0, -1,  -1 },
    { "Delfino - Ricco Plant (bg2)", 0x1,  0x5, 0x1, 94 },
    { "Ricco 1",                     0x3,  0x0, -1,  -1 },
    { "Ricco 2",                     0x3,  0x1, -1,  -1 },
    { "Ricco 3",                     0x3,  0x2, -1,  -1 },
    { "Blooper Race",                0x1E, 0x0, -1,  -1 },
};
const int kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);

namespace {
bool sPending      = false;
s32  sArea         = 0;
s32  sEpisode      = 0;
s32  sOverrideArea = -1;
s32  sExtraFlag    = -1;
}  // namespace

void request(s32 area, s32 episode, s32 overrideArea, s32 extraFlag) {
    sArea         = area;
    sEpisode      = episode;
    sOverrideArea = overrideArea;
    sExtraFlag    = extraFlag;
    sPending      = true;
}

bool pending() { return sPending; }

void execute() {
    if (!sPending) return;
    sPending = false;

    TFlagManager::smInstance->firstStart();
    TFlagManager::smInstance->resetCard();
    if (sExtraFlag > 0x10000) {
        TFlagManager::smInstance->setBool(true, sExtraFlag);
    } else if (sExtraFlag >= 0) {
        TFlagManager::smInstance->setShineFlag(sExtraFlag);
    }

    if (sOverrideArea >= 0) {
        gpApplication.mCurrentScene.mAreaID     = sOverrideArea;
        gpApplication.mCurrentScene.mEpisodeID = 0;
        gpApplication.mCurrentScene.mFlag      = 0;
        TFlagManager::smInstance->setBool(true, 0x30006);
    }
    gpApplication.mNextScene.mAreaID     = sArea;
    gpApplication.mNextScene.mEpisodeID = sEpisode;
    gpApplication.mNextScene.mFlag      = 0;

    TFlagManager::smInstance->setFlag(0x40002, 1);
    TFlagManager::smInstance->setFlag(0x3000B, 1);
    TFlagManager::smInstance->setFlag(0x3000C, 1);
    TFlagManager::smInstance->setFlag(0x3000D, 1);
    TFlagManager::smInstance->saveSuccess();
}

}  // namespace Warp
