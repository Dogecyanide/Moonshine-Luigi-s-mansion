#pragma once

#include <JSystem/JParticle/JPABaseParticle.hxx>
#include <JSystem/JSupport/JSUList.hxx>

class JPABaseEmitter : public JSUPtrLink {
public:
    JPABaseEmitter();

    void calc();

    JPABaseParticle *calcCreateParticle();
    void calcCurrentRateTimerStep();
    void calcEmitterGlobalParams();
    void calcKeyFrameAnime();
    bool checkMaxFrame();
    bool checkStartFrame();
    JPABaseParticle *createChildParticle(JPABaseParticle *particle);
    JPABaseParticle *createParticle();
    void deleteAllParticle();
    void doChildParticle();
    void doParticle();
    void drawEmitterCallback();
    f32 getAspect();
    void getEmitterGlobalTranslation(TVec3f &);
    f32 getFovy();
    void loadBaseEmitterBlock(JPABaseParticle * /*JPADataBlock **/);
    void setGlobalRTMatrix(Mtx);
    void setGlobalSRTMatrix(Mtx);

    u32 _10[0xE4 / 4];
    JSUPtrList mParticleList;       // _F4
    JSUPtrList mChildParticleList;  // _100
    u32 _10C[0x48 / 4];
    TVec3f mSize1;  // _154
    u32 _160[0x14 / 4];
    TVec3f mSize3;  // _174
    u32 _180[0xB0 / 4];
};