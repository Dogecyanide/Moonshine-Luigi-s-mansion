#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JUtility/JUTRect.hxx>

class J2DOrthoGraph : public J2DGrafContext {
public:
    J2DOrthoGraph(int x, int y, int w, int h);
    J2DOrthoGraph(const JUTRect &);
    ~J2DOrthoGraph();

    void setPort();
    void setLookat();
    void scissorBounds(JUTRect *, JUTRect *);

    JUTRect _D8;
    u32 _DC;
    u32 _E0;
    u32 _E4;
    f32 _E8;
    f32 _EC;
};