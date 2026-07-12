#pragma once

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/Map/JointModel.hxx>

class TJointModelManager : public JDrama::TViewObj {
public:
    TJointModelManager(const char *);
    virtual ~TJointModelManager();

    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual TJointModel *newJointModel() const;

    void initJointModel();

    size_t mJointModelNum;
    TJointModel **mJointModels;
    u32 _18;
    const char *mJointModelsPath;
    MActorAnmData *mAnmData;
};