#ifndef SUSAMUNE_GHOST_STORAGE_H
#define SUSAMUNE_GHOST_STORAGE_H

#include "susamune/ghost_format.h"
#include "susamune/mem2_map.h"

// PPC/ARM doorbell at the front of the existing ghost transfer slot. Each
// side owns a separate cache line; the request payload stays immutable until
// ackSeq catches requestSeq.
#define SUSAMUNE_GHOST_STORAGE_MAGIC        0x53475354u  // 'SGST'
#define SUSAMUNE_GHOST_STORAGE_VERSION      3u
#define SUSAMUNE_GHOST_STORAGE_HEADER_SIZE \
    SUSAMUNE_GHOST_TRANSFER_MAILBOX_HEADER_SIZE
#define SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE \
    (SUSAMUNE_GHOST_SLOT_SIZE - SUSAMUNE_GHOST_STORAGE_HEADER_SIZE)
#define SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE   0x4000u

#if SUSAMUNE_GHOST_STORAGE_HEADER_SIZE + SUSAMUNE_GHOST_MAX_FILE_SIZE > SUSAMUNE_GHOST_SECONDARY_HEAP_OFFSET
#error "ghost transfer payload overlaps the secondary model heap"
#endif
#if ((SUSAMUNE_GHOST_STORAGE_HEADER_SIZE + SUSAMUNE_GHOST_MAX_FILE_SIZE) & 31u) != 0
#error "ghost transfer payload end must be cache-line aligned"
#endif

// V3 keeps the 48-row wire catalog; only the first 45 personal rows are live.
#define SUSAMUNE_GHOST_SLOT_COUNT SUSAMUNE_GHOST_PROFILE_MAX_ENTRIES
#define SUSAMUNE_GHOST_CATALOG_MAX_ENTRIES SUSAMUNE_GHOST_SLOT_COUNT

// Fixed-slot console files are a 64-byte big-endian commit envelope followed
// by canonical SGHF bytes. The CRC covers the envelope with headerChecksum
// zeroed; payloadChecksum covers the canonical file byte-for-byte.
#define SUSAMUNE_GHOST_ENVELOPE_MAGIC     0x5347454Eu  // 'SGEN'
#define SUSAMUNE_GHOST_ENVELOPE_VERSION   1u
#define SUSAMUNE_GHOST_ENVELOPE_SIZE      64u
#define SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE 0x00000001u

struct SusamuneGhostStorageEnvelope {
    unsigned int magic;
    unsigned short version;
    unsigned short headerSize;
    unsigned int generation;
    unsigned int flags;
    unsigned int gameId;
    unsigned short profile;
    unsigned short slot;
    unsigned int payloadSize;
    unsigned int durationQf;
    unsigned int payloadChecksum;
    unsigned int headerChecksum;
    unsigned int reserved[6];
};

#define SUSAMUNE_GHOST_CMD_NONE   0u
#define SUSAMUNE_GHOST_CMD_SAVE   1u
#define SUSAMUNE_GHOST_CMD_LOAD   2u
#define SUSAMUNE_GHOST_CMD_DELETE 3u
#define SUSAMUNE_GHOST_CMD_LIST   4u
#define SUSAMUNE_GHOST_CMD_EXPORT 5u
#define SUSAMUNE_GHOST_CMD_IMPORT_SCAN 6u
// Source compatibility for the Gate 2 fixed-inbox call sites. V3 scans the
// global import directory and does not consume request.slot.
#define SUSAMUNE_GHOST_CMD_IMPORT SUSAMUNE_GHOST_CMD_IMPORT_SCAN

// Imported files are read directly from one bounded global directory. The ARM
// accepts only validated ASCII LFN leaves ending in .smsghost. Export derives
// its leaf from validated date/route/time/CRC fields.
#define SUSAMUNE_GHOST_SHARE_DIRECTORY "share"
#define SUSAMUNE_GHOST_IMPORT_DIRECTORY "import"
#define SUSAMUNE_GHOST_SHARE_EXTENSION ".smsghost"
#define SUSAMUNE_GHOST_IMPORT_LEAF_SIZE 96u

#define SUSAMUNE_GHOST_RESPONSE_READY 0x0001u
#define SUSAMUNE_GHOST_RESPONSE_BUSY  0x0002u

#define SUSAMUNE_GHOST_STATUS_OK                  0
#define SUSAMUNE_GHOST_STATUS_INVALID_REQUEST    -1
#define SUSAMUNE_GHOST_STATUS_INVALID_SLOT       -2
#define SUSAMUNE_GHOST_STATUS_PAYLOAD_TOO_LARGE  -3
#define SUSAMUNE_GHOST_STATUS_INVALID_FILE       -4
#define SUSAMUNE_GHOST_STATUS_FORWARD_VERSION    -5
#define SUSAMUNE_GHOST_STATUS_QUOTA_EXCEEDED     -6
#define SUSAMUNE_GHOST_STATUS_STORAGE_UNAVAILABLE -7
#define SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE        -8
#define SUSAMUNE_GHOST_STATUS_NOT_FOUND          -9
#define SUSAMUNE_GHOST_STATUS_SLOT_OCCUPIED      -10
#define SUSAMUNE_GHOST_STATUS_IO_BASE             0x10000
#define SUSAMUNE_GHOST_STATUS_IO(result) \
    (SUSAMUNE_GHOST_STATUS_IO_BASE + (int)(result))

#define SUSAMUNE_GHOST_SLOT_PRESENT 0x0001u
#define SUSAMUNE_GHOST_SLOT_UNSAFE  0x0002u
#define SUSAMUNE_GHOST_SLOT_IMPORTED 0x0004u

struct SusamuneGhostStorageRequest {
    unsigned int requestMagic;
    unsigned short protocolVersion;
    unsigned short command;
    unsigned int requestSeq;
    unsigned short profile;
    unsigned short slot;
    unsigned int payloadSize;
    unsigned int flags;
    unsigned int reserved[2];
};

struct SusamuneGhostStorageResponse {
    unsigned int responseMagic;
    unsigned short protocolVersion;
    unsigned short flags;
    unsigned int ackSeq;
    signed int status;
    unsigned int payloadSize;
    unsigned int generation;
    unsigned int totalDurationQf;
    unsigned short slotCount;
    unsigned short profile;
};

struct SusamuneGhostSlotInfo {
    unsigned int generation;
    unsigned int payloadSize;
    unsigned int gameId;
    unsigned int sampleCount;
    unsigned int durationQf;
    unsigned int resultQf;
    unsigned int requiredFeatures;
    unsigned int runFlags;
    signed int routeVariant;
    signed int status;
    unsigned short flags;
    unsigned short sampleIntervalQf;
    unsigned char routeArea;
    unsigned char routeEpisode;
    unsigned char routeParentArea;
    unsigned char routeFlags;
    unsigned char recordingMode;
    unsigned char sampleCodec;
    unsigned char discRevision;
    unsigned char region;
    unsigned char authorLength;
    unsigned char nameLength;
    unsigned char canonicalVersion;
    unsigned char reserved[1];
    char author[SUSAMUNE_GHOST_AUTHOR_SIZE];
    char name[SUSAMUNE_GHOST_NAME_SIZE];
};

struct SusamuneGhostStorageMailbox {
    struct SusamuneGhostStorageRequest request;
    struct SusamuneGhostStorageResponse response;
    unsigned char reserved[SUSAMUNE_GHOST_STORAGE_HEADER_SIZE - 64u];
    unsigned char payload[SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE];
};

#define SUSAMUNE_GHOST_STORAGE_PHYS_PTR \
    ((volatile struct SusamuneGhostStorageMailbox *) \
         SUSAMUNE_CONSOLE_GHOST_TRANSFER_PHYS_BASE)
#define SUSAMUNE_GHOST_STORAGE_PPC_PTR \
    ((volatile struct SusamuneGhostStorageMailbox *) \
         SUSAMUNE_GHOST_TRANSFER_PPC_BASE)

typedef char SusamuneGhostStorageRequestSize[
    sizeof(struct SusamuneGhostStorageRequest) == 32u ? 1 : -1];
typedef char SusamuneGhostStorageResponseSize[
    sizeof(struct SusamuneGhostStorageResponse) == 32u ? 1 : -1];
typedef char SusamuneGhostSlotInfoSize[
    sizeof(struct SusamuneGhostSlotInfo) == 128u ? 1 : -1];
typedef char SusamuneGhostSlotInfoCanonicalVersionOffset[
    __builtin_offsetof(struct SusamuneGhostSlotInfo, canonicalVersion) == 54u
        ? 1 : -1];
typedef char SusamuneGhostStorageMailboxSize[
    sizeof(struct SusamuneGhostStorageMailbox) == SUSAMUNE_GHOST_SLOT_SIZE
        ? 1 : -1];
typedef char SusamuneGhostCatalogFits[
    sizeof(struct SusamuneGhostSlotInfo) * SUSAMUNE_GHOST_SLOT_COUNT <=
            SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE
        ? 1 : -1];
typedef char SusamuneGhostImportedCatalogFits[
    sizeof(struct SusamuneGhostSlotInfo) *
            SUSAMUNE_GHOST_IMPORTED_MAX_ENTRIES <=
            SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE
        ? 1 : -1];
typedef char SusamuneGhostStorageEnvelopeSize[
    sizeof(struct SusamuneGhostStorageEnvelope) ==
            SUSAMUNE_GHOST_ENVELOPE_SIZE
        ? 1 : -1];
typedef char SusamuneGhostStorageEnvelopeChecksumOffset[
    __builtin_offsetof(struct SusamuneGhostStorageEnvelope, headerChecksum) ==
            36u
        ? 1 : -1];

#endif
