#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/J3D/J3DCluster.hxx>
#include <JSystem/J3D/J3DJoint.hxx>
#include <JSystem/J3D/J3DNode.hxx>
#include <JSystem/J3D/J3DPacket.hxx>
#include <JSystem/J3D/J3DVertex.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JUtility/JUTNameTab.hxx>

class J3DModelHierarchy;
class J3DNode;
class J3DMaterial;
class J3DShape;
class J3DMtxCalc;
class J3DTexture;
class J3DDeformData;
class J3DVtxColorCalc;

class J3DModelData {
public:
    J3DModelData();
    virtual ~J3DModelData();

    void clear();
    int entryMatColorAnimator(J3DAnmColor *);
    int entryTevRegAnimator(J3DAnmTevRegKey *);
    int entryTexMtxAnimator(J3DAnmTextureSRTKey *);
    bool isDeformableVertexFormat() const;
    void makeHierarchy(J3DNode *, const J3DModelHierarchy **);
    int removeMatColorAnimator(J3DAnmColor *);
    int removeTevRegAnimator(J3DAnmTevRegKey *);
    int removeTexMtxAnimator(J3DAnmTextureSRTKey *);
    int setMatColorAnimator(J3DAnmColor *, J3DMatColorAnm *);
    int setTevRegAnimator(J3DAnmTevRegKey *, J3DTevKColorAnm *);
    int setTexMtxAnimator(J3DAnmTextureSRTKey *, J3DTexNtxAnm *, J3DTexNtxAnm *);
    int setTexNoAnimator(J3DAnmTexPattern *, J3DTexNoAnm *);

    u16 getJointNum() const { return mJointNum; }
    u16 getMaterialNum() const { return mMaterialNum; }

    const void *_4;
    J3DModelHierarchy *mHierarchy;
    u32 _C;
    J3DJoint *mRootNode;
    J3DMtxCalc *mMtxCalc;
    u16 _18;
    u16 mHasBillboard;
    u16 mJointNum;
    u16 _1E;
    J3DJoint **mJoints;
    u16 mMaterialNum;
    u16 _26;
    J3DMaterial **mMaterials;
    u16 mShapeNum;
    u16 _2E;
    J3DShape **mShapes;
    u16 _34;
    u16 _36;
    J3DMaterial *_38;
    J3DVertexData mVertexData;
    u32 _80;
    u16 _84;
    u16 _86;
    u8 *_88;
    u16 *_8C;
    f32 *_90;
    Mtx *_94;
    J3DDrawMtxData mDrawMtxData;
    u32 _A4;
    JUTNameTab *_A8;
    J3DTexture *_AC;
    JUTNameTab *mJointNames;
    JUTNameTab *mMaterialNames;
    JUTNameTab *_B8;
};

class J3DModel {
public:
    J3DModel(J3DModelData *, u32, u32);

    virtual void update();
    virtual void entry();
    virtual void calc();
    virtual void viewCalc();

    void calcBBoard();
    void calcBumpMtx();
    void calcNrmMtx();
    void calcWeightEnvelopeMtx();
    void entryModelData(J3DModelData *, u32, u32);
    void initialize();
    void lock();
    void unlock();
    void makeDL();
    void prepareShapePackets();
    void setSkinDeform(J3DSkinDeform *, J3DDeformAttachFlag);
    virtual ~J3DModel();

    J3DModelData *getModelData() const { return mModelData; }
    Mtx *getAnmMtx(int index) { return &mJointArray[index]; }
    const Mtx *getAnmMtx(int index) const { return &mJointArray[index]; }
    Mtx *getBaseTRMtx() { return &mBaseMtx; }

    J3DModelData *mModelData;
    u32 _8;
    void (*mCalcCallback)(J3DModel *, u32);
    u32 _10;
    TVec3f mBaseScale;
    Mtx mBaseMtx;
    u8 *mScaleFlags;
    u8 *mEnvelopeScaleFlags;
    Mtx *mJointArray;
    Mtx *mWeightEnvelopeMatrices;
    Mtx **mDrawMtxBuf[2];
    Mtx33 **mNrmMtxBuf[2];
    Mtx33 ***mBumpMtxBuf[2];
    u32 _78;
    u32 mCurrentViewNo;
    J3DMatPacket *mMatPackets;
    J3DShapePacket *mShapePackets;
    J3DDeformData *mDeformData;
    J3DSkinDeform *mSkinDeform;
    J3DVtxColorCalc *mVertexColorCalc;
    void *_94;
    J3DVertexBuffer *mVtxBuffer;
    void *_9C;
};

struct J3DModelHierarchy {
    u16 mType;
    u16 mValue;
};

static_assert(sizeof(J3DModelData) == 0xBC, "J3DModelData layout changed");
static_assert(sizeof(J3DModel) == 0xA0, "J3DModel layout changed");
