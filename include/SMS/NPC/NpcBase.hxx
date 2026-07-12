#pragma once

#include <Dolphin/types.h>
#include <SMS/Enemy/SpineEnemy.hxx>

class TBaseNPC : public TSpineEnemy {
public:
    TBaseNPC(u32 type, const char *name);
    ~TBaseNPC() override;

    void load(JSUMemoryInputStream &in) override;
    void loadAfter() override;
    void perform(u32 flags, JDrama::TGraphics *graphics) override;
    bool receiveMessage(THitActor *sender, u32 message) override;
    void init(TLiveManager *) override;
    void calcRootMatrix() override;
    void bind() override;
    void moveObject() override;
    void kill() override;
    TVec3f getFocalPoint() const override;
    const char **getBasNameTable() const override;

    u32 _00;                 // 0x0150
    u32 _01;                 // 0x0154
    THitActor *mPrevHolder;  // 0x0158
    u32 _02[0xD4 / 4];       // 0x015C
};