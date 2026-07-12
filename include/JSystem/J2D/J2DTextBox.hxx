#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JSupport/JSUInputStream.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <JSystem/JUtility/JUTFont.hxx>
#include <JSystem/JUtility/JUTResFont.hxx>

enum class J2DTextBoxHBinding { Center, Right, Left };

enum class J2DTextBoxVBinding { Center, Bottom, Top };

class J2DTextBox : public J2DPane {
public:
    J2DTextBox() : J2DPane() {}
    J2DTextBox(const ResFONT *, const char *);
    J2DTextBox(J2DPane *, JSURandomInputStream *, bool);
    J2DTextBox(u32, const JUTRect &, const ResFONT *, const char *, J2DTextBoxHBinding,
               J2DTextBoxVBinding);
    virtual ~J2DTextBox();

    virtual void resize(int, int);
    virtual bool setConnectParent(bool);
    virtual void drawSelf(int, int);
    virtual void drawSelf(int, int, Mtx *);

    void initiate(const ResFONT *, const char *, J2DTextBoxHBinding, J2DTextBoxVBinding);

    char *getStringPtr() const;
    size_t setString(const char *, ...);
    void setFont(JUTFont *);
    void draw(int, int);

    JUTFont *mFont;  // _EC
    JUtility::TColor mGradientTop;
    JUtility::TColor mGradientBottom;
    J2DTextBoxHBinding mHBinding;  // _F8
    J2DTextBoxVBinding mVBinding;  // _FC
    char *mStrPtr;
    u32 _104;
    u32 _108;
    size_t mCharSpacing;
    size_t mNewlineSize;
    size_t mCharSizeX;
    size_t mCharSizeY;
    JUtility::TColor mColorMask;
    JUtility::TColor mAdvanceColor;
    bool mInitialized;
};