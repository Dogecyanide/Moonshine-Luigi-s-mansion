#pragma once

#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JUtility/JUTRect.hxx>

class J2DWindow : public J2DPane {
public:
    JUTRect mFillRect;
    u32 _FC[0x40 / 4];
};