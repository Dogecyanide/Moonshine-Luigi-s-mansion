#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>

#include <JSystem/JGeometry/JGMVec.hxx>

class TGraphWeb;

class TSplineRail {
public:
    TSplineRail(TGraphWeb *);

    void getPosAndRot(f32, TVec3f *posOut, TVec3f *rotOut);
};

class TSplinePath {
public:
    TSplinePath(int);

    f32 calcSpline(f32, f32 *, f32 *, f32 *);
    void calcTable();
    Vec getPoint(f32);
    void makeTable(f32 *, f32 *, f32 *);
};