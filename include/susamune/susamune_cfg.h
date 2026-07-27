#ifndef SUSAMUNE_CFG_H
#define SUSAMUNE_CFG_H

#include "susamune/mem2_map.h"
#include "susamune/settings_list.h"
#include "susamune/binds_list.h"

// =====================================================================
// susamune_cfg.h
//
// The handoff block that carries persisted settings between the
// Nintendont ARM kernel and the mod running on the PPC, and doubles as
// the doorbell the mod rings to ask the kernel to write susamune.ini
// back to the SD card. Lives at SUSAMUNE_MEM2_CFG_* (mem2_map.h).
//
// Shared by three toolchains, so this header is plain C with no type
// dependencies -- it uses the built-in types directly rather than u32/u16,
// which each toolchain spells in its own header. Both the ARM kernel and
// the PPC are built big-endian, so the struct needs no byte swapping.
//
// Flow:
//   boot  -- kernel parses /susamune.ini, fills magic/version/count/values,
//            zeroes the sequence fields, flushes.
//   init  -- mod invalidates, validates magic+version, copies values into
//            its own BSS SettingsData (see settings.hxx). The MEM2 block is
//            never read again during gameplay.
//   save  -- mod copies BSS -> values, flushes, then bumps saveSeq. The
//            kernel's main loop notices saveSeq != ackSeq, rewrites the ini,
//            and reports back through status + ackSeq.
//
// CACHE-LINE OWNERSHIP (this is why the padding exists): the PPC flushes
// whole 32-byte cache lines, so a field the ARM writes must never share a
// line with a field the PPC writes -- a PPC writeback would otherwise clobber
// the ARM's newer value with a stale copy. Line 1 is therefore owned
// exclusively by the kernel and the mod only ever invalidates-and-reads it.
// =====================================================================

#define SUSAMUNE_CFG_MAGIC        0x53434647u  // 'SCFG'
#define SUSAMUNE_CFG_VERSION      1u

// Capacity of values[] / binds[]. Fixed (rather than SETTING_COUNT /
// BIND_COUNT) so the block size is stable as entries are added: a kernel and a
// mod built at different times still agree on the layout, and count/bindCount
// say how much of each is meaningful.
#define SUSAMUNE_CFG_MAX_SETTINGS 128
#define SUSAMUNE_CFG_MAX_BINDS    64

// Value meaning "the ini had no entry for this setting" -- the mod leaves the
// compiled-in default in place. Also what an absent/unparsable ini yields.
#define SUSAMUNE_CFG_UNSET        0xFFu
// Same, for a bind. 0 is a real value ("unbound"), so the sentinel has to be
// distinct from it.
#define SUSAMUNE_CFG_BIND_UNSET   0xFFFFu

// No susamune.ini existed on the SD card. The kernel cannot write a default
// file itself -- the default values live in kSettingDescs on the mod side, and
// duplicating them in the launcher would be a second source of truth -- so it
// raises this flag and the mod answers by issuing a save() during init, which
// creates the file with a complete set of defaults.
#define SUSAMUNE_CFG_FLAG_NO_FILE 0x1u

struct SusamuneCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned short count;    // entries of values[] the writer filled in
    unsigned int   saveSeq;  // mod -> kernel: bump to request an ini write
    unsigned int   flags;    // SUSAMUNE_CFG_FLAG_*
    // Entries of binds[] the writer filled in. Zero from a kernel built before
    // binds existed (it memsets only as much of the block as it knows about),
    // which is exactly the "no persisted binds -- keep the defaults" answer.
    unsigned short bindCount;
    unsigned char  pad0[14];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;   // kernel -> mod: echoes saveSeq once written
    unsigned int   status;   // kernel -> mod: 0 = ok, else the FatFS FRESULT
    unsigned char  pad1[24];

    // --- cache line 2+: written by the kernel at boot, by the mod on save ---
    unsigned char  values[SUSAMUNE_CFG_MAX_SETTINGS];
    unsigned short binds[SUSAMUNE_CFG_MAX_BINDS];
};

#define SUSAMUNE_CFG_PPC_PTR  ((struct SusamuneCfg *)SUSAMUNE_MEM2_CFG_PPC_BASE)
#define SUSAMUNE_CFG_PHYS_PTR ((struct SusamuneCfg *)SUSAMUNE_MEM2_CFG_PHYS_BASE)

// Path of the ini on the SD card root.
#define SUSAMUNE_INI_PATH "/susamune.ini"

// Section headers. [nintendont] is reserved for launcher-side keys (ISO path
// and friends) that will replace the loader's command-line flags; the kernel
// parses and rewrites it today but has no keys in it yet.
#define SUSAMUNE_INI_SECTION_NINTENDONT "nintendont"
#define SUSAMUNE_INI_SECTION_SETTINGS   "settings"
// [binds] holds one `key = A+DUp` line per configurable action; the button
// tokens are SUSAMUNE_BIND_BUTTON_LIST (binds_list.h), so both sides spell
// them the same way.
#define SUSAMUNE_INI_SECTION_BINDS      "binds"

// Portable compile-time checks (no C11 dependency): a negative array size
// fails the build if the layout the three toolchains agree on ever drifts.
typedef char susamune_cfg_line0_check[(__builtin_offsetof(struct SusamuneCfg, ackSeq) == 32) ? 1 : -1];
typedef char susamune_cfg_line2_check[(__builtin_offsetof(struct SusamuneCfg, values) == 64) ? 1 : -1];
typedef char susamune_cfg_binds_check[(__builtin_offsetof(struct SusamuneCfg, binds) == 192) ? 1 : -1];
typedef char susamune_cfg_size_check[(sizeof(struct SusamuneCfg) <= SUSAMUNE_MEM2_CFG_SIZE) ? 1 : -1];

#endif  // SUSAMUNE_CFG_H
