#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPicture.hxx>
#include <JSystem/J2D/J2DWindow.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <SMS/GC2D/ConsoleStr.hxx>

class TGCConsole2 {

public:
    struct HealthPoint {
        J2DPicture *mActivePicture;
        J2DPicture *mInactivePicture;
    };

    u32 _00[0x24 / 4];    // 0x0000
    s32 mShineCount;      // 0x0024
    s32 mCurWater;        // 0x0028
    u32 _2C[0x10 / 4];    // 0x002C
    bool mIsRedCoinCard;  // 0x003C
    bool _3D;
    bool mIsTimerCard;  // 0x003E
    bool _3F;
    bool _40;
    bool _41;
    bool _42;
    bool mIsTimerCardActive;
    bool mIsDEBSAlert;       // 0x0044
    bool mWaterCardRising;   // 0x0045
    bool mIsWaterCard;       // 0x0046
    u8 _02;                  // 0x0047
    u16 _03;                 // 0x0048
    bool mIsResetTimer;      // 0x004A
    bool mWaterCardFalling;  // 0x004B
    u32 _05[0x28 / 4];       // 0x004C
    s16 mRedCoinCardTimer;   // 0x0074
    s16 mTimerCardTimer;     // 0x0076
    u32 _78[0x1C / 4];
    TConsoleStr *mConsoleStr;                // 0x0094
    u16 _06;                                 // 0x0098
    JUtility::TColor mWaterLeftPanelColor;   // 0x009A
    JUtility::TColor mWaterRightPanelColor;  // 0x009E
    JUtility::TColor mJuiceCardOrangeColor;  // 0x00A2
    JUtility::TColor mJuiceCardPurpleColor;  // 0x00A6
    JUtility::TColor mJuiceCardPinkColor;    // 0x00AA
    u16 _07;                                 // 0x00AE
    J2DScreen *mMainScreen;                  // 0x00B0
    u32 _B4[0xC8 / 4];                       // 0x00B4
    HealthPoint mHealthPoints[9];            // 0x017C
    u32 _1C4[2];
    s8 mHealthCount;               // 0x01CC
    JUTRect mHealthPointRects[9];  // 0x01D0
    u32 _260[0x40 / 4];
    J2DPicture *mWaterTopPanel;   // 0x02A0
    u32 _09[0x14 / 4];            // 0x02A4
    u32 mWaterCardTopHeight;      // 0x02B8
    u32 _10[0x170 / 4];           // 0x02BC
    TExPane *mRedCoinPanelBack;   // 0x042C
    TExPane *mRedCoinPanel;       // 0x0430
    TExPane *_434;                // 0x0434
    TExPane *_438;                // 0x0438
    TExPane *_43C;                // 0x043C
    TExPane *_440;                // 0x0440
    TExPane *_444;                // 0x0444
    u32 _448[0xBC / 4];           // 0x0448
    TExPane *mTimerPanelNormal;   // 0x0504
    TExPane *mTimerPanelRush;     // 0x0508
    u32 _50C[0x8 / 4];            // 0x050C
    bool mIsTimerPanelRush;       // 0x0514
    s32 mTimerSecondsLeft;        // 0x0518
    u32 _51C[0x8 / 4];            // 0x051C
    TExPane *mTelopPanel;         // 0x0524
    TExPane *mTelopWindow;        // 0x0528
    J2DTextBox *mTelopTextFront;  // 0x052C
    J2DTextBox *mTelopTextBack;   // 0x0530
    u32 _534;
    JUTRect mTelopPosition;  // 0x0538
    u32 _548[0x10 / 4];
    size_t mTelopPixelWidth;  // 0x0558
    u32 _55C[0xC / 4];
    f32 mDEBSScrollSpeed;  // 0x0568
    f32 mDEBSPositionX;    // 0x056C
    u32 _570[2];

    // 0x1C4 = TExPane *LifeMeter
    // 0x2F8 = TExPane *WaterBackPanel

    // 0x314 = J2DPicture *FruitText
};