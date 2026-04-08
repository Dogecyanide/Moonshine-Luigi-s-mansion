#include "susamune/settings_menu.hxx"
#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DTextBox.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "SMS/System/Application.hxx"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*x));

const char* sMainStageNames[] = {
    "Bianco Hills",
    "Ricco Harbor",
    "Gelato Beach",
    "Pinna Park",
    "Sirena Beach",
    "Pianta Village",
    "Noki Bay",
};

SettingsMenu::SettingsMenu() {
    mShown = false;
    mChangeStageReady = false;
    mSelectedArea = 0;
    mSelectedEpisode = 0;

    for (u32 stage = 0; stage < NUM_STAGES; stage++) {
        J2DTextBox* stageText = new J2DTextBox(gpSystemFont->mFont, sMainStageNames[stage]);
        stageText->mCharSizeX = textSizeX;
        stageText->mCharSizeY = textSizeY;
        mStageTexts[stage] = stageText;
        
        for (u32 episode = 0; episode < NUM_EPISODES; episode++) {
            char* episodeStr = new char[4];
            snprintf(episodeStr, 4, "%d", (int)episode+1);
            J2DTextBox* episodeText = new J2DTextBox(gpSystemFont->mFont, episodeStr);
            episodeText->mCharSizeX = textSizeX;
            episodeText->mCharSizeY = textSizeY;
            mEpisodeTexts[stage][episode] = episodeText;
        }
    }
}

void SettingsMenu::draw(J2DOrthoGraph* ortho) {
    if (!mShown) { 
        return;
    }

    J2DFillBox(frameOutset,frameOutset, (640-frameOutset*2), (448-frameOutset*2), {64,64, 64, 127});

    int row_x = frameOutset + frameInset;
    int row_y = frameOutset + frameInset + textSizeY;
    char epName[4];
    for (u32 stage = 0; stage < NUM_STAGES; stage++) {
        if (stage == mSelectedArea) {
            mStageTexts[stage]->mGradientBottom = {255,0,0,255};
            mStageTexts[stage]->mGradientTop = {255,0,0,255};
        } else {
            mStageTexts[stage]->mGradientBottom = {255,255,255,255};
            mStageTexts[stage]->mGradientTop = {255,255,255,255};
        }
        mStageTexts[stage]->draw(row_x, row_y);
        
        int col_x = row_x + 200;
        for (u32 episode = 0; episode < NUM_EPISODES; episode++) {
            if (stage == mSelectedArea && episode == mSelectedEpisode) {
                mEpisodeTexts[stage][episode]->mGradientBottom = {255,0,0,255};
                mEpisodeTexts[stage][episode]->mGradientTop = {255,0,0,255};
            } else {
                mEpisodeTexts[stage][episode]->mGradientBottom = {255,255,255,255};
                mEpisodeTexts[stage][episode]->mGradientTop = {255,255,255,255};
            }
            mEpisodeTexts[stage][episode]->draw(col_x, row_y);
            col_x += textSizeX + 5;
        }
        row_y += textSizeY + 5;
    }
}

void SettingsMenu::processInput(TMarioGamePad* controller) {
    u32 frameInput = controller->mButtons.mFrameInput;
    u32 rapidInput = controller->mButtons.mRapidInput;
    if (rapidInput & TMarioGamePad::DPAD_UP) {
        mShown = !mShown;
        return;
    }
    if (!mShown) return;
    
    if (rapidInput & TMarioGamePad::A) {
        // todo should not just be a flag but rather store separtely what ep & stage have been confirmed
        mChangeStageReady = true; 
        mShown = false;
        return;
    }

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
}

void SettingsMenu::changeStageHook() {
    if (mChangeStageReady) {
        gpApplication.mNextScene.mAreaID = mSelectedArea + 0x2; // offset for normal stages
        gpApplication.mNextScene.mEpisodeID = mSelectedEpisode;
        gpApplication.mNextScene.mFlag = 0;
    }
}

/*
void init_menu() {
    MenuWidget** stageSelectButtons = (MenuWidget**)malloc(sizeof(MenuWidget*) * 3);

}*/

