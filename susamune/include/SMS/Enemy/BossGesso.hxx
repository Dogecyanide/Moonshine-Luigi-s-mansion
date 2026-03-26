#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRActor.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <SMS/Enemy/SpineEnemy.hxx>
#include <SMS/Manager/EnemyManager.hxx>

class TBGPolDrop : JDrama::TActor {
public:
    enum DropStatus : int { DEAD, ALIVE, HIT };

    TBGPolDrop(const char *);
    virtual ~TBGPolDrop();

    virtual void perform(u32, JDrama::TGraphics *) override;

    void launch(const TVec3f &pos, const TVec3f &vel);
    void move();

    TVec3f mVelocity;     // 0x0044
    MActor *mActor;       // 0x0050
    MActor *mActorWhite;  // 0x0054
    DropStatus mStatus;   // 0x0058
};

class TBossGesso : public TSpineEnemy {

public:
    enum Attacks { SINGLE, DOUBLE, SKIPROPE, UNISON, SHOOT };

    TBossGesso(const char *);
    ~TBossGesso();

    virtual void init(TLiveManager *) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual bool receiveMessage(THitActor *, u32) override;
    virtual void moveObject() override;
    virtual void calcRootMatrix() override;
    virtual void reset() override;
    virtual const char **getBasNameTable() const override;

    bool beakHeld() const;
    void changeAllTentacleState(int);
    void changeAttackMode(int);
    void doAttackDouble();
    void doAttackShoot();
    void doAttackSingle();
    void doAttackSkipRope();
    void doAttackUnison();
    void gotTentacleDamage();
    bool is2ndFightNow() const;
    void launchPolDrop();
    void lenFromToeToMario();
    void rumblePad(int, const JGeometry::TVec3<float> &);
    void showMessage(u32);
    void stopIfRoll();
    bool tentacleHeld() const;

    u32 *tentacle1;                    // 0x0150 (TBGTentacle **)
    u32 *tentacle2;              // 0x0154 (TBGTentacle **)
    u32 *tentacle3;                    // 0x0158 (TBGTentacle **)
    u32 *tentacle4;              // 0x015C (TBGTentacle **)
    u32 *beak;                    // 0x0160 (TBGBeakHit **)
    u32 _164;                    // 0x0164 ? (TBossGessoMtxCalc uses this)
    u32 mAttackState;         // 0x0168
    u32 mAttackTimer;            // 0x016C
    u32 _170;                    // 0x0170 ? (THitActor?)
    u32 _174;                    // 0x0174 ? (THitActor?)
    u32 *dirtyWhite;         // 0x0178 ? (THitActor?)
    u32 _17C;                    // 0x017C ? (MActor?)
    TBGPolDrop *mPollutionDrop;  // 0x0180
    u32 _184;                    // 0x0184
    f32 _188;           // 0x0188
    u32 _18C;                    // 0x018C
    J3DGXColor mGoopColor; // 0x0190
    u8 mAttackCount;   // 0x0194
    u8 _195;               // 0x0195
    u8 _196;           // 0x0196
    u8 _197;                // 0x0197 (Padding?)
    u32 _198;          // 0x0198
    u32 _19C;              // 0x019C
    u32 _1A0;          // 0x01A0
    u32 _1A4;              // 0x01A4
    u32 _1A8;          // 0x01A8
    u32 _1AC;              // 0x01AC
};

class TBossGessoManager : public TEnemyManager {
public:
    TBossGessoManager(const char *);
    virtual ~TBossGessoManager();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void createModelData() override;

    void initJParticle();
};