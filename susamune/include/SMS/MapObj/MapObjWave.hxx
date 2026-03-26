#pragma once

#include <JDrama/JDRViewObj.hxx>

class TMapObjWave : public JDrama::TViewObj {
public:
    TMapObjWave(const char *name);
    ~TMapObjWave() override;

    void load(JSUMemoryInputStream &in) override;
    void perform(u32, JDrama::TGraphics *) override;

    void initDraw();
    void draw();

    f32 getHeight(f32 x, f32 y, f32 z) const;
    f32 getWaveHeight(f32 x, f32 z) const;

    void noWave();
    void updateHeightAndAlpha();
    void updateTime();

public:
    f32 _10;
    f32 _14;
    f32 _18;
    u32 _1C;
    u32 _20;
    f32 _24;
    f32 _28;
    f32 _2C;
    f32 _30;
    f32 _34;
    f32 _38;
    f32 _3C;
    f32 _40;
    f32 _44;
    f32 _48;
    f32 _4C;
    f32 _50;
    f32 _54;
    f32 _58;
    f32 _5C;
    f32 _60;
    f32 _64;
    f32 _68;
    f32 _6C;
    f32 _70;
    f32 _74;
    f32 _78;
    u16 _7C;
    u16 _7E;
    u16 _80;
    u16 _82;
    u16 _84;
    u16 _86;
    u16 _88;
    u16 _8A;
    u16 _8C;
    u16 _8E;
    u16 _90;
    u16 _92;
    u32 _94;
    u16 _98;
    u16 _9A;
};

extern TMapObjWave *gpMapObjWave;