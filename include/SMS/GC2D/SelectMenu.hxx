#pragma once

#include <JSystem/J2D/J2DPicture.hxx>
#include <JSystem/J2D/J2DSetScreen.hxx>
#include <JSystem/J2D/J2DTextBox.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JUtility/JUTTexture.hxx>
#include <SMS/G2D/BoundPane.hxx>
#include <SMS/G2D/ExPane.hxx>
#include <SMS/GC2D/SelectGrad.hxx>

class TSelectDir;
class TSelectShineManager;

class TSelectMenu : public JDrama::TViewObj {
public:
    TSelectMenu(const char *);
    virtual ~TSelectMenu();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    u32 mState;
    u32 _14[0xC / 4];
    J2DSetScreen *mScreen;           // 0x0020
    TExPane *mMask1;                 // 0x0024
    TExPane *mMask2;                 // 0x0028
    J2DPane *mMap;                   // 0x002C
    J2DPane *mPaneDynName;           // 0x0030
    TBoundPane *mBoundPaneDynName1;  // 0x0034
    TBoundPane *mBoundPaneDynName2;  // 0x0038
    u32 _3C;
    TExPane *mS_0;             // 0x0040
    J2DTextBox *mSttxTextBox;  // 0x0044
    J2DPicture *mPicture0;     // 0x0048
    J2DPicture *mPicture1;     // 0x004C
    u32 _50;
    bool _54;
    JUTRect _58;
    TExPane *m0_0;             // 0x0068
    J2DTextBox *m0ttxTextBox;  // 0x006C
    u32 _70[0x10 / 4];
    JUTTexture *mShineCountNumberTex[8];  // 0x0080
    J2DPane *mI_0;                        // 0x00A0
    J2DPane *mSc_0;                       // 0x00A4
    JUTTexture *mCoinCountNumberTex[10];  // 0x00A8
    JUTTexture *mScMark1;
    JUTTexture *mScMark0;
    u32 _D8[0x2C / 4];
    J2DPane *mA_l;  // 0x0104
    J2DPane *mA_r;  // 0x0108
    bool _10C;
    u8 _10D;
    JUTRect mLeftArrowPos;          // 0x0110
    JUTRect mRightArrowPos;         // 0x0120
    TSelectShineManager *mManager;  // 0x0130
    TSelectDir *mDir;               // 0x0134
    u8 _138;
    u8 mCloseTime;
    u8 mAreaID;                         // 0x013A
    s8 mEpisodeID;                      // 0x013B
    s8 mEpisodeCount;                   // 0x013C
    u16 _13E;
    JUtility::TColor mSelectedColor;    // 0x0140
    JUtility::TColor mUnselectedColor;  // 0x0144
    u8 mSelectedFlickerTime;            // 0x0148
    u8 mUnselectedAlphaMix;             // 0x0149
    f32 mAnmDeltaTime;                  // 0x014C
    u8 mSelectionState[8];              // 0x0150
    void *mStageBMGData;                // 0x0158
    void *mScenarioBMGData;             // 0x015C
    JUtility::TColor _160;              // 0x0160
    JUtility::TColor _164;              // 0x0164
    s16 _168;
    s16 _16A;
    s16 mCloseDuration;
};