#include "susamune/ghost_model.hxx"

#include "Dolphin/GX_types.h"
#include "Dolphin/MTX.h"
#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "JSystem/J3D/J3DModel.hxx"
#include "JSystem/J3D/J3DModelLoaderDataBase.hxx"
#include "JSystem/JDrama/JDRViewObjPtrListT.hxx"
#include "JSystem/JKernel/JKRFileLoader.hxx"
#include "JSystem/JKernel/JKRHeap.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/MarioDraw.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/ghost.hxx"
#include "susamune/ghost_model_asset.h"
#include "susamune/ghost_storage.h"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

class TScreenTexture {
public:
    bool replace(J3DModelData *, const char *);
};

extern TScreenTexture *gpScreenTexture;

void SMS_InitPacket_MatColor(J3DModel *, u16, GXChannelID,
                             const GXColor *);
J3DMtxCalc *J3DNewMtxCalcAnm(u32, J3DAnmTransform *);

namespace GhostModel {
namespace {

enum Appearance {
    APPEARANCE_SHADOW = 0,
    APPEARANCE_PIANTA = 1,
    APPEARANCE_COUNT = 2,
};

struct AssetHeader {
    u32 magic;
    u16 version;
    u16 headerSize;
    s32 status;
    u32 totalSize;
    u32 bmdOffset;
    u32 bmdSize;
    u32 payloadChecksum;
    u32 reserved;
};

struct ModelSlot {
    JKRExpHeap *heap;
    J3DModelData *data;
    J3DModel *model;
    J3DMtxCalc *mtxCalc;
};

const u32 kCueCalcView = 0x00000004u;
const u32 kCueEntry = 0x00000200u;
const u32 kRegistrationMinFree = 64u;
const u32 kShadowLoadFlags = 0x10210000u;
const u32 kPiantaLoadFlags = 0x10040000u;
const u32 kShadowWorstCaseUsed = 0x118CDu;
const u32 kPiantaWorstCaseUsed = 0x11B4Du;
const u32 kModelAllocationPreflight = 0x12000u;
const u32 kFixedExpHeapOverhead = 0x130u;
const u32 kExpectedJointCount = 29u;
const f32 kAngleToRadians = 0.00009587379924285257f;
const u16 kMarioBckIdMax = 200u;
const char kShadowModelPath[] = "/scene/kagemario/default.bmd";
const char kPiantaModelPath[] = "/scene/map/map/pad/monteman_model.bmd";
const char kShadowTextureName[] = "H_kagemario_dummy";

static_assert(sizeof(AssetHeader) == SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE,
              "ghost model asset header ABI changed");
static_assert(SUSAMUNE_GHOST_MODEL_HEAP_OFFSET ==
                  SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE,
              "primary model heap must follow the sample payload");
static_assert(SUSAMUNE_GHOST_MODEL_HEAP_OFFSET +
                      SUSAMUNE_GHOST_MODEL_HEAP_SIZE <=
                  SUSAMUNE_GHOST_SEGMENT_TABLE_OFFSET,
              "primary model heap overlaps playback data");
static_assert(SUSAMUNE_GHOST_STORAGE_HEADER_SIZE +
                      SUSAMUNE_GHOST_MAX_FILE_SIZE <=
                  SUSAMUNE_GHOST_SECONDARY_HEAP_OFFSET,
              "secondary model heap overlaps transfer payload");
static_assert(SUSAMUNE_GHOST_SECONDARY_HEAP_OFFSET +
                      SUSAMUNE_GHOST_SECONDARY_HEAP_SIZE ==
                  SUSAMUNE_GHOST_SLOT_SIZE,
              "secondary model heap must consume the transfer tail");
static_assert(((SUSAMUNE_GHOST_STORAGE_HEADER_SIZE +
                SUSAMUNE_GHOST_MAX_FILE_SIZE) &
               31u) == 0,
              "transfer payload end must be cache-line aligned");
static_assert(kShadowWorstCaseUsed <= kModelAllocationPreflight,
              "Shadow allocation proof exceeds the preflight bound");
static_assert(kPiantaWorstCaseUsed <= kModelAllocationPreflight,
              "Piantissimo allocation proof exceeds the preflight bound");
static_assert(SUSAMUNE_GHOST_MODEL_HEAP_SIZE >=
                  kModelAllocationPreflight + kFixedExpHeapOverhead,
              "primary model heap cannot satisfy its preflight");
static_assert(SUSAMUNE_GHOST_SECONDARY_HEAP_SIZE >=
                  kModelAllocationPreflight + kFixedExpHeapOverhead,
              "secondary model heap cannot satisfy its preflight");

ModelSlot sSlots[APPEARANCE_COUNT];
bool sRegistered;
bool sPrepared[2];
bool sSubmitted[2];
GXColor sGhostColor = {255, 255, 255, 160};

u32 readBig32(const u8 *bytes) {
    return (static_cast<u32>(bytes[0]) << 24) |
           (static_cast<u32>(bytes[1]) << 16) |
           (static_cast<u32>(bytes[2]) << 8) |
           static_cast<u32>(bytes[3]);
}

u32 crc32(const void *data, u32 size) {
    const u8 *bytes = static_cast<const u8 *>(data);
    u32 crc = SUSAMUNE_GHOST_CRC32_INIT;
    for (u32 i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (u32 bit = 0; bit < 8; ++bit) {
            const u32 mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (SUSAMUNE_GHOST_CRC32_POLY & mask);
        }
    }
    return crc ^ SUSAMUNE_GHOST_CRC32_XOR_OUT;
}

bool validBmdHeader(const void *resource, u32 size) {
    const u8 *bytes = static_cast<const u8 *>(resource);
    return bytes && memcmp(bytes, "J3D2bmd3", 8) == 0 &&
           readBig32(bytes + 8) == size;
}

bool validBmd(const void *resource, u32 size, u32 checksum) {
    return validBmdHeader(resource, size) &&
           crc32(resource, size) == checksum;
}

#if !defined(IS_EMULATOR) || !IS_EMULATOR
const u8 *validateMaster(void *raw, u32 bufferSize, u32 magic,
                         u32 totalSize, u32 bmdOffset, u32 bmdSize,
                         u32 payloadChecksum) {
    DCInvalidateRange(raw, bufferSize);
    const AssetHeader *header = static_cast<const AssetHeader *>(raw);
    if (header->magic != magic ||
        header->version != SUSAMUNE_GHOST_MODEL_ASSET_VERSION ||
        header->headerSize != SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE ||
        header->status != SUSAMUNE_GHOST_MODEL_STATUS_READY ||
        header->totalSize != totalSize || header->bmdOffset != bmdOffset ||
        header->bmdSize != bmdSize ||
        header->payloadChecksum != payloadChecksum || header->reserved != 0 ||
        totalSize < bmdOffset || totalSize > bufferSize) {
        return nullptr;
    }
    const u8 *payload = static_cast<const u8 *>(raw) + bmdOffset;
    return crc32(payload, totalSize - bmdOffset) == payloadChecksum
        ? payload
        : nullptr;
}

const u8 *shadowMaster() {
    void *asset = const_cast<SusamuneGhostShadowAsset *>(
        SUSAMUNE_GHOST_SHADOW_MASTER_PPC_PTR);
    return validateMaster(asset, SUSAMUNE_GHOST_SHADOW_ASSET_BUFFER_SIZE,
                          SUSAMUNE_GHOST_SHADOW_ASSET_MAGIC,
                          SUSAMUNE_GHOST_SHADOW_ASSET_SIZE,
                          SUSAMUNE_GHOST_SHADOW_BMD_OFFSET,
                          SUSAMUNE_GHOST_SHADOW_BMD_SIZE,
                          SUSAMUNE_GHOST_SHADOW_PAYLOAD_CRC32);
}

const u8 *piantaMaster() {
    void *asset = const_cast<SusamuneGhostPiantaAsset *>(
        SUSAMUNE_GHOST_PIANTA_MASTER_PPC_PTR);
    return validateMaster(asset, SUSAMUNE_GHOST_PIANTA_ASSET_BUFFER_SIZE,
                          SUSAMUNE_GHOST_PIANTA_ASSET_MAGIC,
                          SUSAMUNE_GHOST_PIANTA_ASSET_SIZE,
                          SUSAMUNE_GHOST_PIANTA_BMD_OFFSET,
                          SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
                          SUSAMUNE_GHOST_PIANTA_PAYLOAD_CRC32);
}
#endif

const void *stageLocalBmd(const char *path, u32 size, u32 checksum,
                          bool allowTextureMutation) {
    const void *resource = JKRFileLoader::getGlbResource(path);
    if (!validBmdHeader(resource, size)) return nullptr;
    return allowTextureMutation || crc32(resource, size) == checksum
        ? resource
        : nullptr;
}

const void *shadowResource() {
#if !defined(IS_EMULATOR) || !IS_EMULATOR
    const u8 *master = shadowMaster();
    if (master) {
        SusamuneGhostShadowAsset *workingAsset =
            const_cast<SusamuneGhostShadowAsset *>(
                SUSAMUNE_GHOST_SHADOW_STAGING_PPC_PTR);
        u8 *working = reinterpret_cast<u8 *>(workingAsset) +
                      SUSAMUNE_GHOST_SHADOW_BMD_OFFSET;
        memcpy(working, master, SUSAMUNE_GHOST_SHADOW_BMD_SIZE);
        DCFlushRange(working, SUSAMUNE_GHOST_SHADOW_BMD_SIZE);
        if (!validBmd(working, SUSAMUNE_GHOST_SHADOW_BMD_SIZE,
                      SUSAMUNE_GHOST_SHADOW_BMD_CRC32)) {
            return nullptr;
        }
        return working;
    }
#endif
    const void *local = stageLocalBmd(
        kShadowModelPath, SUSAMUNE_GHOST_SHADOW_BMD_SIZE,
        SUSAMUNE_GHOST_SHADOW_BMD_CRC32, true);
    return local;
}

const void *piantaResource() {
#if !defined(IS_EMULATOR) || !IS_EMULATOR
    const u8 *master = piantaMaster();
    if (master && validBmd(master, SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
                           SUSAMUNE_GHOST_PIANTA_BMD_CRC32)) {
        return master;
    }
#endif
    const void *local = stageLocalBmd(
        kPiantaModelPath, SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
        SUSAMUNE_GHOST_PIANTA_BMD_CRC32, false);
    return local;
}

u8 ghostAlpha() {
    static const u8 kOpacity[] = {64, 128, 192, 255};
    u8 choice = gSettings.get(SETTING_GHOST_OPACITY);
    if (choice >= sizeof(kOpacity)) choice = 1;
    return kOpacity[choice];
}

J3DAnmTransform *marioAnimation(u16 logicalId) {
    if (!gpMarioOriginal || !gpMarioOriginal->mModelData ||
        !gpMarioOriginal->mModelData->_04 ||
        logicalId > SUSAMUNE_GHOST_ANIMATION_ID_MAX) {
        return nullptr;
    }
    const u16 bckId = gMarioAnimeData[logicalId].mAnimID;
    if (bckId > kMarioBckIdMax) return nullptr;
    u8 *common = static_cast<u8 *>(gpMarioOriginal->mModelData->_04);
    J3DAnmTransform **animations =
        *reinterpret_cast<J3DAnmTransform ***>(common + 4);
    return animations ? animations[bckId] : nullptr;
}

J3DAnmTransform **animationSlot(ModelSlot &slot) {
    return slot.mtxCalc
        ? reinterpret_cast<J3DAnmTransform **>(
              reinterpret_cast<u8 *>(slot.mtxCalc) - 0x10)
        : nullptr;
}

s16 animationFrameMax(const J3DAnmTransform *animation) {
    return animation
        ? *reinterpret_cast<const s16 *>(
              reinterpret_cast<const u8 *>(animation) + 2)
        : 0;
}

f32 *animationFrame(J3DAnmTransform *animation) {
    return animation
        ? reinterpret_cast<f32 *>(reinterpret_cast<u8 *>(animation) + 4)
        : nullptr;
}

J3DMtxCalc **rootMtxCalcSlot(ModelSlot &slot) {
    return slot.data && slot.data->mRootNode
        ? reinterpret_cast<J3DMtxCalc **>(
              reinterpret_cast<u8 *>(slot.data->mRootNode) + 0x58)
        : nullptr;
}

bool configureTranslucency(ModelSlot &slot) {
    if (!slot.data || !slot.model) return false;
    const u16 materialCount = slot.data->getMaterialNum();
    for (u16 i = 0; i < materialCount; ++i) {
        u8 *material = reinterpret_cast<u8 *>(slot.data->mMaterials[i]);
        if (!material) return false;
        u32 *mode = reinterpret_cast<u32 *>(material + 0x08);
        u8 *pe = *reinterpret_cast<u8 **>(material + 0x30);
        if (!pe) return false;

        *mode = (*mode & ~3u) | 4u;
        *reinterpret_cast<u16 *>(pe + 0x08) = 0x00E7u;
        pe[0x0a] = 0;
        pe[0x0b] = 0;
        pe[0x0c] = GX_BM_BLEND;
        pe[0x0d] = GX_BL_SRCALPHA;
        pe[0x0e] = GX_BL_INVSRCALPHA;
        pe[0x0f] = GX_LO_COPY;
        *reinterpret_cast<u16 *>(pe + 0x10) = 0x0016u;
    }
    slot.model->makeDL();
    for (u16 i = 0; i < materialCount; ++i) {
        SMS_InitPacket_MatColor(slot.model, i, GX_COLOR0A0, &sGhostColor);
    }
    return true;
}

void restoreHeap(JKRHeap *heap) {
    if (heap) heap->becomeCurrentHeap();
    else JKRHeap::sCurrentHeap = nullptr;
}

void clearLiveModel(ModelSlot &slot) {
    slot.data = nullptr;
    slot.model = nullptr;
    slot.mtxCalc = nullptr;
}

bool loadModel(Appearance appearance) {
    ModelSlot &slot = sSlots[appearance];
    clearLiveModel(slot);
    if (!slot.heap) return false;
    slot.heap->freeAll();
    const u32 emptyFree = slot.heap->getTotalFreeSize();
    if (emptyFree < kModelAllocationPreflight) return false;

    const void *resource = appearance == APPEARANCE_SHADOW
        ? shadowResource()
        : piantaResource();
    if (!resource ||
        (appearance == APPEARANCE_SHADOW && !gpScreenTexture)) return false;

    JKRHeap *oldHeap = JKRHeap::sCurrentHeap;
    slot.heap->becomeCurrentHeap();
    const u32 loadFlags = appearance == APPEARANCE_SHADOW
        ? kShadowLoadFlags
        : kPiantaLoadFlags;
    slot.data = J3DModelLoaderDataBase::load(resource, loadFlags);
    if (slot.data && slot.data->getJointNum() == kExpectedJointCount) {
        void *storage = slot.heap->alloc(sizeof(J3DModel), 32);
        if (storage) slot.model = new (storage) J3DModel(slot.data, 0, 1);
    }
    if (slot.model) {
        const u16 initialId = gpMarioOriginal &&
                                      gpMarioOriginal->mAnimationID <=
                                          SUSAMUNE_GHOST_ANIMATION_ID_MAX
            ? gpMarioOriginal->mAnimationID
            : 0;
        J3DAnmTransform *initial = marioAnimation(initialId);
        if (!initial && initialId != 0) initial = marioAnimation(0);
        if (initial) {
            slot.mtxCalc = J3DNewMtxCalcAnm(slot.data->_C & 0xfu, initial);
        }
        if (!slot.mtxCalc) slot.model = nullptr;
    }
    if (slot.model && appearance == APPEARANCE_SHADOW &&
        !gpScreenTexture->replace(slot.data, kShadowTextureName)) {
        slot.model = nullptr;
    }
    if (slot.model && !configureTranslucency(slot)) slot.model = nullptr;
    if (slot.model && appearance == APPEARANCE_PIANTA &&
        !validBmd(resource, SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
                  SUSAMUNE_GHOST_PIANTA_BMD_CRC32)) {
        slot.model = nullptr;
    }

    const u32 free = slot.heap->getTotalFreeSize();
    const u32 used = emptyFree >= free ? emptyFree - free : 0;
    restoreHeap(oldHeap);

    if (!slot.model || used > kModelAllocationPreflight) {
        clearLiveModel(slot);
        return false;
    }
    return true;
}

Appearance selectedAppearance() {
    return gSettings.get(SETTING_GHOST_APPEARANCE) == 1
        ? APPEARANCE_PIANTA : APPEARANCE_SHADOW;
}

ModelSlot &runnerSlot(int runner) {
    const int selected = static_cast<int>(selectedAppearance());
    return sSlots[selected ^ (runner != 0 ? 1 : 0)];
}

bool prepareRunner(int runner) {
    Ghost::VisualState state;
    ModelSlot &slot = runnerSlot(runner);
    const bool haveState = runner == 0
        ? Ghost::visualState(&state)
        : Ghost::secondaryVisualState(&state);
    if (!haveState || !slot.model || !slot.mtxCalc || !state.visible ||
        !gSettings.getBool(SETTING_GHOST_DISPLAY) ||
        (gMenu && gMenu->shown())) return false;

    J3DAnmTransform *animation = marioAnimation(state.animationId);
    J3DAnmTransform **animationPtr = animationSlot(slot);
    J3DMtxCalc **rootCalc = rootMtxCalcSlot(slot);
    const s16 frameMax = animationFrameMax(animation);
    f32 *frame = animationFrame(animation);
    if (!animation || !animationPtr || !rootCalc || frameMax <= 0 || !frame)
        return false;

    Mtx yaw;
    MTXRotRad(yaw, 'y', static_cast<f32>(state.yaw) * kAngleToRadians);
    yaw[0][3] = state.x;
    yaw[1][3] = state.y;
    yaw[2][3] = state.z;
    MTXCopy(yaw, *slot.model->getBaseTRMtx());

    const f32 savedFrame = *frame;
    J3DMtxCalc *savedCalc = *rootCalc;
    *animationPtr = animation;
    *frame = static_cast<f32>(state.animationPhase) * frameMax /
             static_cast<f32>(SUSAMUNE_GHOST_ANIMATION_PHASE_MAX + 1u);
    *rootCalc = slot.mtxCalc;
    slot.model->J3DModel::calc();
    *rootCalc = savedCalc;
    *frame = savedFrame;
    return true;
}

class GhostView : public JDrama::TViewObj {
public:
    GhostView() : JDrama::TViewObj("Susamune Ghost Models") {}

    virtual void perform(u32 cue, JDrama::TGraphics *) override {
        if ((cue & (kCueCalcView | kCueEntry)) == 0) return;
        if (cue & kCueCalcView) {
            Ghost::prepareVisual();
            sGhostColor.a = ghostAlpha();
            for (int runner = 0; runner < 2; ++runner) {
                sPrepared[runner] = prepareRunner(runner);
                if (sPrepared[runner])
                    runnerSlot(runner).model->J3DModel::viewCalc();
            }
        }
        if (cue & kCueEntry) {
            for (int runner = 0; runner < 2; ++runner) {
                if (!sPrepared[runner]) continue;
                runnerSlot(runner).model->J3DModel::entry();
                sSubmitted[runner] = true;
            }
        }
    }
};

alignas(32) u8 sViewStorage[sizeof(GhostView)];
GhostView *sView;

bool registerView(TMarDirector *director) {
    static const char kPlayerGroup[] =
        "\x83\x76\x83\x8C\x81\x5B\x83\x84\x81\x5B"
        "\x83\x4F\x83\x8B\x81\x5B\x83\x76";
    JDrama::TNameRef *ref = director->mViewObjRoot->search(kPlayerGroup);
    if (!ref) return false;
    JDrama::TViewObjPtrListT<JDrama::TViewObj> *group =
        reinterpret_cast<JDrama::TViewObjPtrListT<JDrama::TViewObj> *>(ref);
    for (JGadget::TList_pointer_void::iterator it = group->mViewObjList.begin();
         it != group->mViewObjList.end(); ++it) {
        if (*it == sView) return true;
    }
    JKRHeap *nodeHeap = JKRHeap::sCurrentHeap;
    if (!nodeHeap || nodeHeap->getFreeSize() < kRegistrationMinFree)
        return false;
    const u32 oldSize = group->mViewObjList.size();
    group->mViewObjList.push_back(sView);
    return group->mViewObjList.size() == oldSize + 1;
}

}  // namespace

void init() {
    memset(sSlots, 0, sizeof(sSlots));
    sRegistered = false;
    sPrepared[0] = sPrepared[1] = false;
    sSubmitted[0] = sSubmitted[1] = false;
    sView = new (sViewStorage) GhostView();
    JKRHeap *oldHeap = JKRHeap::sCurrentHeap;
    sSlots[APPEARANCE_SHADOW].heap = JKRExpHeap::create(
        reinterpret_cast<void *>(SUSAMUNE_GHOST_MODEL_HEAP_PPC_BASE),
        SUSAMUNE_GHOST_MODEL_HEAP_SIZE, JKRHeap::sRootHeap, false);
    sSlots[APPEARANCE_PIANTA].heap = JKRExpHeap::create(
        reinterpret_cast<void *>(SUSAMUNE_GHOST_SECONDARY_HEAP_PPC_BASE),
        SUSAMUNE_GHOST_SECONDARY_HEAP_SIZE, JKRHeap::sRootHeap, false);
    restoreHeap(oldHeap);
}

void beginFrame() {
    sSubmitted[0] = sSubmitted[1] = false;
    sPrepared[0] = sPrepared[1] = false;
}

void beforeStageSetup() {
    // Retail never clears this stage-local singleton.
    gpScreenTexture = nullptr;
}

void onStageSetup(TMarDirector *director) {
    sRegistered = false;
    sSubmitted[0] = sSubmitted[1] = false;
    sPrepared[0] = sPrepared[1] = false;
    clearLiveModel(sSlots[APPEARANCE_SHADOW]);
    clearLiveModel(sSlots[APPEARANCE_PIANTA]);
    if (!director || !director->mViewObjRoot) return;
    const bool shadow = loadModel(APPEARANCE_SHADOW);
    const bool pianta = loadModel(APPEARANCE_PIANTA);
    if ((!shadow && !pianta) || !registerView(director)) {
        clearLiveModel(sSlots[APPEARANCE_SHADOW]);
        clearLiveModel(sSlots[APPEARANCE_PIANTA]);
        return;
    }
    sRegistered = true;
}

bool available() {
    return sRegistered && (sSlots[APPEARANCE_SHADOW].model ||
                           sSlots[APPEARANCE_PIANTA].model);
}

bool submitted(bool secondary) {
    return sSubmitted[secondary ? 1 : 0];
}

}  // namespace GhostModel
