#include "susamune/settings_menu.hxx"
#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DTextBox.hxx"
#include "SMS/Manager/FlagManager.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "SMS/System/Application.hxx"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

const char *sMainStageNames[] = {
  "Airstrip",   "Delfino Plaza", "Bianco Hills",  "Ricco Harbor",   "Gelato Beach",
  "Pinna Park", "Sirena Beach",  "Sirena Hotel?", "Pianta Village", "Noki Bay",
};

const char *sTabNames[] = {"Stages", "Presets"};

const WarpDescriptor sPresetDescriptors[] = {
  {"Delfino - Bianco Plant",      0x1,  0x0, 0x1, -1},
  {"Delfino - Bianco Chase",      0x1,  0x1, 0x1, -1},
  {"Delfino - Ricco Plant",       0x1,  0x5, 0x1, -1},
  {"Pianta 1",                    0x8,  0x0, -1,  -1},
  {"Pianta 2",                    0x8,  0x1, -1,  -1},
  {"Pianta 3",                    0x8,  0x2, -1,  -1},
  {"Delfino - Pianta Exit",       0x1,  0x5, 0x8, -1},
  {"Pachinko",                    0x16, 0x0, -1,  -1},
  {"Delfino - Ricco Plant (bg2)", 0x1,  0x5, 0x1, 94},
  {"Ricco 1",                     0x3,  0x0, -1,  -1},
  {"Ricco 2",                     0x3,  0x1, -1,  -1},
  {"Ricco 3",                     0x3,  0x2, -1,  -1},
  {"Blooper Race",                0x1E, 0x0, -1,  -1},
  // TODO: doesnt work with force plaza events, need to have the ricco flag (probably 0x10385?) on
  // {"Delfino - Ricco Exit",        0x1,  0x5, 0x3, 0x50001 },
};

SettingsMenu::SettingsMenu() {
  mShown                = false;
  mChangeStageReady     = false;
  mSelectedArea         = 0;
  mSelectedEpisode      = 0;
  mSelectedPreset       = 0;
  mTab                  = PRESETS;
  mInfoText             = new J2DTextBox(gpSystemFont->mFont, "00000000");
  mInfoText->mCharSizeX = 20;
  mInfoText->mCharSizeY = 20;

  for (u32 tab = 0; tab < NUM_TABS; tab++) {
    J2DTextBox *tabText = new J2DTextBox(gpSystemFont->mFont, sTabNames[tab]);
    tabText->mCharSizeX = textSizeX;
    tabText->mCharSizeY = textSizeY;
    mTabTexts[tab]      = tabText;
  }

  for (u32 stage = 0; stage < NUM_STAGES; stage++) {
    J2DTextBox *stageText = new J2DTextBox(gpSystemFont->mFont, sMainStageNames[stage]);
    stageText->mCharSizeX = textSizeX;
    stageText->mCharSizeY = textSizeY;
    mStageTexts[stage]    = stageText;

    char *episodeStr             = new char[3 * NUM_EPISODES + 1];
    episodeStr[3 * NUM_EPISODES] = 0;
    for (u32 episode = 0; episode < NUM_EPISODES; episode++) {
      episodeStr[episode * 3]     = ' ';
      episodeStr[episode * 3 + 1] = (char)(episode + 1 + 0x30);
      episodeStr[episode * 3 + 2] = ' ';
    }
    J2DTextBox *episodeText = new J2DTextBox(gpSystemFont->mFont, episodeStr);
    episodeText->mCharSizeX = textSizeX / 1.5;
    episodeText->mCharSizeY = textSizeY;
    mEpisodeTexts[stage]    = episodeText;
  }

  for (u32 preset = 0; preset < NUM_PRESETS; preset++) {
    J2DTextBox *presetText = new J2DTextBox(gpSystemFont->mFont, sPresetDescriptors[preset].name);
    presetText->mCharSizeX = textSizeX;
    presetText->mCharSizeY = textSizeY / 1.25;
    mPresetWarps[preset]   = presetText;
  }
}

void SettingsMenu::draw(J2DOrthoGraph *ortho) {
  if (!mShown) {
    return;
  }

  J2DFillBox(frameOutset, frameOutset, (640 - frameOutset * 2), (448 - frameOutset * 2),
             {64, 64, 64, 220});

  mInfoText->mGradientBottom = {255, 0, 128, 255};
  mInfoText->mGradientTop    = {255, 0, 128, 255};
  mInfoText->draw(200, 200);
  
  int row_x = frameOutset + frameInset;
  int row_y = frameOutset + frameInset + textSizeY;
  for (int tab = 0; tab < NUM_TABS; tab++) {
    J2DTextBox *tabText = mTabTexts[tab];
    if (mTab == tab) {
      tabText->mGradientBottom = {255, 0, 128, 255};
      tabText->mGradientTop    = {255, 0, 128, 255};
    } else {
      tabText->mGradientBottom = {255, 255, 255, 255};
      tabText->mGradientTop    = {255, 255, 255, 255};
    }
    tabText->draw(row_x, row_y);
    row_x += tabWidth;
  }
  row_x = frameOutset + frameInset;
  row_y += tabGap;

  switch (mTab) {
  case PRESETS: {
    for (u32 preset = 0; preset < ARRAY_SIZE(sPresetDescriptors); preset++) {
      J2DTextBox *presetWarp = mPresetWarps[preset];
      if (preset == mSelectedPreset) {
        presetWarp->mGradientBottom = {255, 0, 0, 255};
        presetWarp->mGradientTop    = {255, 0, 0, 255};
      } else {
        presetWarp->mGradientBottom = {255, 255, 255, 255};
        presetWarp->mGradientTop    = {255, 255, 255, 255};
      }
      presetWarp->draw(row_x, row_y);
      row_y += textSizeY;
    }
    break;
  }
  case STAGES: {
    char epName[4];
    for (u32 stage = 0; stage < NUM_STAGES; stage++) {
      if (stage == mSelectedArea) {
        mStageTexts[stage]->mGradientBottom = {255, 0, 0, 255};
        mStageTexts[stage]->mGradientTop    = {255, 0, 0, 255};
      } else {
        mStageTexts[stage]->mGradientBottom = {255, 255, 255, 255};
        mStageTexts[stage]->mGradientTop    = {255, 255, 255, 255};
      }
      mStageTexts[stage]->draw(row_x, row_y);

      int col_x        = row_x + 200;
      char *episodeStr = mEpisodeTexts[stage]->getStringPtr();
      for (u32 episode = 0; episode < NUM_EPISODES; episode++) {
        if (stage == mSelectedArea && episode == mSelectedEpisode) {
          episodeStr[episode * 3]     = '[';
          episodeStr[episode * 3 + 2] = ']';
        } else {
          episodeStr[episode * 3]     = ' ';
          episodeStr[episode * 3 + 2] = ' ';
        }
      }
      mEpisodeTexts[stage]->draw(col_x, row_y);

      row_y += textSizeY + 5;
    }
    break;
  }
  default:
    break;
  }
}

void SettingsMenu::processInput(TMarioGamePad *controller) {
  u32 input      = controller->mButtons.mInput;
  u32 rapidInput = controller->mButtons.mRapidInput;
  if ((input & TMarioGamePad::Y) && (rapidInput & TMarioGamePad::START)) {
    mShown = !mShown;
    return;
  }
  if (!mShown)
    return;

  if (rapidInput & TMarioGamePad::A) {
    // todo should not just be a flag but rather store separtely what ep & stage have been
    // confirmed
    mChangeStageReady = true;
    mShown            = false;
    return;
  }

  if (rapidInput & TMarioGamePad::L) {
    mTab = (SettingsTab)((int)mTab - 1);
  }
  if (rapidInput & TMarioGamePad::R) {
    mTab = (SettingsTab)((int)mTab + 1);
  }
  if ((int)mTab < 0) {
    mTab = (SettingsTab)((int)mTab + NUM_TABS);
  } else if ((int)mTab >= NUM_TABS) {
    mTab = (SettingsTab)((int)mTab - NUM_TABS);
  }

  switch (mTab) {
  case PRESETS: {
    if (rapidInput & TMarioGamePad::CSTICK_UP) {
      mSelectedPreset--;
    } else if (rapidInput & TMarioGamePad::CSTICK_DOWN) {
      mSelectedPreset++;
    }

    const int numPresets = ARRAY_SIZE(sPresetDescriptors);
    if (mSelectedPreset < 0) {
      mSelectedPreset += numPresets;
    } else if (mSelectedPreset >= numPresets) {
      mSelectedPreset -= numPresets;
    }
    break;
  }
  case STAGES: {
    if (rapidInput & TMarioGamePad::CSTICK_UP) {
      mSelectedArea--;
    } else if (rapidInput & TMarioGamePad::CSTICK_DOWN) {
      mSelectedArea++;
    }

    if (rapidInput & TMarioGamePad::CSTICK_LEFT) {
      mSelectedEpisode--;
    } else if (rapidInput & TMarioGamePad::CSTICK_RIGHT) {
      mSelectedEpisode++;
    }

    if (mSelectedArea < 0) {
      mSelectedArea += NUM_STAGES;
    } else if (mSelectedArea >= NUM_STAGES) {
      mSelectedArea -= NUM_STAGES;
    }

    if (mSelectedEpisode < 0) {
      mSelectedEpisode += NUM_EPISODES;
    } else if (mSelectedEpisode >= NUM_EPISODES) {
      mSelectedEpisode -= NUM_EPISODES;
    }
    break;
  }
  default:
    break;
  }
}

void SettingsMenu::changeStageHook() {
  if (mChangeStageReady) {

    s32 selectedArea    = 0;
    s32 selectedEpisode = 0;
    s32 currOverride    = -1;
    s32 extraFlag       = -1;
    switch (mTab) {
    case PRESETS: {
      selectedArea    = sPresetDescriptors[mSelectedPreset].area;
      selectedEpisode = sPresetDescriptors[mSelectedPreset].episode;
      currOverride    = sPresetDescriptors[mSelectedPreset].overrideArea;
      extraFlag       = sPresetDescriptors[mSelectedPreset].extraFlag;
      break;
    }
    case STAGES: {
      selectedArea    = mSelectedArea;
      selectedEpisode = mSelectedEpisode;
      break;
    }
    default:
      break;
    }
    TFlagManager::smInstance->firstStart();
    TFlagManager::smInstance->resetCard();
    if (extraFlag > 0x10000) {
      TFlagManager::smInstance->setBool(true, extraFlag);
    } else if (extraFlag >= 0) {  // use lower ID space for shine IDs instead
      TFlagManager::smInstance->setShineFlag(extraFlag);
    }

    // override current scene, for proper exit in delfino
    if (currOverride >= 0) {
      gpApplication.mCurrentScene.mAreaID    = currOverride;
      gpApplication.mCurrentScene.mEpisodeID = 0x0;
      gpApplication.mCurrentScene.mFlag      = 0;
      // got shine in previous stage - so that we dont get the death animation instead
      TFlagManager::smInstance->setBool(true, 0x30006);
    }
    gpApplication.mNextScene.mAreaID    = selectedArea;
    gpApplication.mNextScene.mEpisodeID = selectedEpisode;
    gpApplication.mNextScene.mFlag      = 0;

    // give coins
    TFlagManager::smInstance->setFlag(0x40002, 1);
    // intro FMV disables
    TFlagManager::smInstance->setFlag(0x3000B, 1);
    TFlagManager::smInstance->setFlag(0x3000C, 1);
    TFlagManager::smInstance->setFlag(0x3000D, 1);
    TFlagManager::smInstance->saveSuccess();
  }
}

void SettingsMenu::setInfo(u32 x) {
  char *mInfo = mInfoText->getStringPtr();
  for (int i = 7; i >= 0; i--, x >>= 4) {
    int xb   = x & 0xF;
    char c   = (char)(((xb <= 9) ? 0x30 : 0x37) + xb);
    mInfo[i] = c;
  }
}