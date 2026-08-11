#ifndef SUSAMUNE_MEM2_MAP_H
#define SUSAMUNE_MEM2_MAP_H

// Shared Wii MEM2 layout for the PPC loader/mod and the ARM Nintendont kernel.
// ARM uses physical 0x1xxxxxxx addresses; PPC cached aliases add 0x80000000.
// Keep this file C-compatible: it is included by all three build toolchains.

// ISO images and real discs are mutually exclusive, so both readers share the
// first 3 MiB. RealDI divides it into front/drive/tail-cache subranges.
#define NIN_MEM2_DISC_CACHE_PHYS_BASE         0x11000000u
#define NIN_MEM2_DISC_CACHE_PPC_BASE          0x91000000u
#define NIN_MEM2_DISC_CACHE_SIZE              0x00300000u

// Nintendont buffers relocated below the savestate window.
#define NIN_MEM2_SEGABOOT_PHYS_BASE          0x11300000u
#define NIN_MEM2_SEGABOOT_PPC_BASE           0x91300000u
#define NIN_MEM2_SEGABOOT_SIZE               0x00100000u

#define NIN_MEM2_DIMM_PHYS_BASE              0x11400000u
#define NIN_MEM2_DIMM_PPC_BASE               0x91400000u
#define NIN_MEM2_DIMM_SIZE                   0x00300000u

#define NIN_MEM2_DI_SCRATCH_PHYS_BASE        0x11700000u
#define NIN_MEM2_DI_SCRATCH_PPC_BASE         0x91700000u
#define NIN_MEM2_DI_SCRATCH_SIZE             0x00080000u

// Used only by the PPC loader before control passes to the game/kernel.
#define NIN_MEM2_LOADER_DISC_PHYS_BASE       0x11780000u
#define NIN_MEM2_LOADER_DISC_PPC_BASE        0x91780000u
#define NIN_MEM2_LOADER_DISC_SIZE            0x00180000u

// patch.bin / patch.txt handoff: the loader writes through the cached PPC
// alias and the ARM kernel consumes the same bytes through the physical alias.
#define NIN_MEM2_FILE_PATCH_PHYS_BASE        0x11900000u
#define NIN_MEM2_FILE_PATCH_PPC_BASE         0x91900000u
#define NIN_MEM2_FILE_PATCH_SIZE             0x005E0000u

// mod_<region>.bin staging (struct SusamuneModHeader, mod_bin.h). The PPC
// loader reads the file for the detected disc here before booting the kernel;
// PatchSusamune() copies the code out into MEM1 and nothing reads it again, so
// the mod is resident in exactly one place for the rest of the session.
//
// It shares the file-patch buffer's lifetime -- both are loader-to-kernel
// handoffs consumed at PatchGame time -- so it is carved off that buffer's tail
// rather than given memory of its own: 128 KiB out of six MiB, and the game
// never ships a patch.bin anyway. Deliberately NOT taken out of the snapshot
// window, whose payload can legitimately grow to fill its reservation.
#define SUSAMUNE_MEM2_MODBIN_PHYS_BASE       0x11EE0000u
#define SUSAMUNE_MEM2_MODBIN_PPC_BASE        0x91EE0000u
#define SUSAMUNE_MEM2_MODBIN_SIZE            0x00020000u

// Dedicated Susamune snapshot window. No Nintendont buffer may overlap this
// range. Its exclusive end is the settings block below.
#define SUSAMUNE_MEM2_SNAPSHOT_PHYS_BASE     0x11F00000u
#define SUSAMUNE_MEM2_SNAPSHOT_PPC_BASE      0x91F00000u
#define SUSAMUNE_MEM2_SNAPSHOT_SIZE          0x00FF0000u

// Settings/PB handoff block (struct SusamuneCfg, susamune_cfg.h). The ARM
// kernel fills it before boot and services the mod's independent ini and PB
// save doorbells. Carved off the top of the snapshot window rather than from
// Nintendont's buffers, since the snapshot's 16 MiB reservation is far larger
// than its use. Deliberately outside the snapshot payload so a savestate load
// does not rewind persistent data.
#define SUSAMUNE_MEM2_CFG_PHYS_BASE          0x12EF0000u
#define SUSAMUNE_MEM2_CFG_PPC_BASE           0x92EF0000u
#define SUSAMUNE_MEM2_CFG_SIZE               0x00010000u

// PPC-only live PB mirror. The ARM doorbell payload remains immutable while a
// save is in flight; later PB changes queue here for the next write.
#define SUSAMUNE_MEM2_PB_LIVE_PPC_BASE       0x92EFFE00u
#define SUSAMUNE_MEM2_PB_LIVE_SIZE           0x00000200u

#define NIN_MEM2_KERNEL_PHYS_BASE            0x12F00000u
#define NIN_MEM2_KERNEL_PPC_BASE             0x92F00000u

// Compile-time adjacency checks. Besides documenting the intended ownership,
// these prevent a future size/address edit from silently entering the snapshot.
#if NIN_MEM2_DISC_CACHE_PHYS_BASE + NIN_MEM2_DISC_CACHE_SIZE != NIN_MEM2_SEGABOOT_PHYS_BASE
#error "MEM2 map gap/overlap between disc cache and SegaBoot"
#endif
#if NIN_MEM2_SEGABOOT_PHYS_BASE + NIN_MEM2_SEGABOOT_SIZE != NIN_MEM2_DIMM_PHYS_BASE
#error "MEM2 map gap/overlap between SegaBoot and DIMM buffers"
#endif
#if NIN_MEM2_DIMM_PHYS_BASE + NIN_MEM2_DIMM_SIZE != NIN_MEM2_DI_SCRATCH_PHYS_BASE
#error "MEM2 map gap/overlap between DIMM and DI scratch buffers"
#endif
#if NIN_MEM2_DI_SCRATCH_PPC_BASE + NIN_MEM2_DI_SCRATCH_SIZE != NIN_MEM2_LOADER_DISC_PPC_BASE
#error "MEM2 map gap/overlap before the loader disc buffer"
#endif
#if NIN_MEM2_LOADER_DISC_PPC_BASE + NIN_MEM2_LOADER_DISC_SIZE != NIN_MEM2_FILE_PATCH_PPC_BASE
#error "MEM2 map gap/overlap before the file-patch buffer"
#endif
#if NIN_MEM2_FILE_PATCH_PHYS_BASE + NIN_MEM2_FILE_PATCH_SIZE != SUSAMUNE_MEM2_MODBIN_PHYS_BASE
#error "MEM2 file-patch buffer must end at the mod-blob staging area"
#endif
#if SUSAMUNE_MEM2_MODBIN_PHYS_BASE + SUSAMUNE_MEM2_MODBIN_SIZE != SUSAMUNE_MEM2_SNAPSHOT_PHYS_BASE
#error "MEM2 mod-blob staging area must end at the snapshot window"
#endif
#if SUSAMUNE_MEM2_SNAPSHOT_PHYS_BASE + SUSAMUNE_MEM2_SNAPSHOT_SIZE != SUSAMUNE_MEM2_CFG_PHYS_BASE
#error "MEM2 snapshot window must end at the settings block"
#endif
#if SUSAMUNE_MEM2_CFG_PHYS_BASE + SUSAMUNE_MEM2_CFG_SIZE != NIN_MEM2_KERNEL_PHYS_BASE
#error "MEM2 settings block must end at the Nintendont kernel"
#endif
#if SUSAMUNE_MEM2_PB_LIVE_PPC_BASE + SUSAMUNE_MEM2_PB_LIVE_SIZE != NIN_MEM2_KERNEL_PPC_BASE
#error "MEM2 live PB mirror must end at the Nintendont kernel"
#endif

// Physical-to-PPC cached alias checks.
#if NIN_MEM2_DISC_CACHE_PHYS_BASE + 0x80000000u != NIN_MEM2_DISC_CACHE_PPC_BASE
#error "Invalid disc-cache PPC alias"
#endif
#if NIN_MEM2_SEGABOOT_PHYS_BASE + 0x80000000u != NIN_MEM2_SEGABOOT_PPC_BASE
#error "Invalid SegaBoot PPC alias"
#endif
#if NIN_MEM2_DIMM_PHYS_BASE + 0x80000000u != NIN_MEM2_DIMM_PPC_BASE
#error "Invalid DIMM PPC alias"
#endif
#if NIN_MEM2_DI_SCRATCH_PHYS_BASE + 0x80000000u != NIN_MEM2_DI_SCRATCH_PPC_BASE
#error "Invalid DI-scratch PPC alias"
#endif
#if NIN_MEM2_LOADER_DISC_PHYS_BASE + 0x80000000u != NIN_MEM2_LOADER_DISC_PPC_BASE
#error "Invalid loader-disc PPC alias"
#endif
#if NIN_MEM2_FILE_PATCH_PHYS_BASE + 0x80000000u != NIN_MEM2_FILE_PATCH_PPC_BASE
#error "Invalid file-patch PPC alias"
#endif
#if SUSAMUNE_MEM2_SNAPSHOT_PHYS_BASE + 0x80000000u != SUSAMUNE_MEM2_SNAPSHOT_PPC_BASE
#error "Invalid snapshot PPC alias"
#endif
#if SUSAMUNE_MEM2_MODBIN_PHYS_BASE + 0x80000000u != SUSAMUNE_MEM2_MODBIN_PPC_BASE
#error "Invalid mod-blob PPC alias"
#endif
#if SUSAMUNE_MEM2_CFG_PHYS_BASE + 0x80000000u != SUSAMUNE_MEM2_CFG_PPC_BASE
#error "Invalid settings-block PPC alias"
#endif
#if NIN_MEM2_KERNEL_PHYS_BASE + 0x80000000u != NIN_MEM2_KERNEL_PPC_BASE
#error "Invalid kernel PPC alias"
#endif

#endif // SUSAMUNE_MEM2_MAP_H
