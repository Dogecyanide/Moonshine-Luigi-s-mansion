#pragma once

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/Strategic/ObjHitCheck.hxx>

class TStrategy : public JDrama::TViewObj {
public:
    TStrategy(const char *name);
    virtual ~TStrategy();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void loadAfter() override;
    virtual TNameRef *searchF(u16, const char *) override;

    TIdxGroupObj *mMapGroup;
    TIdxGroupObj *mEmptyGroup;
    TIdxGroupObj *mManagerGroup;
    TIdxGroupObj *mObjectGroup;
    TIdxGroupObj *mGraffitiGroup;
    TIdxGroupObj *mItemGroup;
    TIdxGroupObj *mPlayerGroup;
    TIdxGroupObj *mEnemyGroup;
    TIdxGroupObj *mBossGroup;
    TIdxGroupObj *mNPCGroup;
    TIdxGroupObj *mWaterPartyGroup;
    TIdxGroupObj *mInitializationGroup;
    u32 _40;
    u32 _44;
    u32 _48;
    u32 _4C;
    u16 mHitObjFlags;
};

extern TStrategy *gpStrategy;