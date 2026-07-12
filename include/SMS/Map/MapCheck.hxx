#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <SMS/Map/BGCheck.hxx>

class TBGWallCheckRecord {
public:
    TVec3f mPosition;
    f32 mRadius;
    size_t mCollideMax;
    size_t mNumWalls;
    u32 mIgnoreFlags;
    TBGCheckData *mWalls[4];
};