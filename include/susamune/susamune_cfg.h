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
// Bump whenever values[]/binds[] change meaning at a given index -- i.e. on any
// removal or reorder in settings_list.h / binds_list.h. Since the mod now ships
// as mod_<region>.bin separately from the launcher, a user can pair a new mod
// with an old kernel; the version check is what makes that combination fall
// back to defaults rather than scramble every setting.
//   1 -> 2: SETTING_INTRO_SKIP removed, shifting the settings after it.
#define SUSAMUNE_CFG_VERSION      2u

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

// Input Display has values wider than the generic one-byte settings table
// (screen coordinates and an RGB colour), so it owns one compact, versioned
// payload at the end of the handoff block. Each field has its own unset value:
// a newer mod can therefore keep its defaults when paired with an older ini,
// while a newer launcher can safely preserve a field it does not yet expose.
#define SUSAMUNE_INPUT_CFG_MAGIC       0x53494443u  // 'SIDC'
#define SUSAMUNE_INPUT_CFG_VERSION     1u
#define SUSAMUNE_INPUT_CFG_U8_UNSET    0xFFu
#define SUSAMUNE_INPUT_CFG_U16_UNSET   0xFFFFu

#define SUSAMUNE_INPUT_VALUES_OFF      0u
#define SUSAMUNE_INPUT_VALUES_STICKS   1u
#define SUSAMUNE_INPUT_VALUES_FULL     2u

#define SUSAMUNE_INPUT_SOURCE_RAW      0u
#define SUSAMUNE_INPUT_SOURCE_PROCESSED 1u

#define SUSAMUNE_INPUT_VALUES_BELOW    0u
#define SUSAMUNE_INPUT_VALUES_ABOVE    1u
#define SUSAMUNE_INPUT_VALUES_INSIDE   2u

struct SusamuneInputDisplayCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short x;
    unsigned short y;
    unsigned char  startVisible;
    unsigned char  scale;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  brightness;
    unsigned char  valueMode;
    unsigned char  valueSource;
    unsigned char  valuePlacement;
    unsigned char  reserved[12];
};

// Metadata Display keeps a compact in-game configuration plus an optional
// hand-authored template. The template is edited in susamune.ini; the game
// menu only selects it and edits the live overlay's layout.
#define SUSAMUNE_METADATA_CFG_MAGIC       0x534D4443u  // 'SMDC'
#define SUSAMUNE_METADATA_CFG_VERSION     1u
#define SUSAMUNE_METADATA_FORMAT_SIZE     240u
#define SUSAMUNE_METADATA_FORMAT_UNSET    0xFFu

#define SUSAMUNE_METADATA_LABEL_SHORT     0u
#define SUSAMUNE_METADATA_LABEL_LONG      1u
#define SUSAMUNE_METADATA_LABEL_CUSTOM    2u

#define SUSAMUNE_METADATA_FIELD_X         (1u << 0)
#define SUSAMUNE_METADATA_FIELD_Y         (1u << 1)
#define SUSAMUNE_METADATA_FIELD_Z         (1u << 2)
#define SUSAMUNE_METADATA_FIELD_ANGLE     (1u << 3)
#define SUSAMUNE_METADATA_FIELD_HSPD      (1u << 4)
#define SUSAMUNE_METADATA_FIELD_VSPD      (1u << 5)
#define SUSAMUNE_METADATA_FIELD_QF        (1u << 6)
#define SUSAMUNE_METADATA_FIELD_CANGLE    (1u << 7)
#define SUSAMUNE_METADATA_FIELD_INVINC    (1u << 8)
#define SUSAMUNE_METADATA_FIELD_GOOP      (1u << 9)
#define SUSAMUNE_METADATA_FIELD_SPIN      (1u << 10)
#define SUSAMUNE_METADATA_FIELD_ALL       ((1u << 11) - 1u)

struct SusamuneMetadataDisplayCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short x;
    unsigned short y;
    unsigned short fieldMask;
    unsigned char  startVisible;
    unsigned char  scale;
    unsigned char  labelMode;
    unsigned char  backgroundAlpha;
    char           format[SUSAMUNE_METADATA_FORMAT_SIZE];
};

// susamune.ini had nothing for the running game version -- either the file is
// absent entirely, or it has no [settings_<region>] section for this disc. The
// kernel cannot author defaults itself -- they live in kSettingDescs on the mod
// side, and duplicating them in the launcher would be a second source of truth
// -- so it raises this flag and the mod answers by issuing a save() during
// init, which fills in this version's sections without disturbing the others.
#define SUSAMUNE_CFG_FLAG_NO_CONFIG 0x1u
// Kernel understands the appended inputDisplay payload and its ini section.
// The flag also prevents a new mod from reading stale bytes there when paired
// with an older launcher that only initialised the shorter struct.
#define SUSAMUNE_CFG_FLAG_INPUT_DISPLAY 0x2u
// Kernel understands metadataDisplay and [metadata_display_<region>].
#define SUSAMUNE_CFG_FLAG_METADATA_DISPLAY 0x4u

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
    struct SusamuneInputDisplayCfg inputDisplay;
    struct SusamuneMetadataDisplayCfg metadataDisplay;
};

#define SUSAMUNE_CFG_PPC_PTR  ((struct SusamuneCfg *)SUSAMUNE_MEM2_CFG_PPC_BASE)
#define SUSAMUNE_CFG_PHYS_PTR ((struct SusamuneCfg *)SUSAMUNE_MEM2_CFG_PHYS_BASE)

// Path of the ini, at the root of whichever device holds it. That is the device
// the launcher was run from, which the kernel may have had to mount as a second
// volume -- see SusamuneCfgIniPath().
#define SUSAMUNE_INI_PATH "/susamune.ini"

// Section headers. [nintendont] holds the launcher's own options (game version,
// per-version disc image paths, and the Nintendont settings that used to live in
// nincfg.bin); the loader owns it and the kernel only copies it through.
#define SUSAMUNE_INI_SECTION_NINTENDONT "nintendont"
//
// Settings and binds are per game version, because a JP route's binds and a PAL
// route's are not the same thing and one launcher now serves all three discs:
// the section names carry the region tag (settings_jp, binds_pal, ...). Only the
// running version's sections are ever parsed or materialised -- the block in
// MEM2 holds exactly one set of values -- so a rewrite copies the other
// versions' sections through as text rather than round-tripping them.
#define SUSAMUNE_INI_SECTION_SETTINGS   "settings"
// One `key = A+DUp` line per configurable action; the button tokens are
// SUSAMUNE_BIND_BUTTON_LIST (binds_list.h), so both sides spell them the same
// way.
#define SUSAMUNE_INI_SECTION_BINDS      "binds"
// Position, scale, colour and optional numeric pad readout. This cannot use
// the generic settings table because several values exceed one byte.
#define SUSAMUNE_INI_SECTION_INPUT_DISPLAY "input_display"
// Native position/angle/speed/QF overlay. Its `format` key is an optional
// custom template containing live-value placeholders such as <x> and <HSpd>.
#define SUSAMUNE_INI_SECTION_METADATA_DISPLAY "metadata_display"
// settings_jp / binds_pal / ...
#define SUSAMUNE_INI_SECTION_SEPARATOR  '_'

// Portable compile-time checks (no C11 dependency): a negative array size
// fails the build if the layout the three toolchains agree on ever drifts.
typedef char susamune_cfg_line0_check[(__builtin_offsetof(struct SusamuneCfg, ackSeq) == 32) ? 1 : -1];
typedef char susamune_cfg_line2_check[(__builtin_offsetof(struct SusamuneCfg, values) == 64) ? 1 : -1];
typedef char susamune_cfg_binds_check[(__builtin_offsetof(struct SusamuneCfg, binds) == 192) ? 1 : -1];
typedef char susamune_input_cfg_size_check[(sizeof(struct SusamuneInputDisplayCfg) == 32) ? 1 : -1];
typedef char susamune_cfg_input_check[(__builtin_offsetof(struct SusamuneCfg, inputDisplay) == 320) ? 1 : -1];
typedef char susamune_metadata_cfg_size_check[(sizeof(struct SusamuneMetadataDisplayCfg) == 256) ? 1 : -1];
typedef char susamune_cfg_metadata_check[(__builtin_offsetof(struct SusamuneCfg, metadataDisplay) == 352) ? 1 : -1];
typedef char susamune_cfg_size_check[(sizeof(struct SusamuneCfg) <= SUSAMUNE_MEM2_CFG_SIZE) ? 1 : -1];

#endif  // SUSAMUNE_CFG_H
