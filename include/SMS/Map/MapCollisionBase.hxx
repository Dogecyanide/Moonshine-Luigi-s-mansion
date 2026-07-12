#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

#include <SMS/Map/BGCheck.hxx>

class TLiveActor;

enum CollisionType { COLLISION_STATIC, COLLISION_MOVE, COLLISION_WARP };

class TMapCollisionBase {
public:
    TMapCollisionBase() {}

    virtual void init(const char *, u16, const TLiveActor *);
    virtual void moveSRT(const TVec3f &, const TVec3f &, const TVec3f &);
    virtual void moveTrans(const TVec3f &);
    virtual void moveMtx(float (*)[4]);
    virtual void setUp();
    virtual void setUpTrans(const TVec3f &);
    virtual void remove();

    void initAllCheckData(s16, const f32 *, u16, const TLiveActor *);
    void initCheckData(s16, u16, const TLiveActor *);
    bool isSetUp() const;
    void setAllActor(const TLiveActor *);
    void setAllBGType(u16);
    void setAllData(s16);
    void setCheckData(const f32 *, const s16 *, TBGCheckData *, int);
    void setMtx(float (*)[4]);
    void setVertexData(u32, const TVec3f &, const TVec3f &, const TVec3f &);
    void update();
    void updateTrans(const TVec3f &);

    TBGCheckData *mCheckDatas;
    size_t _8;
    size_t mNumFaces;
    u32 mNumVerts;    // _10
    u32 mVertOffset;  // _14
    u32 mGroupCount;  // _18
    u32 *mGroups;     // _1C
    Mtx mGlobalTRS;
    TVec3f _50;
    u16 _5C;
    u16 _5E;  // padding
};