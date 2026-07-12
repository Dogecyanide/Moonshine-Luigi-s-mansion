#pragma once

#include <JSystem/JGeometry/JGMVec.hxx>

class JPABaseEmitter;
class JPABaseParticle;

template <typename EmitterT = JPABaseEmitter *> class JPACallBackBase {
public:
    virtual void execute(EmitterT) {}
    virtual void executeAfter(EmitterT) {}
    virtual void draw(EmitterT) {}
};

template <typename EmitterT = JPABaseEmitter *, typename ParticleT = JPABaseParticle *>
class JPACallBackBase2 {
public:
    virtual void execute(EmitterT, ParticleT) {}
    virtual void draw(EmitterT, ParticleT) {}
};

class JPABaseParticle {
public:
    virtual void executeParticleCallBack(JPABaseEmitter *);
    virtual void drawParticleCallBack();

    void calcGlobalPosition();
    void getCurrentPosition(TVec3f &);
    void incTimer();
    void initGlobalPosition();

    u8 _00[0x10];
    u16 mStateFlags;
    u8 _12[0x34];
    f32 mTimerCurrent;
    f32 mTimerLerp;
    f32 mTimerMax;
};