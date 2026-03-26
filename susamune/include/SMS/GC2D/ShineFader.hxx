#pragma once

#include <JSystem/JSupport/JSUInputStream.hxx>
#include <SMS/GC2D/SmplFader.hxx>

class TShineFader : public TSmplFader {
public:
    virtual ~TShineFader();
    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void update() override;

    void registFadeout(u16, u16);

    u16 _38;
};