#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/Strategic/HitActor.hxx>
#include <SMS/Strategic/ObjChara.hxx>

struct TModelDataLoadEntry {
    const char *mModelPath;
    u32 mModelFlags;
    u32 _0C;
};

/* Size -- 0x34 */
class TObjManager : public JDrama::TViewObj {
public:
    TObjManager(const char *);
    virtual ~TObjManager();

    virtual void load(JSUMemoryInputStream &) override;
    virtual TObjManager *searchF(u16, const char *) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void createModelData();
    virtual void createAnmData();
    virtual void createModelDataArray(const TModelDataLoadEntry *);

    void createModelDataArrayBase(const TModelDataLoadEntry *, const char *);
    MActorAnmData *getMActorAnmData();
    void *getModelDataKeeper();
    void manageObj(THitActor *obj);

    size_t mMaxObjs;  // _10
    size_t mObjCount; // _14
    JDrama::TViewObj **mObjAry;
    TObjChara *mObjChara;
    u32 _20;
    u32 _24;
    u32 _28;
    u32 _2C;
    u32 _30;
};