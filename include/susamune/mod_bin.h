#ifndef SUSAMUNE_MOD_BIN_H
#define SUSAMUNE_MOD_BIN_H

#include "susamune/mem2_map.h"

// =====================================================================
// mod_bin.h
//
// The on-disc format of mod_<region>.bin and the MEM2 window it is staged
// in. The mod used to be a byte array compiled into the Nintendont kernel
// (one launcher per game version, and a second copy of the blob resident in
// MEM2 for the whole session). It is now a file next to the launcher's
// boot.dol that the loader reads for the detected disc and the kernel copies
// into MEM1, so one launcher serves GMSJ/GMSE/GMSP.
//
// The hook writes travel with the code: their addresses are per-version, so
// a blob without its write list is not applicable to anything.
//
// Shared by all three toolchains, so this header is plain C with no type
// dependencies. Everything is big-endian on both sides; no swapping.
//
// Flow:
//   loader (PPC) -- knows the game id before booting the kernel; reads
//                   <launch_dir>/mod_<region>.bin into the MEM2 window below
//                   and flushes it. Writes a zeroed header if there is none.
//   kernel (ARM) -- PatchSusamune() validates the header against the running
//                   GAME_ID, memcpy's the code to baseAddr, applies the
//                   writes. SusamuneCfg.c also reads gameId from it to pick
//                   which susamune.ini sections belong to this run.
// =====================================================================

#define SUSAMUNE_MOD_MAGIC   0x534D4F44u  // 'SMOD'
#define SUSAMUNE_MOD_VERSION 1u

// Disc header bytes 0..3 of each supported revision.
#define SUSAMUNE_MOD_GAME_ID_JP  0x474D534Au  // "GMSJ"
#define SUSAMUNE_MOD_GAME_ID_US  0x474D5345u  // "GMSE"
#define SUSAMUNE_MOD_GAME_ID_PAL 0x474D5350u  // "GMSP"

// File layout: this header, then codeSize bytes of code, then writeCount
// pairs of (addr, val). codeSize is a multiple of 4, so the write list is
// word-aligned relative to the header.
struct SusamuneModHeader {
    unsigned int magic;         // SUSAMUNE_MOD_MAGIC
    unsigned int version;       // SUSAMUNE_MOD_VERSION
    unsigned int gameId;        // SUSAMUNE_MOD_GAME_ID_*
    unsigned int baseAddr;      // MEM1 address the code is linked at
    unsigned int codeSize;
    unsigned int writeCount;
    unsigned int arenaReserve;  // what getArenaLo() adds; see PatchSusamuneGeckoCodes
    unsigned int fileSize;      // header + code + writes, for bounds checking
};

#define SUSAMUNE_MOD_HEADER_SIZE 32u

// The file name for a given disc id, spelled the same way by the build
// (scripts/gen_mod_bin.py) and the loader. Null for a game we have no mod for.
#define SUSAMUNE_MOD_REGION_TAG(gameId)                            \
    ((gameId) == SUSAMUNE_MOD_GAME_ID_JP    ? "jp"                 \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_US  ? "us"                 \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_PAL ? "pal"                \
                                            : (const char *)0)

#define SUSAMUNE_MOD_FILE_FMT "mod_%s.bin"

// Portable compile-time checks (no C11 dependency). The window has to hold a
// blob that fills the whole mod region plus the header and write list;
// SUSAMUNE_MOD_REGION_SIZE itself lives in addresses.hxx, which is C++-only, so
// the ceiling is spelled out here.
typedef char susamune_mod_header_size_check
    [(sizeof(struct SusamuneModHeader) == SUSAMUNE_MOD_HEADER_SIZE) ? 1 : -1];
typedef char susamune_mod_window_size_check
    [(SUSAMUNE_MEM2_MODBIN_SIZE >= 0x18000u + 0x1000u) ? 1 : -1];

#define SUSAMUNE_MOD_PPC_PTR  ((struct SusamuneModHeader *)SUSAMUNE_MEM2_MODBIN_PPC_BASE)
#define SUSAMUNE_MOD_PHYS_PTR ((struct SusamuneModHeader *)SUSAMUNE_MEM2_MODBIN_PHYS_BASE)

#endif  // SUSAMUNE_MOD_BIN_H
