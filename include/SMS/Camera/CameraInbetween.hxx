#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

class TCameraInbetween {

public:
    u32 _00[0x30 / 4];   // 0x0000
    TVec3f mSubjectPos;  // 0x0030
};