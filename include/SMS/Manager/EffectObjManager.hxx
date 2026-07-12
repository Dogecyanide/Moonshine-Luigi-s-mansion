#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGadget/Allocator.hxx>
#include <JSystem/JGadget/List.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/Enemy/EffectObjBase.hxx>
#include <SMS/Strategic/HitActor.hxx>
#include <SMS/Strategic/ObjChara.hxx>

/* Size -- 0x34 */
class TEffectObjManager : public JDrama::TViewObj {
public:
    TEffectObjManager(const char *);
    virtual ~TEffectObjManager();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;

    char *mName;
    u16 mKeyCode;
    u32 _0C;
    u32 _10;
    JGadget::TList<TEffectObjBase *> mEffectObjList;
    TEffectObjBase **mEffectObjs;

    static constexpr size_t DefaultEffectNum = 5;
};

extern TEffectObjManager *gpEffectObjManager;