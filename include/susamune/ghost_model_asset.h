#ifndef SUSAMUNE_GHOST_MODEL_ASSET_H
#define SUSAMUNE_GHOST_MODEL_ASSET_H

#include "susamune/ghost_format.h"
#include "susamune/mem2_map.h"

// The loader extracts these bytes from the user's own retail disc. The ARM
// kernel publishes pristine masters after consuming the mod staging file.
#define SUSAMUNE_GHOST_MODEL_ASSET_VERSION     1u
#define SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE 0x20u

#define SUSAMUNE_GHOST_SHADOW_ASSET_MAGIC       0x5347534Du  // 'SGSM'
#define SUSAMUNE_GHOST_SHADOW_ASSET_BUFFER_SIZE 0x10000u
#define SUSAMUNE_GHOST_SHADOW_BMD_OFFSET \
    SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE
#define SUSAMUNE_GHOST_SHADOW_BMD_SIZE 0xF8C0u
#define SUSAMUNE_GHOST_SHADOW_BTK_OFFSET \
    (SUSAMUNE_GHOST_SHADOW_BMD_OFFSET + SUSAMUNE_GHOST_SHADOW_BMD_SIZE)
#define SUSAMUNE_GHOST_SHADOW_BTK_SIZE 0x440u
#define SUSAMUNE_GHOST_SHADOW_ASSET_SIZE \
    (SUSAMUNE_GHOST_SHADOW_BTK_OFFSET + SUSAMUNE_GHOST_SHADOW_BTK_SIZE)
#define SUSAMUNE_GHOST_SHADOW_BMD_CRC32     0xEE0F8339u
#define SUSAMUNE_GHOST_SHADOW_BTK_CRC32     0x02AE7876u
#define SUSAMUNE_GHOST_SHADOW_PAYLOAD_CRC32 0xFC04D868u

#define SUSAMUNE_GHOST_PIANTA_ASSET_MAGIC       0x5347504Du  // 'SGPM'
#define SUSAMUNE_GHOST_PIANTA_ASSET_BUFFER_SIZE 0x12000u
#define SUSAMUNE_GHOST_PIANTA_BMD_OFFSET \
    SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE
#define SUSAMUNE_GHOST_PIANTA_BMD_SIZE       0x119A0u
#define SUSAMUNE_GHOST_PIANTA_ASSET_SIZE \
    (SUSAMUNE_GHOST_PIANTA_BMD_OFFSET + SUSAMUNE_GHOST_PIANTA_BMD_SIZE)
#define SUSAMUNE_GHOST_PIANTA_BMD_CRC32      0x448001A9u
#define SUSAMUNE_GHOST_PIANTA_PAYLOAD_CRC32  SUSAMUNE_GHOST_PIANTA_BMD_CRC32

#define SUSAMUNE_GHOST_MODEL_STATUS_READY              1
#define SUSAMUNE_GHOST_MODEL_STATUS_SOURCE_UNSUPPORTED -1
#define SUSAMUNE_GHOST_MODEL_STATUS_OPEN_FAILED        -2
#define SUSAMUNE_GHOST_MODEL_STATUS_READ_FAILED        -3
#define SUSAMUNE_GHOST_MODEL_STATUS_BAD_YAZ0           -4
#define SUSAMUNE_GHOST_MODEL_STATUS_BAD_RARC           -5
#define SUSAMUNE_GHOST_MODEL_STATUS_RESOURCE_MISSING   -6
#define SUSAMUNE_GHOST_MODEL_STATUS_BAD_CHECKSUM       -7

// Compatibility names used by the existing streaming extractor.
#define SUSAMUNE_GHOST_SHADOW_ASSET_VERSION \
    SUSAMUNE_GHOST_MODEL_ASSET_VERSION
#define SUSAMUNE_GHOST_SHADOW_ASSET_HEADER_SIZE \
    SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE
#define SUSAMUNE_GHOST_SHADOW_STATUS_READY \
    SUSAMUNE_GHOST_MODEL_STATUS_READY
#define SUSAMUNE_GHOST_SHADOW_STATUS_SOURCE_UNSUPPORTED \
    SUSAMUNE_GHOST_MODEL_STATUS_SOURCE_UNSUPPORTED
#define SUSAMUNE_GHOST_SHADOW_STATUS_OPEN_FAILED \
    SUSAMUNE_GHOST_MODEL_STATUS_OPEN_FAILED
#define SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED \
    SUSAMUNE_GHOST_MODEL_STATUS_READ_FAILED
#define SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0 \
    SUSAMUNE_GHOST_MODEL_STATUS_BAD_YAZ0
#define SUSAMUNE_GHOST_SHADOW_STATUS_BAD_RARC \
    SUSAMUNE_GHOST_MODEL_STATUS_BAD_RARC
#define SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING \
    SUSAMUNE_GHOST_MODEL_STATUS_RESOURCE_MISSING
#define SUSAMUNE_GHOST_SHADOW_STATUS_BAD_CHECKSUM \
    SUSAMUNE_GHOST_MODEL_STATUS_BAD_CHECKSUM

struct SusamuneGhostShadowAsset {
    unsigned int magic;
    unsigned short version;
    unsigned short headerSize;
    signed int status;
    unsigned int totalSize;
    unsigned int bmdOffset;
    unsigned int bmdSize;
    unsigned int payloadChecksum;
    unsigned int reserved;
    unsigned char payload[SUSAMUNE_GHOST_SHADOW_ASSET_BUFFER_SIZE -
                          SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE];
};

struct SusamuneGhostPiantaAsset {
    unsigned int magic;
    unsigned short version;
    unsigned short headerSize;
    signed int status;
    unsigned int totalSize;
    unsigned int bmdOffset;
    unsigned int bmdSize;
    unsigned int payloadChecksum;
    unsigned int reserved;
    unsigned char payload[SUSAMUNE_GHOST_PIANTA_ASSET_BUFFER_SIZE -
                          SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE];
};

#define SUSAMUNE_GHOST_SHADOW_STAGING_PPC_PTR \
    ((volatile struct SusamuneGhostShadowAsset *)( \
        SUSAMUNE_GHOST_RECORD_PPC_BASE + \
        SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE))
#define SUSAMUNE_GHOST_SHADOW_STAGING_PHYS_PTR \
    ((volatile struct SusamuneGhostShadowAsset *)( \
        SUSAMUNE_CONSOLE_GHOST_RECORD_PHYS_BASE + \
        SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE))
#define SUSAMUNE_GHOST_PIANTA_STAGING_PPC_PTR \
    ((volatile struct SusamuneGhostPiantaAsset *)( \
        SUSAMUNE_GHOST_PLAY_PPC_BASE + SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE))
#define SUSAMUNE_GHOST_PIANTA_STAGING_PHYS_PTR \
    ((volatile struct SusamuneGhostPiantaAsset *)( \
        SUSAMUNE_CONSOLE_GHOST_PLAY_PHYS_BASE + \
        SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE))

#define SUSAMUNE_GHOST_SHADOW_MASTER_PPC_PTR \
    ((volatile struct SusamuneGhostShadowAsset *)( \
        SUSAMUNE_GHOST_ASSET_VAULT_PPC_BASE + \
        SUSAMUNE_GHOST_SHADOW_MASTER_OFFSET))
#define SUSAMUNE_GHOST_SHADOW_MASTER_PHYS_PTR \
    ((volatile struct SusamuneGhostShadowAsset *)( \
        SUSAMUNE_GHOST_ASSET_VAULT_PHYS_BASE + \
        SUSAMUNE_GHOST_SHADOW_MASTER_OFFSET))
#define SUSAMUNE_GHOST_PIANTA_MASTER_PPC_PTR \
    ((volatile struct SusamuneGhostPiantaAsset *)( \
        SUSAMUNE_GHOST_ASSET_VAULT_PPC_BASE + \
        SUSAMUNE_GHOST_PIANTA_MASTER_OFFSET))
#define SUSAMUNE_GHOST_PIANTA_MASTER_PHYS_PTR \
    ((volatile struct SusamuneGhostPiantaAsset *)( \
        SUSAMUNE_GHOST_ASSET_VAULT_PHYS_BASE + \
        SUSAMUNE_GHOST_PIANTA_MASTER_OFFSET))

// The existing extractor writes Shadow into its pre-handoff staging tail.
#define SUSAMUNE_GHOST_SHADOW_ASSET_PPC_PTR \
    SUSAMUNE_GHOST_SHADOW_STAGING_PPC_PTR

typedef char SusamuneGhostModelAssetHeaderSize[
    __builtin_offsetof(struct SusamuneGhostShadowAsset, payload) ==
                SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE &&
            __builtin_offsetof(struct SusamuneGhostPiantaAsset, payload) ==
                SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE
        ? 1 : -1];
typedef char SusamuneGhostModelAssetHeaderOffsets[
    __builtin_offsetof(struct SusamuneGhostShadowAsset, magic) == 0u &&
            __builtin_offsetof(struct SusamuneGhostShadowAsset, version) == 4u &&
            __builtin_offsetof(struct SusamuneGhostShadowAsset, headerSize) == 6u &&
            __builtin_offsetof(struct SusamuneGhostShadowAsset, status) == 8u &&
            __builtin_offsetof(struct SusamuneGhostShadowAsset, totalSize) == 12u &&
            __builtin_offsetof(struct SusamuneGhostShadowAsset, bmdOffset) == 16u &&
            __builtin_offsetof(struct SusamuneGhostShadowAsset, bmdSize) == 20u &&
            __builtin_offsetof(struct SusamuneGhostShadowAsset, payloadChecksum) == 24u &&
            __builtin_offsetof(struct SusamuneGhostShadowAsset, reserved) == 28u &&
            __builtin_offsetof(struct SusamuneGhostPiantaAsset, payload) == 32u
        ? 1 : -1];
typedef char SusamuneGhostModelAssetBufferSizes[
    sizeof(struct SusamuneGhostShadowAsset) ==
                SUSAMUNE_GHOST_SHADOW_ASSET_BUFFER_SIZE &&
            sizeof(struct SusamuneGhostPiantaAsset) ==
                SUSAMUNE_GHOST_PIANTA_ASSET_BUFFER_SIZE
        ? 1 : -1];
typedef char SusamuneGhostModelAssetsFitStageTails[
    SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE +
                    SUSAMUNE_GHOST_SHADOW_ASSET_BUFFER_SIZE <=
                SUSAMUNE_GHOST_SEGMENT_TABLE_OFFSET &&
            SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE +
                    SUSAMUNE_GHOST_PIANTA_ASSET_BUFFER_SIZE <=
                SUSAMUNE_GHOST_SEGMENT_TABLE_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostModelAssetsFitVault[
    sizeof(struct SusamuneGhostShadowAsset) ==
                SUSAMUNE_GHOST_SHADOW_MASTER_SIZE &&
            sizeof(struct SusamuneGhostPiantaAsset) ==
                SUSAMUNE_GHOST_PIANTA_MASTER_SIZE
        ? 1 : -1];

#endif
