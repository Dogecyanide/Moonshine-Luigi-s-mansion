#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUList.hxx>

class JKRDisposer {
public:
    JKRDisposer();
    virtual ~JKRDisposer();

    u32 _4;
    JSUPtrLink mPtrLink;  // _8
};