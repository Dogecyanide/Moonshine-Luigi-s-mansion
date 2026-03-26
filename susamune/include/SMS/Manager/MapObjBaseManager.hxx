#include <Dolphin/types.h>
#include <SMS/Manager/LiveManager.hxx>
#include <SMS/MapObj/MapObjBase.hxx>

class TMapObjBaseManager : public TLiveManager {
public:
    TMapObjBaseManager(const char *);
    virtual ~TMapObjBaseManager();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void clipActors(JDrama::TGraphics *) override;
    virtual void createModelData() override;

    u32 getObjNumWithActorType(u32) const;
    TLiveActor *makeObjAppear(f32, f32, f32, u32, bool);
    TLiveActor *makeObjAppear(u32);
    void makeObjAppeared(u32);

    static u32 getActorTypeByEventID(u32);
    static TMapObjBase *newAndRegisterObj(const char *, const TVec3f &, const TVec3f &,
                                          const TVec3f &);
    static TMapObjBase *newAndRegisterObjByEventID(u32, const char *);

    f32 _38;
    f32 _3C;
};