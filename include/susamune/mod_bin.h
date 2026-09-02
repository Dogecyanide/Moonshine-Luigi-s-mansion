#ifndef SUSAMUNE_MOD_BIN_H
#define SUSAMUNE_MOD_BIN_H

#include "susamune/mem2_map.h"

// =====================================================================
// mod_bin.h
//
// The on-disc format of mod_<game-or-region>.bin and the MEM2 window it is staged
// in. The mod used to be a byte array compiled into the Nintendont kernel
// (one launcher per game version, and a second copy of the blob resident in
// MEM2 for the whole session). It is now a file next to the launcher's
// boot.dol that the loader reads for the detected disc and the kernel copies
// into MEM1, so one launcher can serve GMSJ/GMSE/GMSP and the separately
// authenticated GLMJ01 diagnostic.
//
// The hook writes travel with the code: their addresses are per-version, so
// a blob without its write list is not applicable to anything.
//
// Shared by all three toolchains, so this header is plain C with no type
// dependencies. Everything is big-endian on both sides; no swapping.
//
// Flow:
//   loader (PPC) -- knows the game id before booting the kernel; reads
//                   <launch_dir>/mod_<tag>.bin into the MEM2 window below
//                   and flushes it. Writes a zeroed header if there is none.
//   kernel (ARM) -- PatchSusamune() validates the header against the running
//                   GAME_ID, memcpy's the code to baseAddr, applies the
//                   writes. SusamuneCfg.c also reads gameId from it to pick
//                   which susamune.ini sections belong to this run.
// =====================================================================

#define SUSAMUNE_MOD_MAGIC          0x534D4F44u  // 'SMOD'
#define SUSAMUNE_MOD_VERSION_LEGACY 1u
#define SUSAMUNE_MOD_VERSION_AUTH   2u

// Disc header bytes 0..3 of each supported revision.
#define SUSAMUNE_MOD_GAME_ID_JP  0x474D534Au  // "GMSJ"
#define SUSAMUNE_MOD_GAME_ID_US  0x474D5345u  // "GMSE"
#define SUSAMUNE_MOD_GAME_ID_PAL 0x474D5350u  // "GMSP"
#define SUSAMUNE_MOD_GAME_ID_LMJ 0x474C4D4Au  // "GLMJ"

#define SUSAMUNE_MOD_BASE_JP  0x80426020u
#define SUSAMUNE_MOD_BASE_US  0x80429800u
#define SUSAMUNE_MOD_BASE_PAL 0x80420D60u
#define SUSAMUNE_MOD_BASE_LMJ 0x804B8400u

// Nintendont's executable fingerprint is not the main.dol file length.  Its
// DOLSize calculation starts with sizeof(dolhdr) (0xE4) and then adds every
// text/data section, whereas the on-disc file has a 0x100-byte padded header.
#define SUSAMUNE_MOD_DOL_SIZE_LMJ 0x00394924u
#define SUSAMUNE_MOD_DOL_MIN_LMJ  0x00003100u
#define SUSAMUNE_MOD_DOL_MAX_LMJ  0x004A6400u

#define SUSAMUNE_MOD_REGION_SIZE 0x80000u
#define SUSAMUNE_SCRATCH 0x40u
#define SUSAMUNE_MOD_MEM1_WORKING_CAP_SIZE 0x50000u
#define SUSAMUNE_MOD_BLOB_MAX_SIZE 0x50000u
#define SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET 0x50000u
#define SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE 0x20000u
#define SUSAMUNE_MOD_SCRATCH_OFFSET \
    (SUSAMUNE_MOD_REGION_SIZE - SUSAMUNE_SCRATCH)
#define SUSAMUNE_DEBUG_STACK_SIZE 0x2000u
#define SUSAMUNE_ARENA_RESERVE_SIZE \
    (SUSAMUNE_MOD_REGION_SIZE + SUSAMUNE_DEBUG_STACK_SIZE)

#define SUSAMUNE_MOD_BASE_FOR_GAME_ID(gameId)                         \
    ((gameId) == SUSAMUNE_MOD_GAME_ID_JP    ? SUSAMUNE_MOD_BASE_JP   \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_US  ? SUSAMUNE_MOD_BASE_US   \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_PAL ? SUSAMUNE_MOD_BASE_PAL  \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_LMJ ? SUSAMUNE_MOD_BASE_LMJ  \
                                             : 0u)

#define SUSAMUNE_MOD_VERSION_FOR_GAME_ID(gameId)                         \
    ((gameId) == SUSAMUNE_MOD_GAME_ID_LMJ ? SUSAMUNE_MOD_VERSION_AUTH   \
                                           : SUSAMUNE_MOD_VERSION_LEGACY)

// V1 write records are (address, replacement). V2 records add the expected
// original word and end the file with a CRC32 of every preceding byte (header,
// code, and records). Bit zero marks a V2 record as an authentication-only
// check; game addresses are always word-aligned.
#define SUSAMUNE_MOD_WRITE_WORDS_LEGACY 2u
#define SUSAMUNE_MOD_WRITE_WORDS_AUTH   3u
#define SUSAMUNE_MOD_WRITE_FLAG_CHECK_ONLY 1u
#define SUSAMUNE_MOD_AUTH_FOOTER_SIZE 4u

// File layout starts with this header and codeSize bytes of code. V1 then has
// writeCount (address, replacement) pairs. V2 has (address, expected,
// replacement) triples followed by a CRC32 of header + code + records.
// codeSize is a multiple of four, so every record and the footer remain
// word-aligned.
struct SusamuneModHeader {
    unsigned int magic;         // SUSAMUNE_MOD_MAGIC
    unsigned int version;       // SUSAMUNE_MOD_VERSION_*
    unsigned int gameId;        // SUSAMUNE_MOD_GAME_ID_*
    unsigned int baseAddr;      // MEM1 address the code is linked at
    unsigned int codeSize;
    unsigned int writeCount;
    unsigned int arenaReserve;  // what getArenaLo() adds; see PatchSusamuneGeckoCodes
    unsigned int fileSize;      // header + code + writes, for bounds checking
};

#define SUSAMUNE_MOD_HEADER_SIZE 32u

// Sunshine's region tag is also used by its settings and ghost services. Keep
// GLMJ out of it: the first Luigi's Mansion payload is a diagnostic and must
// not activate Sunshine's persistence schemas.
#define SUSAMUNE_MOD_REGION_TAG(gameId)                            \
    ((gameId) == SUSAMUNE_MOD_GAME_ID_JP    ? "jp"                 \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_US  ? "us"                 \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_PAL ? "pal"                \
                                            : (const char *)0)

// Payload filenames are game-specific even when two games share a language.
// This prevents a Sunshine mod_jp.bin from ever being selected for GLMJ01.
#define SUSAMUNE_MOD_FILE_TAG(gameId)                              \
    ((gameId) == SUSAMUNE_MOD_GAME_ID_LMJ ? "lmj"                  \
                                           : SUSAMUNE_MOD_REGION_TAG(gameId))

#define SUSAMUNE_MOD_FILE_FMT "mod_%s.bin"

// Portable compile-time checks (no C11 dependency). The reset-safe file
// ceiling has to fit inside the loader's unchanged staging allocation.
typedef char susamune_mod_header_size_check
    [(sizeof(struct SusamuneModHeader) == SUSAMUNE_MOD_HEADER_SIZE) ? 1 : -1];
typedef char susamune_mod_window_size_check
    [(SUSAMUNE_MEM2_MODBIN_SIZE >= SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE) ? 1 : -1];
typedef char susamune_mod_blob_scratch_check
    [(SUSAMUNE_MOD_BLOB_MAX_SIZE <= SUSAMUNE_MOD_SCRATCH_OFFSET) ? 1 : -1];
typedef char susamune_mod_working_cap_check
    [(SUSAMUNE_MOD_BLOB_MAX_SIZE == SUSAMUNE_MOD_MEM1_WORKING_CAP_SIZE) ? 1 : -1];
typedef char susamune_mod_attachment_offset_check
    [(SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET ==
      SUSAMUNE_MOD_MEM1_WORKING_CAP_SIZE) ? 1 : -1];
typedef char susamune_mod_attachment_bounds_check
    [(SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET +
      SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE <= SUSAMUNE_MOD_SCRATCH_OFFSET) ? 1 : -1];
typedef char susamune_mod_attachment_alignment_check
    [(((SUSAMUNE_MOD_BASE_JP | SUSAMUNE_MOD_BASE_US |
        SUSAMUNE_MOD_BASE_PAL | SUSAMUNE_MOD_BASE_LMJ |
        SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET |
        SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE) & 31u) == 0) ? 1 : -1];
typedef char susamune_mod_staging_vault_check
    [(SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE ==
      SUSAMUNE_GHOST_ASSET_VAULT_OFFSET) ? 1 : -1];
typedef char susamune_mod_file_capacity_check
    [(SUSAMUNE_MOD_HEADER_SIZE + SUSAMUNE_MOD_BLOB_MAX_SIZE <=
      SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE) ? 1 : -1];

#define SUSAMUNE_MOD_PPC_PTR  ((struct SusamuneModHeader *)SUSAMUNE_MEM2_MODBIN_PPC_BASE)
#define SUSAMUNE_MOD_PHYS_PTR ((struct SusamuneModHeader *)SUSAMUNE_MEM2_MODBIN_PHYS_BASE)

#endif  // SUSAMUNE_MOD_BIN_H
