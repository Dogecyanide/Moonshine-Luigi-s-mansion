#pragma once

#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

class TCameraMapTool {
public:
    TCameraMapTool(const TCameraMapTool &);
    virtual ~TCameraMapTool();

    void calcPosAndAt(TVec3f *, TVec3f *) const;
    void load(JSUMemoryInputStream &);

    TVec3f mTranslation;
    TVec3f mRotation;
    u32 mType;
    u32 _28;
    u32 _2C;
};