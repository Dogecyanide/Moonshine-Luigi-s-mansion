#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRDirector.hxx>
#include <JSystem/JDrama/JDRDisplay.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Player/Mario.hxx>

class TMovieDirector : public JDrama::TDirector {
public:
    TMovieDirector();
    virtual ~TMovieDirector();

    s32 direct() override;

    s32 decideNextMode(s32 *);
    const char *getStreamMovieName(u32);
    void rsetup();
    s32 setup(JDrama::TDisplay *, TMarioGamePad *);
    void setupThreadFunc(void *);

    bool _18;
    u32 _1C;
    u32 _20;
    u32 _24;
    u32 _28;
    u32 _2C;
    u16 _30;
    u32 _34;
    u32 _38;
};