#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <SMS/System/MarNameRefGen.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGadget/Vector.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

#include <SMS/Camera/CameraMapTool.hxx>

class TCubeGeneralInfo : public JDrama::TNameRef {
public:
    TCubeGeneralInfo(const char *name);
    ~TCubeGeneralInfo() override;

    void load(JSUMemoryInputStream &in) override;

    TVec3f mTranslation;
    TVec3f mRotation;
    TVec3f mScale;
    u16 _30;
    u16 _32; // Always 2
    u32 mDataNo;
};

class TCubeCameraInfo : public JDrama::TNameRef {
public:
    TCubeCameraInfo(const char *name);
    ~TCubeCameraInfo() override;

    void load(JSUMemoryInputStream &in) override;

    TVec3f mTranslation;
    TVec3f mRotation;
    TVec3f mScale;
    u16 _30;
    u16 _32;  // Always 2
    u32 mDataNo;
    TCameraMapTool *mMapTool;
};