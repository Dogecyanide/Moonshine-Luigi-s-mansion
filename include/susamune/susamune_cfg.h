#ifndef SUSAMUNE_CFG_H
#define SUSAMUNE_CFG_H

#include "susamune/mem2_map.h"
#include "susamune/settings_list.h"
#include "susamune/binds_list.h"

// =====================================================================
// susamune_cfg.h
//
// The handoff block that carries persisted settings and ILing PBs between
// the Nintendont ARM kernel and the mod running on the PPC. Independent
// doorbells ask the kernel to write susamune.ini or the binary PB journal.
// Lives at SUSAMUNE_MEM2_CFG_* (mem2_map.h).
//
// Shared by three toolchains, so this header is plain C with no type
// dependencies -- it uses the built-in types directly rather than u32/u16,
// which each toolchain spells in its own header. Both the ARM kernel and
// the PPC are built big-endian, so the struct needs no byte swapping.
//
// Flow:
//   boot  -- kernel loads the ini and current region's PB journal, zeroes the
//            sequence fields, then flushes the block.
//   init  -- mod invalidates, validates and copies both payloads into private
//            live caches.
//   save  -- mod publishes one payload before bumping its saveSeq. The kernel
//            services that doorbell and answers through its status + ackSeq.
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

// Appended styling keeps the established 32-byte Input Display payload and
// every later mailbox offset stable.
#define SUSAMUNE_INPUT_STYLE_MAGIC       0x53495343u  // 'SISC'
#define SUSAMUNE_INPUT_STYLE_VERSION     1u
#define SUSAMUNE_INPUT_COLOR_MAIN_STICK  0u
#define SUSAMUNE_INPUT_COLOR_C_STICK     1u
#define SUSAMUNE_INPUT_COLOR_A           2u
#define SUSAMUNE_INPUT_COLOR_B           3u
#define SUSAMUNE_INPUT_COLOR_X           4u
#define SUSAMUNE_INPUT_COLOR_Y           5u
#define SUSAMUNE_INPUT_COLOR_L           6u
#define SUSAMUNE_INPUT_COLOR_R           7u
#define SUSAMUNE_INPUT_COLOR_START       8u
#define SUSAMUNE_INPUT_COLOR_Z           9u
#define SUSAMUNE_INPUT_COLOR_VALUES      10u
#define SUSAMUNE_INPUT_COLOR_TRIGGER_OUTLINE 11u
#define SUSAMUNE_INPUT_COLOR_COUNT       12u

#define SUSAMUNE_INPUT_STYLE_OPACITY     (1u << 0)
#define SUSAMUNE_INPUT_STYLE_PADDING     (1u << 1)
#define SUSAMUNE_INPUT_STYLE_COLOR(i)    (1u << (2u + (i)))
#define SUSAMUNE_INPUT_STYLE_ALL         ((1u << (2u + SUSAMUNE_INPUT_COLOR_COUNT)) - 1u)

struct SusamuneInputStyleCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short present;
    unsigned char  elementOpacity;
    unsigned char  padding;
    unsigned char  rgb[SUSAMUNE_INPUT_COLOR_COUNT][3];
    unsigned char  reserved[18];
};

// Shared Creation payload for native HUD/model colours and three heapless
// custom text overlays. It is appended, so every older mailbox offset stays
// stable when a launcher and mod from adjacent builds are paired.
#define SUSAMUNE_CREATION_CFG_MAGIC       0x53435243u  // 'SCRC'
#define SUSAMUNE_CREATION_CFG_VERSION     1u
#define SUSAMUNE_CREATION_COLOR_COUNT     25u
#define SUSAMUNE_CREATION_WORD_COUNT      3u
#define SUSAMUNE_CREATION_WORD_CHARS      32u
#define SUSAMUNE_CREATION_WORD_TEXT_SIZE  (SUSAMUNE_CREATION_WORD_CHARS + 1u)
#define SUSAMUNE_CREATION_COLOR(index)    (1u << (index))

#define SUSAMUNE_CREATION_FLUDD_CIRCLE     0u
#define SUSAMUNE_CREATION_FLUDD_WATER      1u
#define SUSAMUNE_CREATION_TIMER_BG         2u
#define SUSAMUNE_CREATION_COIN_BG          3u
#define SUSAMUNE_CREATION_RED_BG           4u
#define SUSAMUNE_CREATION_BLUE_BG          5u
#define SUSAMUNE_CREATION_LIVES_BG         6u
#define SUSAMUNE_CREATION_SHINES_BG        7u
#define SUSAMUNE_CREATION_LIFE_TEXT         8u
#define SUSAMUNE_CREATION_TIMER_CHAR_FIRST  9u
#define SUSAMUNE_CREATION_TIMER_CHAR_COUNT 13u
#define SUSAMUNE_CREATION_SHINE_OUTFIT     22u
#define SUSAMUNE_CREATION_MARIO_HAT         23u
#define SUSAMUNE_CREATION_MENU_BG           24u

struct SusamuneCreationWordCfg {
    unsigned short x;
    unsigned short y;
    unsigned char  scale;
    unsigned char  textA;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  textBrightness;
    unsigned char  padding;
    unsigned char  visible;
    unsigned char  length;
    char           text[SUSAMUNE_CREATION_WORD_TEXT_SIZE];
    unsigned char  rgb[SUSAMUNE_CREATION_WORD_CHARS][3];
    unsigned char  reserved;
};

struct SusamuneCreationCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short reserved0;
    unsigned int   colorPresent;
    unsigned char  rgb[SUSAMUNE_CREATION_COLOR_COUNT][3];
    unsigned char  timerScale;
    unsigned short timerX;
    unsigned short timerY;
    unsigned char  timerPositionPresent;
    unsigned char  reserved1[3];
    struct SusamuneCreationWordCfg words[SUSAMUNE_CREATION_WORD_COUNT];
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

// Shared presentation fields for the compact QFT readout. `present` lets a
// hand-written ini omit individual fields without reserving 0xFF as an unset
// value, so fully opaque colours remain representable.
#define SUSAMUNE_QFT_DISPLAY_CFG_MAGIC   0x53514643u  // 'SQFC'
#define SUSAMUNE_QFT_DISPLAY_CFG_VERSION 2u
#define SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS  9u

#define SUSAMUNE_QFT_DISPLAY_X           (1u << 0)
#define SUSAMUNE_QFT_DISPLAY_Y           (1u << 1)
#define SUSAMUNE_QFT_DISPLAY_SCALE       (1u << 2)
#define SUSAMUNE_QFT_DISPLAY_TEXT_R      (1u << 3)
#define SUSAMUNE_QFT_DISPLAY_TEXT_G      (1u << 4)
#define SUSAMUNE_QFT_DISPLAY_TEXT_B      (1u << 5)
#define SUSAMUNE_QFT_DISPLAY_TEXT_A      (1u << 6)
#define SUSAMUNE_QFT_DISPLAY_BG_R        (1u << 7)
#define SUSAMUNE_QFT_DISPLAY_BG_G        (1u << 8)
#define SUSAMUNE_QFT_DISPLAY_BG_B        (1u << 9)
#define SUSAMUNE_QFT_DISPLAY_BG_A        (1u << 10)
#define SUSAMUNE_QFT_DISPLAY_TEXT_BRIGHTNESS (1u << 11)
#define SUSAMUNE_QFT_DISPLAY_PADDING     (1u << 12)
#define SUSAMUNE_QFT_DISPLAY_LEADING_ZERO (1u << 13)
#define SUSAMUNE_QFT_DISPLAY_ALL         ((1u << 14) - 1u)
#define SUSAMUNE_QFT_DISPLAY_SLOT(slot)  (1u << (slot))
#define SUSAMUNE_QFT_DISPLAY_ALL_SLOTS   ((1u << SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS) - 1u)

struct SusamuneQftDisplayCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short present;
    unsigned short x;
    unsigned short y;
    unsigned char  scale;
    unsigned char  textR;
    unsigned char  textG;
    unsigned char  textB;
    unsigned char  textA;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  textBrightness;
    unsigned char  padding;
    unsigned char  reservedV1[9];
    unsigned short slotPresent;
    unsigned char  textRgb[SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS][3];
    unsigned char  leadingZero;
    unsigned char  reserved[2];
};

// Creation styling added to Metadata without moving its established 256-byte
// config or the IL PB mailbox that follows it. Version 2 gives standard fields
// stable maximum-width character ranges so changing digits cannot shift rows.
#define SUSAMUNE_METADATA_STYLE_MAGIC       0x534D5343u  // 'SMSC'
#define SUSAMUNE_METADATA_STYLE_VERSION     2u
#define SUSAMUNE_METADATA_STYLE_TEXT_SLOTS  256u
#define SUSAMUNE_METADATA_STYLE_SLOT_BYTES  32u

#define SUSAMUNE_METADATA_STYLE_TEXT_R      (1u << 0)
#define SUSAMUNE_METADATA_STYLE_TEXT_G      (1u << 1)
#define SUSAMUNE_METADATA_STYLE_TEXT_B      (1u << 2)
#define SUSAMUNE_METADATA_STYLE_TEXT_A      (1u << 3)
#define SUSAMUNE_METADATA_STYLE_BG_R        (1u << 4)
#define SUSAMUNE_METADATA_STYLE_BG_G        (1u << 5)
#define SUSAMUNE_METADATA_STYLE_BG_B        (1u << 6)
#define SUSAMUNE_METADATA_STYLE_BG_A        (1u << 7)
#define SUSAMUNE_METADATA_STYLE_BRIGHTNESS  (1u << 8)
#define SUSAMUNE_METADATA_STYLE_PADDING     (1u << 9)
#define SUSAMUNE_METADATA_STYLE_ALL         ((1u << 10) - 1u)

struct SusamuneMetadataStyleCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short present;
    unsigned char  textR;
    unsigned char  textG;
    unsigned char  textB;
    unsigned char  textA;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  textBrightness;
    unsigned char  padding;
    unsigned char  reserved0[14];
    unsigned char  slotPresent[SUSAMUNE_METADATA_STYLE_SLOT_BYTES];
    unsigned char  textRgb[SUSAMUNE_METADATA_STYLE_TEXT_SLOTS][3];
};

// susamune.ini had nothing for the running game version -- either the file is
// absent entirely, or it has no [settings_<region>] section for this disc. The
// kernel cannot author defaults itself -- they live in the mod's descriptor
// pools, and duplicating them in the launcher would be a second source of truth
// -- so it raises this flag and the mod answers by issuing a save() during
// init, which fills in this version's sections without disturbing the others.
#define SUSAMUNE_CFG_FLAG_NO_CONFIG 0x1u
// Kernel understands the appended inputDisplay payload and its ini section.
// The flag also prevents a new mod from reading stale bytes there when paired
// with an older launcher that only initialised the shorter struct.
#define SUSAMUNE_CFG_FLAG_INPUT_DISPLAY 0x2u
// Kernel understands metadataDisplay and [metadata_display_<region>].
#define SUSAMUNE_CFG_FLAG_METADATA_DISPLAY 0x4u
// Kernel understands the independent ILing PB payload and save doorbell.
#define SUSAMUNE_CFG_FLAG_ILING_PBS 0x8u
// Kernel understands qftDisplay and [qft_display_<region>].
#define SUSAMUNE_CFG_FLAG_QFT_DISPLAY 0x10u
// Kernel understands Metadata's appended Creation style and character colours.
#define SUSAMUNE_CFG_FLAG_METADATA_STYLE 0x20u
// Kernel understands Input Display's appended Creation colour payload.
#define SUSAMUNE_CFG_FLAG_INPUT_STYLE 0x40u
// Kernel/backend understands [creation_<region>] and the appended payload.
#define SUSAMUNE_CFG_FLAG_CREATION 0x80u

// IL PBs use stable result slots: ordinary rows use retail Shine ids, while
// independent Secret and Any% rows occupy otherwise-unused ids through 120.
// Seven spare values keep the payload cache-line-sized and allow append-only
// additions without moving anything in the handoff block.
#define SUSAMUNE_ILING_PB_MAGIC          0x53495042u  // 'SIPB'
#define SUSAMUNE_ILING_PB_FILE_MAGIC     0x53504246u  // 'SPBF'
#define SUSAMUNE_ILING_PB_VERSION        1u
#define SUSAMUNE_ILING_PB_SLOT_COUNT     121u
#define SUSAMUNE_ILING_PB_MAX_SLOTS      128u
#define SUSAMUNE_ILING_PB_UNSET          (-1)
#define SUSAMUNE_ILING_PB_MAX_QF         0x000AF9B0

struct SusamuneILingPbCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned short count;
    unsigned int   saveSeq;
    unsigned char  pad0[20];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;
    unsigned int   status;
    unsigned char  pad1[24];

    // --- cache lines 2+: written by the kernel at boot, by the mod on save ---
    signed int values[SUSAMUNE_ILING_PB_MAX_SLOTS];
};

// Fixed binary record written by the ARM kernel. Two generations are kept per
// region, so an interrupted write cannot destroy the previous valid PB list.
struct SusamuneILingPbFile {
    unsigned int   magic;
    unsigned short version;
    unsigned short count;
    unsigned int   gameId;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved[12];
    signed int     values[SUSAMUNE_ILING_PB_MAX_SLOTS];
};

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
    struct SusamuneILingPbCfg ilingPbs;
    // Appended after the independent PB mailbox so its established offsets do
    // not move when an old launcher and a new mod are paired (or vice versa).
    struct SusamuneQftDisplayCfg qftDisplay;
    struct SusamuneMetadataStyleCfg metadataStyle;
    struct SusamuneInputStyleCfg inputStyle;
    struct SusamuneCreationCfg creation;
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
// Compact three-decimal QFT readout presentation, edited from Creation.
#define SUSAMUNE_INI_SECTION_QFT_DISPLAY "qft_display"
// Native HUD/model colours plus the custom text overlays.
#define SUSAMUNE_INI_SECTION_CREATION "creation"
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
typedef char susamune_qft_display_cfg_size_check[(sizeof(struct SusamuneQftDisplayCfg) == 64) ? 1 : -1];
typedef char susamune_qft_display_v1_tail_check[(__builtin_offsetof(struct SusamuneQftDisplayCfg, slotPresent) == 32) ? 1 : -1];
typedef char susamune_metadata_style_cfg_size_check[(sizeof(struct SusamuneMetadataStyleCfg) == 832) ? 1 : -1];
typedef char susamune_metadata_style_slots_check[(__builtin_offsetof(struct SusamuneMetadataStyleCfg, textRgb) == 64) ? 1 : -1];
typedef char susamune_input_style_cfg_size_check[(sizeof(struct SusamuneInputStyleCfg) == 64) ? 1 : -1];
typedef char susamune_creation_word_cfg_size_check[(sizeof(struct SusamuneCreationWordCfg) == 144) ? 1 : -1];
typedef char susamune_creation_cfg_size_check[(sizeof(struct SusamuneCreationCfg) == 528) ? 1 : -1];
typedef char susamune_iling_pb_cfg_ack_check[(__builtin_offsetof(struct SusamuneILingPbCfg, ackSeq) == 32) ? 1 : -1];
typedef char susamune_iling_pb_cfg_values_check[(__builtin_offsetof(struct SusamuneILingPbCfg, values) == 64) ? 1 : -1];
typedef char susamune_iling_pb_cfg_size_check[(sizeof(struct SusamuneILingPbCfg) == 576) ? 1 : -1];
typedef char susamune_iling_pb_file_values_check[(__builtin_offsetof(struct SusamuneILingPbFile, values) == 32) ? 1 : -1];
typedef char susamune_iling_pb_file_size_check[(sizeof(struct SusamuneILingPbFile) == 544) ? 1 : -1];
typedef char susamune_cfg_iling_pb_check[(__builtin_offsetof(struct SusamuneCfg, ilingPbs) == 608) ? 1 : -1];
typedef char susamune_cfg_qft_display_check[(__builtin_offsetof(struct SusamuneCfg, qftDisplay) == 1184) ? 1 : -1];
typedef char susamune_cfg_metadata_style_check[(__builtin_offsetof(struct SusamuneCfg, metadataStyle) == 1248) ? 1 : -1];
typedef char susamune_cfg_input_style_check[(__builtin_offsetof(struct SusamuneCfg, inputStyle) == 2080) ? 1 : -1];
typedef char susamune_cfg_creation_check[(__builtin_offsetof(struct SusamuneCfg, creation) == 2144) ? 1 : -1];
typedef char susamune_cfg_expanded_size_check[(sizeof(struct SusamuneCfg) == 2672) ? 1 : -1];
typedef char susamune_cfg_size_check[
    (sizeof(struct SusamuneCfg) <=
     SUSAMUNE_MEM2_PB_LIVE_PPC_BASE - SUSAMUNE_MEM2_CFG_PPC_BASE) ? 1 : -1];

#endif  // SUSAMUNE_CFG_H
