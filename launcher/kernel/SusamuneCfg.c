/*

Susamune settings persistence (Nintendont kernel side).

The mod cannot touch the SD card: file I/O lives on the ARM, and the PPC only
sees the emulated GameCube hardware. So settings move through the MEM2 handoff
block described in susamune_cfg.h:

  boot  -- SusamuneCfgInit() parses /susamune.ini into the block, before the
           game is patched and long before the first frame.
  save  -- the mod stages values into the block and bumps saveSeq;
           SusamuneCfgService() notices, rewrites the ini, and answers through
           status + ackSeq.

The ini is plain text. [settings_<region>] holds the in-game options, keyed by
the stable names in settings_list.h -- shared with the mod, so the key table
here cannot drift from the mod's SettingId order. [binds_<region>] holds one
button combination per configurable action, keyed by binds_list.h and written as
`+`-joined button tokens ("X+DUp") from the same shared list.
[input_display_<region>] holds its wider position/colour configuration.
[metadata_display_<region>] holds the native metadata overlay, including its
optional hand-authored text template. [qft_display_<region>] holds the compact
QFT readout's Creation style. [creation_<region>] holds native HUD
colours and the three custom text overlays.
[nintendont] holds
the launcher's own options -- game version, per-version disc image paths, and
the Nintendont settings that used to live in nincfg.bin -- and belongs to
the loader (SusamuneIni.c); this side only copies it through.

The file lives on the device the launcher was run from, which is not
necessarily the one the game is read from; SusamuneCfgIniPath() (main.c) names
the drive it ended up on.

ILing PBs use a separate fixed binary journal on that same device. Two files
per region alternate generations so an interrupted write leaves one valid
copy, and their independent doorbell avoids rewriting the ini after every PB.

One launcher now serves GMSJ/GMSE/GMSP, and each keeps its own settings and
binds, hence the region tag on the section names. Only the running version's
sections are ever parsed: the handoff block holds one set of values, not three.
Consequently a save cannot simply re-emit the file from the block -- it would
drop the other versions. Instead it reads the old file back and copies every
line through verbatim, substituting freshly written sections in place of this
version's five. Sections the launcher does not know stay byte-for-byte intact.

NOTE: keys the launcher does not recognise *inside our own five sections* are
still dropped, since those sections are regenerated wholesale.

*/

#include "SusamuneCfg.h"
#include "string.h"
#include "alloc.h"
#include "debug.h"
#include "ff_utf8.h"

#include "susamune/susamune_cfg.h"
#include "susamune/mod_bin.h"

// Set by DIinit() from the disc header; SusamuneCfgInit() runs after it.
extern u32 GAME_ID;

// The ini key table, generated from the same list that defines the mod's
// SettingId enum. Index == SettingId == index into SusamuneCfg::values.
#define SUSAMUNE_SETTING_KEY(id, key) key,
static const char *const SettingKeys[] = { SUSAMUNE_SETTING_LIST(SUSAMUNE_SETTING_KEY) };
#undef SUSAMUNE_SETTING_KEY

#define SETTING_KEY_COUNT ((u32)(sizeof(SettingKeys) / sizeof(SettingKeys[0])))
typedef char SettingKeyCountFitsCfg[
    SETTING_KEY_COUNT <= SUSAMUNE_CFG_MAX_SETTINGS ? 1 : -1];

// Same, for the running disc's [binds_<region>] section.
#define SUSAMUNE_BIND_KEY(id, key) key,
static const char *const BindKeys[] = { SUSAMUNE_BIND_LIST(SUSAMUNE_BIND_KEY) };
#undef SUSAMUNE_BIND_KEY

#define BIND_KEY_COUNT ((u32)(sizeof(BindKeys) / sizeof(BindKeys[0])))
typedef char BindKeyCountFitsCfg[
    BIND_KEY_COUNT <= SUSAMUNE_CFG_MAX_BINDS ? 1 : -1];

// Button bit <-> ini token. The third list field is the mod's font glyph.
struct BindButton { u16 bit; const char *token; };
#define SUSAMUNE_BIND_BUTTON_ROW(bit, token, display) { (u16)(bit), token },
static const struct BindButton BindButtons[] = { SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_BIND_BUTTON_ROW) };
#undef SUSAMUNE_BIND_BUTTON_ROW

#define BIND_BUTTON_COUNT ((u32)(sizeof(BindButtons) / sizeof(BindButtons[0])))

static const char *const InputColorKeys[SUSAMUNE_INPUT_COLOR_COUNT] =
{
	"main_stick_rgb", "c_stick_rgb", "a_rgb", "b_rgb", "x_rgb", "y_rgb",
	"l_rgb", "r_rgb", "start_rgb", "z_rgb", "value_rgb",
	"trigger_outline_rgb"
};

static const char *const CreationColorKeys[SUSAMUNE_CREATION_COLOR_COUNT] =
{
	// Slot zero is retained so older Creation payloads keep their layout.
	"water_text_rgb", "fludd_tank_rgb", "timer_streak_rgb",
	"coin_streak_rgb", "red_streak_rgb", "blue_streak_rgb",
	"lives_streak_rgb", "shines_streak_rgb", "life_counter_rgb",
	"timer_normal_1_rgb", "timer_normal_2_rgb", "timer_normal_3_rgb",
	"timer_normal_4_rgb", "timer_normal_5_rgb", "timer_normal_6_rgb",
	"timer_rush_1_rgb", "timer_rush_2_rgb", "timer_rush_3_rgb",
	"timer_rush_4_rgb", "timer_separator_1_rgb", "timer_separator_2_rgb",
	"timer_separator_3_rgb", "timer_label_rgb", "mario_hat_rgb",
	"menu_background_rgb"
};

// Enough for the whole file: the settings plus display payloads for all
// three versions, section headers, and the comment banner.
// A file larger than this is refused rather than truncated (see WriteIniFile).
#define SUSAMUNE_INI_BUF_SIZE 49152

// Longest section name we build: "settings" + '_' + "pal" + NUL.
#define SUSAMUNE_SECTION_NAME_MAX 24

static bool CfgReady = false;
static u32  CfgAckSeq = 0;

#define SUSAMUNE_PB_FILE_COUNT 2
#define SUSAMUNE_PB_PATH_SIZE  40

static u32 PbAckSeq = 0;
static u32 PbGeneration = 0;
static s32 PbActiveFile = -1;
static bool PbReady = false;
static char PbPaths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];

enum PbReadResult
{
	PB_READ_INVALID,
	PB_READ_VALID,
	PB_READ_UNSAFE
};

// "[settings_jp]" and friends for the running disc, built once in
// SusamuneCfgInit. Empty for a game we have no mod for, which is also what
// leaves CfgReady false.
static char SettingsSection[SUSAMUNE_SECTION_NAME_MAX];
static char BindsSection[SUSAMUNE_SECTION_NAME_MAX];
static char InputDisplaySection[SUSAMUNE_SECTION_NAME_MAX];
static char MetadataDisplaySection[SUSAMUNE_SECTION_NAME_MAX];
static char QftDisplaySection[SUSAMUNE_SECTION_NAME_MAX];
static char CreationSection[SUSAMUNE_SECTION_NAME_MAX];

// name + '_' + region tag, e.g. "settings" + "jp".
static void BuildSectionName(char *out, const char *base, const char *region)
{
	u32 n = 0;

	while (*base)
		out[n++] = *base++;
	out[n++] = SUSAMUNE_INI_SECTION_SEPARATOR;
	while (*region)
		out[n++] = *region++;
	out[n] = '\0';
}

static struct SusamuneCfg *CfgBlock(void)
{
	return SUSAMUNE_CFG_PHYS_PTR;
}

// ---------------------------------------------------------------------
// ILing PB binary files
// ---------------------------------------------------------------------

static u32 PbHashWord(u32 hash, u32 value)
{
	return (hash ^ value) * 16777619u;
}

static u32 PbChecksum(const struct SusamuneILingPbFile *file)
{
	u32 hash = 2166136261u;
	u32 i;

	hash = PbHashWord(hash, ((u32)file->version << 16) | file->count);
	hash = PbHashWord(hash, file->gameId);
	hash = PbHashWord(hash, file->generation);
	for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
		hash = PbHashWord(hash, (u32)file->values[i]);
	return hash;
}

static bool PbGenerationIsNewer(u32 candidate, u32 current)
{
	return (s32)(candidate - current) > 0;
}

static bool PbValueIsValid(s32 value)
{
	return value >= SUSAMUNE_ILING_PB_UNSET &&
	       value <= SUSAMUNE_ILING_PB_MAX_QF;
}

static enum PbReadResult ReadPbFile(const char *path,
	                               struct SusamuneILingPbFile *file)
{
	FIL f;
	UINT read = 0;
	u32 i;
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
	{
		dbgprintf("Susamune: could not read PB file %s (%d)\r\n", path, ret);
		return PB_READ_UNSAFE;
	}

	if (f_size(&f) != sizeof(*file))
	{
		closeRet = f_close(&f);
		if (closeRet != FR_OK)
			return PB_READ_UNSAFE;
		dbgprintf("Susamune: ignored invalid PB file %s (size)\r\n", path);
		return PB_READ_INVALID;
	}

	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
	{
		dbgprintf("Susamune: ignored unreadable PB file %s\r\n", path);
		return PB_READ_UNSAFE;
	}

	if (file->magic == SUSAMUNE_ILING_PB_FILE_MAGIC &&
	    file->version != SUSAMUNE_ILING_PB_VERSION)
	{
		dbgprintf("Susamune: unsupported PB file %s (version %u)\r\n",
		          path, file->version);
		return PB_READ_UNSAFE;
	}

	if (file->magic != SUSAMUNE_ILING_PB_FILE_MAGIC ||
	    file->count == 0 || file->count > SUSAMUNE_ILING_PB_MAX_SLOTS ||
	    file->gameId != GAME_ID || file->checksum != PbChecksum(file))
	{
		dbgprintf("Susamune: ignored invalid PB file %s (header)\r\n", path);
		return PB_READ_INVALID;
	}

	for (i = 0; i < file->count; i++)
	{
		if (!PbValueIsValid(file->values[i]))
		{
			dbgprintf("Susamune: ignored invalid PB file %s (value)\r\n", path);
			return PB_READ_INVALID;
		}
	}
	return PB_READ_VALID;
}

static bool InitPbFiles(struct SusamuneCfg *cfg, const char *region)
{
	struct SusamuneILingPbCfg *pbs = &cfg->ilingPbs;
	struct SusamuneILingPbFile file;
	u32 i;
	u32 fileIndex;
	bool safe = true;

	pbs->magic   = SUSAMUNE_ILING_PB_MAGIC;
	pbs->version = SUSAMUNE_ILING_PB_VERSION;
	pbs->count   = SUSAMUNE_ILING_PB_SLOT_COUNT;
	for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
		pbs->values[i] = SUSAMUNE_ILING_PB_UNSET;

	_sprintf(PbPaths[0], "%s/susamune_pbs_v1_%s_a.bin",
	         SusamuneCfgStoragePrefix(), region);
	_sprintf(PbPaths[1], "%s/susamune_pbs_v1_%s_b.bin",
	         SusamuneCfgStoragePrefix(), region);

	PbGeneration = 0;
	PbActiveFile = -1;
	for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
	{
		enum PbReadResult readResult = ReadPbFile(PbPaths[fileIndex], &file);
		if (readResult == PB_READ_UNSAFE)
		{
			safe = false;
			continue;
		}
		if (readResult != PB_READ_VALID)
			continue;
		if (PbActiveFile >= 0 &&
		    !PbGenerationIsNewer(file.generation, PbGeneration))
			continue;

		for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
			pbs->values[i] = SUSAMUNE_ILING_PB_UNSET;
		for (i = 0; i < file.count; i++)
			pbs->values[i] = file.values[i];
		pbs->count = file.count > SUSAMUNE_ILING_PB_SLOT_COUNT
		                 ? file.count : SUSAMUNE_ILING_PB_SLOT_COUNT;
		PbGeneration = file.generation;
		PbActiveFile = (s32)fileIndex;
	}

	PbAckSeq = 0;
	PbReady = safe;
	if (!safe)
		dbgprintf("Susamune: PB persistence disabled to preserve unreadable files\r\n");
	return safe;
}

static int WritePbFile(const struct SusamuneILingPbCfg *pbs)
{
	struct SusamuneILingPbFile file;
	FIL f;
	UINT wrote = 0;
	u32 i;
	u32 target = PbActiveFile == 0 ? 1u : 0u;
	int ret;
	int closeRet;

	if (pbs->count == 0 || pbs->count > SUSAMUNE_ILING_PB_MAX_SLOTS)
		return FR_INVALID_PARAMETER;
	for (i = 0; i < pbs->count; i++)
	{
		if (!PbValueIsValid(pbs->values[i]))
			return FR_INVALID_PARAMETER;
	}

	memset(&file, 0, sizeof(file));
	file.magic      = SUSAMUNE_ILING_PB_FILE_MAGIC;
	file.version    = SUSAMUNE_ILING_PB_VERSION;
	file.count      = pbs->count;
	file.gameId     = GAME_ID;
	file.generation = PbGeneration + 1;
	for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
		file.values[i] = pbs->values[i];
	file.checksum = PbChecksum(&file);

	ret = f_open_char(&f, PbPaths[target], FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
		return ret;

	ret = f_write(&f, &file, sizeof(file), &wrote);
	if (ret == FR_OK && wrote != sizeof(file))
		ret = FR_DISK_ERR;
	closeRet = f_close(&f);
	if (ret == FR_OK && closeRet != FR_OK)
		ret = closeRet;

	if (ret == FR_OK)
	{
		PbGeneration = file.generation;
		PbActiveFile = (s32)target;
	}
	return ret;
}

// ---------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------

static bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Trim in place; returns the first non-space character and terminates the
// string after the last one.
static char *Trim(char *s)
{
	char *end;

	while (*s && IsSpace(*s))
		s++;

	end = s + strlen(s);
	while (end > s && IsSpace(end[-1]))
		end--;
	*end = '\0';

	return s;
}

// Parse an unsigned decimal. Returns false on anything that is not all digits,
// so a typo in a hand-edited ini leaves the mod's default in place rather than
// silently applying a garbage value.
static bool ParseU8(const char *s, u8 *out)
{
	u32 v = 0;

	if (*s == '\0')
		return false;

	for (; *s; s++)
	{
		if (*s < '0' || *s > '9')
			return false;
		v = v * 10 + (u32)(*s - '0');
		if (v > 254)  /* 0xFF is reserved for SUSAMUNE_CFG_UNSET */
			return false;
	}

	*out = (u8)v;
	return true;
}

static bool ParseU16(const char *s, u16 *out)
{
	u32 v = 0;

	if (*s == '\0')
		return false;
	for (; *s; s++)
	{
		if (*s < '0' || *s > '9')
			return false;
		v = v * 10 + (u32)(*s - '0');
		if (v > 65534)  /* 0xFFFF is the unset sentinel */
			return false;
	}
	*out = (u16)v;
	return true;
}

static s32 FindSettingKey(const char *key)
{
	u32 i;

	for (i = 0; i < SETTING_KEY_COUNT; i++)
	{
		if (strcmp(key, SettingKeys[i]) == 0)
			return (s32)i;
	}
	return -1;
}

static s32 FindBindKey(const char *key)
{
	u32 i;

	for (i = 0; i < BIND_KEY_COUNT; i++)
	{
		if (strcmp(key, BindKeys[i]) == 0)
			return (s32)i;
	}
	return -1;
}

static char LowerChar(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Length-delimited, case-insensitive compare against a NUL-terminated token,
// so the file may spell buttons "dup" or "DUp" interchangeably.
static bool TokenEquals(const char *s, u32 len, const char *token)
{
	u32 i;

	for (i = 0; i < len; i++)
	{
		if (token[i] == '\0' || LowerChar(s[i]) != LowerChar(token[i]))
			return false;
	}
	return token[len] == '\0';
}

// Parse "X+DUp" into a button mask. Returns false on any unrecognised token, so
// a typo leaves the mod's compiled-in default in place rather than silently
// producing a half-bind. The literal "none" (and an empty value) is a valid
// unbound entry.
static bool ParseBindMask(const char *s, u16 *out)
{
	u16 mask = 0;

	if (*s == '\0' || TokenEquals(s, (u32)strlen(s), SUSAMUNE_BIND_NONE_TOKEN))
	{
		*out = 0;
		return true;
	}

	while (*s)
	{
		const char *end = s;
		u32         len;
		u32         i;
		bool        found = false;

		while (*end && *end != SUSAMUNE_BIND_SEPARATOR)
			end++;
		len = (u32)(end - s);

		for (i = 0; i < BIND_BUTTON_COUNT; i++)
		{
			if (TokenEquals(s, len, BindButtons[i].token))
			{
				mask |= BindButtons[i].bit;
				found = true;
				break;
			}
		}
		if (!found)
			return false;

		s = (*end == SUSAMUNE_BIND_SEPARATOR) ? end + 1 : end;
	}

	*out = mask;
	return true;
}

// Which section the parser is currently inside. Anything that is not one of
// this version's six sections -- [nintendont], or another version's settings --
// is SECTION_OTHER and left alone.
enum IniSection {
	SECTION_OTHER,
	SECTION_SETTINGS,
	SECTION_BINDS,
	SECTION_INPUT_DISPLAY,
	SECTION_METADATA_DISPLAY,
	SECTION_QFT_DISPLAY,
	SECTION_CREATION
};

static enum IniSection ClassifySection(const char *name)
{
	if (strcmp(name, SettingsSection) == 0)
		return SECTION_SETTINGS;
	if (strcmp(name, BindsSection) == 0)
		return SECTION_BINDS;
	if (strcmp(name, InputDisplaySection) == 0)
		return SECTION_INPUT_DISPLAY;
	if (strcmp(name, MetadataDisplaySection) == 0)
		return SECTION_METADATA_DISPLAY;
	if (strcmp(name, QftDisplaySection) == 0)
		return SECTION_QFT_DISPLAY;
	if (strcmp(name, CreationSection) == 0)
		return SECTION_CREATION;
	return SECTION_OTHER;
}

static void ApplyInputDisplayKey(struct SusamuneInputDisplayCfg *cfg,
				 const char *key, const char *text)
{
	u8  v8;
	u16 v16;

	if (strcmp(key, "x") == 0)
	{
		if (ParseU16(text, &v16)) cfg->x = v16;
	}
	else if (strcmp(key, "y") == 0)
	{
		if (ParseU16(text, &v16)) cfg->y = v16;
	}
	else if (ParseU8(text, &v8))
	{
		if (strcmp(key, "start_visible") == 0) cfg->startVisible = v8;
		else if (strcmp(key, "scale") == 0) cfg->scale = v8;
		else if (strcmp(key, "background_r") == 0) cfg->bgR = v8;
		else if (strcmp(key, "background_g") == 0) cfg->bgG = v8;
		else if (strcmp(key, "background_b") == 0) cfg->bgB = v8;
		else if (strcmp(key, "background_alpha") == 0) cfg->bgA = v8;
		else if (strcmp(key, "brightness") == 0) cfg->brightness = v8;
		else if (strcmp(key, "value_mode") == 0) cfg->valueMode = v8;
		else if (strcmp(key, "value_source") == 0) cfg->valueSource = v8;
		else if (strcmp(key, "value_position") == 0) cfg->valuePlacement = v8;
	}
}

static void CopyMetadataFormat(char *dst, const char *src)
{
	u32 i = 0;

	while (i + 1 < SUSAMUNE_METADATA_FORMAT_SIZE && src[i])
	{
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
}

static void ApplyMetadataDisplayKey(struct SusamuneMetadataDisplayCfg *cfg,
				    const char *key, const char *text)
{
	u8  v8;
	u16 v16;

	if (strcmp(key, "format") == 0)
	{
		CopyMetadataFormat(cfg->format, text);
	}
	else if (strcmp(key, "x") == 0)
	{
		if (ParseU16(text, &v16)) cfg->x = v16;
	}
	else if (strcmp(key, "y") == 0)
	{
		if (ParseU16(text, &v16)) cfg->y = v16;
	}
	else if (strcmp(key, "fields") == 0)
	{
		if (ParseU16(text, &v16)) cfg->fieldMask = v16;
	}
	else if (ParseU8(text, &v8))
	{
		if (strcmp(key, "start_visible") == 0) cfg->startVisible = v8;
		else if (strcmp(key, "scale") == 0) cfg->scale = v8;
		else if (strcmp(key, "label_mode") == 0) cfg->labelMode = v8;
		else if (strcmp(key, "background_alpha") == 0) cfg->backgroundAlpha = v8;
	}
}

static bool ParseQftU8(const char *s, u8 *out)
{
	u32 v = 0;

	if (*s == '\0')
		return false;
	for (; *s; s++)
	{
		if (*s < '0' || *s > '9')
			return false;
		v = v * 10 + (u32)(*s - '0');
		if (v > 255)
			return false;
	}
	*out = (u8)v;
	return true;
}

static bool ParseQftRgb(const char *s, u8 out[3])
{
	u32 channel;

	for (channel = 0; channel < 3; channel++)
	{
		u32 v = 0;
		bool any = false;

		while (*s == ' ' || *s == '\t') s++;
		while (*s >= '0' && *s <= '9')
		{
			any = true;
			v = v * 10 + (u32)(*s++ - '0');
			if (v > 255) return false;
		}
		if (!any) return false;
		out[channel] = (u8)v;
		while (*s == ' ' || *s == '\t') s++;
		if (channel < 2)
		{
			if (*s++ != ',') return false;
		}
	}
	return *s == '\0';
}

static void ApplyInputStyleKey(struct SusamuneInputStyleCfg *cfg,
			       const char *key, const char *text)
{
	u8 value;
	u8 rgb[3];
	u32 i;

	if (strcmp(key, "element_alpha") == 0 && ParseQftU8(text, &value))
	{
		cfg->elementOpacity = value;
		cfg->present |= SUSAMUNE_INPUT_STYLE_OPACITY;
		return;
	}
	if (strcmp(key, "padding") == 0 && ParseQftU8(text, &value))
	{
		cfg->padding = value;
		cfg->present |= SUSAMUNE_INPUT_STYLE_PADDING;
		return;
	}
	for (i = 0; i < SUSAMUNE_INPUT_COLOR_COUNT; i++)
	{
		if (strcmp(key, InputColorKeys[i]) == 0 && ParseQftRgb(text, rgb))
		{
			cfg->rgb[i][0] = rgb[0];
			cfg->rgb[i][1] = rgb[1];
			cfg->rgb[i][2] = rgb[2];
			cfg->present |= SUSAMUNE_INPUT_STYLE_COLOR(i);
			return;
		}
	}
}

static void ApplyQftDisplayKey(struct SusamuneQftDisplayCfg *cfg,
			       const char *key, const char *text)
{
	u8  v8;
	u16 v16;
	u32 i;
	u8  rgb[3];

	if (strlen(key) == 10 && memcmp(key, "text_", 5) == 0 &&
	    key[5] >= '1' && key[5] <= '9' && strcmp(key + 6, "_rgb") == 0 &&
	    ParseQftRgb(text, rgb))
	{
		i = (u32)(key[5] - '1');
		cfg->textRgb[i][0] = rgb[0];
		cfg->textRgb[i][1] = rgb[1];
		cfg->textRgb[i][2] = rgb[2];
		cfg->slotPresent |= SUSAMUNE_QFT_DISPLAY_SLOT(i);
		return;
	}

	if (strcmp(key, "x") == 0)
	{
		if (ParseU16(text, &v16))
		{
			cfg->x = v16;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_X;
		}
	}
	else if (strcmp(key, "y") == 0)
	{
		if (ParseU16(text, &v16))
		{
			cfg->y = v16;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_Y;
		}
	}
	else if (ParseQftU8(text, &v8))
	{
		if (strcmp(key, "scale") == 0)
		{
			cfg->scale = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_SCALE;
		}
		else if (strcmp(key, "text_r") == 0)
		{
			cfg->textR = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_R;
		}
		else if (strcmp(key, "text_g") == 0)
		{
			cfg->textG = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_G;
		}
		else if (strcmp(key, "text_b") == 0)
		{
			cfg->textB = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_B;
		}
		else if (strcmp(key, "text_alpha") == 0)
		{
			cfg->textA = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_A;
		}
		else if (strcmp(key, "background_r") == 0)
		{
			cfg->bgR = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_BG_R;
		}
		else if (strcmp(key, "background_g") == 0)
		{
			cfg->bgG = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_BG_G;
		}
		else if (strcmp(key, "background_b") == 0)
		{
			cfg->bgB = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_BG_B;
		}
		else if (strcmp(key, "background_alpha") == 0)
		{
			cfg->bgA = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_BG_A;
		}
		else if (strcmp(key, "text_brightness") == 0 ||
		         strcmp(key, "brightness") == 0)
		{
			cfg->textBrightness = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_BRIGHTNESS;
		}
		else if (strcmp(key, "padding") == 0)
		{
			cfg->padding = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_PADDING;
		}
		else if (strcmp(key, "leading_zero") == 0)
		{
			cfg->leadingZero = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_LEADING_ZERO;
		}
	}
}

static bool ParseMetadataRgbKey(const char *key, u32 *slot)
{
	u32 value = 0;
	const char *p;

	if (memcmp(key, "char_", 5) != 0)
		return false;
	p = key + 5;
	if (*p < '0' || *p > '9')
		return false;
	while (*p >= '0' && *p <= '9')
	{
		value = value * 10 + (u32)(*p++ - '0');
		if (value > SUSAMUNE_METADATA_STYLE_TEXT_SLOTS)
			return false;
	}
	if (value == 0 || strcmp(p, "_rgb") != 0)
		return false;
	*slot = value - 1;
	return true;
}

static void ApplyMetadataStyleKey(struct SusamuneMetadataStyleCfg *cfg,
				  const char *key, const char *text)
{
	u8 rgb[3];
	u8 v8;
	u32 slot;

	if (ParseMetadataRgbKey(key, &slot) && ParseQftRgb(text, rgb))
	{
		cfg->textRgb[slot][0] = rgb[0];
		cfg->textRgb[slot][1] = rgb[1];
		cfg->textRgb[slot][2] = rgb[2];
		cfg->slotPresent[slot >> 3] |= (u8)(1u << (slot & 7));
		return;
	}
	if (!ParseQftU8(text, &v8))
		return;

	if (strcmp(key, "text_r") == 0)
	{
		cfg->textR = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_TEXT_R;
	}
	else if (strcmp(key, "text_g") == 0)
	{
		cfg->textG = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_TEXT_G;
	}
	else if (strcmp(key, "text_b") == 0)
	{
		cfg->textB = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_TEXT_B;
	}
	else if (strcmp(key, "text_alpha") == 0)
	{
		cfg->textA = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_TEXT_A;
	}
	else if (strcmp(key, "background_r") == 0)
	{
		cfg->bgR = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BG_R;
	}
	else if (strcmp(key, "background_g") == 0)
	{
		cfg->bgG = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BG_G;
	}
	else if (strcmp(key, "background_b") == 0)
	{
		cfg->bgB = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BG_B;
	}
	else if (strcmp(key, "background_alpha") == 0)
	{
		cfg->bgA = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BG_A;
	}
	else if (strcmp(key, "text_brightness") == 0 || strcmp(key, "brightness") == 0)
	{
		cfg->textBrightness = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BRIGHTNESS;
	}
	else if (strcmp(key, "padding") == 0)
	{
		cfg->padding = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_PADDING;
	}
}

static bool ParseCreationWordKey(const char *key, u32 *word, const char **field)
{
	if (memcmp(key, "word", 4) != 0 || key[4] < '1' || key[4] > '3' ||
	    key[5] != '_')
		return false;
	*word = (u32)(key[4] - '1');
	*field = key + 6;
	return true;
}

static bool ParseCreationCharKey(const char *field, u32 *slot)
{
	u32 value = 0;
	const char *p;

	if (memcmp(field, "char_", 5) != 0)
		return false;
	p = field + 5;
	if (*p < '0' || *p > '9')
		return false;
	while (*p >= '0' && *p <= '9')
	{
		value = value * 10 + (u32)(*p++ - '0');
		if (value > SUSAMUNE_CREATION_WORD_CHARS)
			return false;
	}
	if (value == 0 || strcmp(p, "_rgb") != 0)
		return false;
	*slot = value - 1;
	return true;
}

static void ApplyCreationKey(struct SusamuneCreationCfg *cfg,
	                         const char *key, const char *text)
{
	u8 rgb[3];
	u8 v8;
	u16 v16;
	u32 i;
	u32 word;
	u32 slot;
	const char *field;

	for (i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++)
	{
		if (strcmp(key, CreationColorKeys[i]) == 0 && ParseQftRgb(text, rgb))
		{
			cfg->rgb[i][0] = rgb[0];
			cfg->rgb[i][1] = rgb[1];
			cfg->rgb[i][2] = rgb[2];
			cfg->colorPresent |= SUSAMUNE_CREATION_COLOR(i);
			return;
		}
	}
	if (strcmp(key, "show_timer_label") == 0 && ParseQftU8(text, &v8))
	{
		cfg->timerLabelVisible = v8 ? 1 : 0;
		cfg->timerLabelVisiblePresent = 1;
		return;
	}
	if (!ParseCreationWordKey(key, &word, &field))
		return;
	if (strcmp(field, "text") == 0)
	{
		u32 length = (u32)strlen(text);
		if (length > SUSAMUNE_CREATION_WORD_CHARS)
			length = SUSAMUNE_CREATION_WORD_CHARS;
		memcpy(cfg->words[word].text, text, length);
		cfg->words[word].text[length] = '\0';
		cfg->words[word].length = (u8)length;
		return;
	}
	if (strcmp(field, "text_rgb") == 0 && ParseQftRgb(text, rgb))
	{
		for (i = 0; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
		{
			cfg->words[word].rgb[i][0] = rgb[0];
			cfg->words[word].rgb[i][1] = rgb[1];
			cfg->words[word].rgb[i][2] = rgb[2];
		}
		return;
	}
	if (ParseCreationCharKey(field, &slot) && ParseQftRgb(text, rgb))
	{
		cfg->words[word].rgb[slot][0] = rgb[0];
		cfg->words[word].rgb[slot][1] = rgb[1];
		cfg->words[word].rgb[slot][2] = rgb[2];
		return;
	}
	if ((strcmp(field, "x") == 0 || strcmp(field, "y") == 0) &&
	    ParseU16(text, &v16))
	{
		if (field[0] == 'x') cfg->words[word].x = v16;
		else cfg->words[word].y = v16;
		return;
	}
	if (!ParseQftU8(text, &v8))
		return;
	if (strcmp(field, "scale") == 0) cfg->words[word].scale = v8;
	else if (strcmp(field, "text_alpha") == 0) cfg->words[word].textA = v8;
	else if (strcmp(field, "background_r") == 0) cfg->words[word].bgR = v8;
	else if (strcmp(field, "background_g") == 0) cfg->words[word].bgG = v8;
	else if (strcmp(field, "background_b") == 0) cfg->words[word].bgB = v8;
	else if (strcmp(field, "background_alpha") == 0) cfg->words[word].bgA = v8;
	else if (strcmp(field, "text_brightness") == 0) cfg->words[word].textBrightness = v8;
	else if (strcmp(field, "padding") == 0) cfg->words[word].padding = v8;
	else if (strcmp(field, "visible") == 0) cfg->words[word].visible = v8;
}

// Whether the file already carries settings for this game version. When it does
// not, the mod is asked to author them (SUSAMUNE_CFG_FLAG_NO_CONFIG).
static bool SawSettingsSection = false;

static void ParseIni(char *text, struct SusamuneCfg *cfg)
{
	char *line = text;
	enum IniSection section = SECTION_OTHER;

	SawSettingsSection = false;

	while (*line)
	{
		char *next;
		char *eq;
		s32   idx;
		u8    value;
		u16   mask;

		// Split off this line.
		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		else
			next = line + strlen(line);

		line = Trim(line);

		if (*line == '\0' || *line == ';' || *line == '#')
		{
			line = next;
			continue;
		}

		if (*line == '[')
		{
			char *close = strchr(line, ']');
			if (close)
			{
				char *name;
				*close = '\0';
				name = Trim(line + 1);
				section = ClassifySection(name);
				if (section == SECTION_SETTINGS)
					SawSettingsSection = true;
			}
			line = next;
			continue;
		}

		eq = strchr(line, '=');
		if (eq == NULL)
		{
			line = next;
			continue;
		}
		*eq = '\0';

		if (section == SECTION_SETTINGS)
		{
			idx = FindSettingKey(Trim(line));
			if (idx >= 0 && ParseU8(Trim(eq + 1), &value))
				cfg->values[idx] = value;
		}
		else if (section == SECTION_BINDS)
		{
			idx = FindBindKey(Trim(line));
			if (idx >= 0 && ParseBindMask(Trim(eq + 1), &mask))
				cfg->binds[idx] = (u16)(mask & SUSAMUNE_BIND_BUTTON_MASK);
		}
		else if (section == SECTION_INPUT_DISPLAY)
		{
			ApplyInputDisplayKey(&cfg->inputDisplay, Trim(line), Trim(eq + 1));
			ApplyInputStyleKey(&cfg->inputStyle, Trim(line), Trim(eq + 1));
		}
		else if (section == SECTION_METADATA_DISPLAY)
		{
			ApplyMetadataDisplayKey(&cfg->metadataDisplay, Trim(line), Trim(eq + 1));
			ApplyMetadataStyleKey(&cfg->metadataStyle, Trim(line), Trim(eq + 1));
		}
		else if (section == SECTION_QFT_DISPLAY)
		{
			ApplyQftDisplayKey(&cfg->qftDisplay, Trim(line), Trim(eq + 1));
		}
		else if (section == SECTION_CREATION)
		{
			ApplyCreationKey(&cfg->creation, Trim(line), Trim(eq + 1));
		}

		line = next;
	}
}

// ---------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------

// Render a bind mask as "X+DUp", or the "none" token when it has no buttons.
static u32 FormatBindMask(char *buf, u16 mask)
{
	u32 n = 0;
	u32 i;

	for (i = 0; i < BIND_BUTTON_COUNT; i++)
	{
		const char *p;

		if (!(mask & BindButtons[i].bit))
			continue;
		if (n > 0)
			buf[n++] = SUSAMUNE_BIND_SEPARATOR;
		for (p = BindButtons[i].token; *p; p++)
			buf[n++] = *p;
	}

	if (n == 0)
	{
		const char *p;
		for (p = SUSAMUNE_BIND_NONE_TOKEN; *p; p++)
			buf[n++] = *p;
	}

	buf[n] = '\0';
	return n;
}

// Append to an output file, latching the first error so the caller can check
// once at the end instead of after every line.
static void Emit(FIL *f, int *err, const char *s, u32 len)
{
	UINT wrote;
	int  ret;

	if (*err != FR_OK)
		return;

	ret = f_write(f, s, len, &wrote);
	if (ret == FR_OK && wrote != len)
		ret = FR_DISK_ERR;
	*err = ret;
}

static void EmitStr(FIL *f, int *err, const char *s)
{
	Emit(f, err, s, (u32)strlen(s));
}

// Write out this version's [settings_<region>] section, header included.
static void EmitSettingsSection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	char line[96];
	u32  count = cfg->count;
	u32  i;

	if (count > SETTING_KEY_COUNT)
		count = SETTING_KEY_COUNT;

	EmitStr(f, err, "[");
	EmitStr(f, err, SettingsSection);
	EmitStr(f, err, "]\r\n");

	for (i = 0; i < count; i++)
	{
		if (cfg->values[i] == SUSAMUNE_CFG_UNSET)
			continue;
		Emit(f, err, line,
		     (u32)_sprintf(line, "%s = %u\r\n", SettingKeys[i], cfg->values[i]));
	}
}

static void EmitBindsSection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	char line[128];
	char combo[64];
	u32  bindCount = cfg->bindCount;
	u32  i;

	if (bindCount > BIND_KEY_COUNT)
		bindCount = BIND_KEY_COUNT;

	EmitStr(f, err, "[");
	EmitStr(f, err, BindsSection);
	EmitStr(f, err, "]\r\n");

	for (i = 0; i < bindCount; i++)
	{
		if (cfg->binds[i] == SUSAMUNE_CFG_BIND_UNSET)
			continue;
		FormatBindMask(combo, cfg->binds[i]);
		Emit(f, err, line,
		     (u32)_sprintf(line, "%s = %s\r\n", BindKeys[i], combo));
	}
}

static void EmitInputU8(FIL *f, int *err, const char *key, u8 value)
{
	char line[96];
	if (value != SUSAMUNE_INPUT_CFG_U8_UNSET)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitInputU16(FIL *f, int *err, const char *key, u16 value)
{
	char line[96];
	if (value != SUSAMUNE_INPUT_CFG_U16_UNSET)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitMetadataStyleU8(FIL *f, int *err, const char *key, u8 value,
				u16 present, u16 bit)
{
	char line[96];
	if (present & bit)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitInputDisplaySection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	const struct SusamuneInputDisplayCfg *d = &cfg->inputDisplay;
	const struct SusamuneInputStyleCfg *s = &cfg->inputStyle;
	char line[96];
	u32 i;

	EmitStr(f, err, "[");
	EmitStr(f, err, InputDisplaySection);
	EmitStr(f, err, "]\r\n");
	EmitInputU16(f, err, "x", d->x);
	EmitInputU16(f, err, "y", d->y);
	EmitInputU8(f, err, "start_visible", d->startVisible);
	EmitInputU8(f, err, "scale", d->scale);
	EmitInputU8(f, err, "background_r", d->bgR);
	EmitInputU8(f, err, "background_g", d->bgG);
	EmitInputU8(f, err, "background_b", d->bgB);
	EmitInputU8(f, err, "background_alpha", d->bgA);
	EmitInputU8(f, err, "brightness", d->brightness);
	EmitInputU8(f, err, "value_mode", d->valueMode);
	EmitInputU8(f, err, "value_source", d->valueSource);
	EmitInputU8(f, err, "value_position", d->valuePlacement);
	if (s->magic != SUSAMUNE_INPUT_STYLE_MAGIC ||
	    s->version != SUSAMUNE_INPUT_STYLE_VERSION)
		return;
	EmitMetadataStyleU8(f, err, "element_alpha", s->elementOpacity, s->present,
	                    SUSAMUNE_INPUT_STYLE_OPACITY);
	EmitMetadataStyleU8(f, err, "padding", s->padding, s->present,
	                    SUSAMUNE_INPUT_STYLE_PADDING);
	for (i = 0; i < SUSAMUNE_INPUT_COLOR_COUNT; i++)
	{
		if (s->present & SUSAMUNE_INPUT_STYLE_COLOR(i))
		{
			Emit(f, err, line, (u32)_sprintf(
				line, "%s = %u,%u,%u\r\n", InputColorKeys[i],
				s->rgb[i][0], s->rgb[i][1], s->rgb[i][2]));
		}
	}
}

static void EmitMetadataDisplaySection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	const struct SusamuneMetadataDisplayCfg *d = &cfg->metadataDisplay;
	const struct SusamuneMetadataStyleCfg *s = &cfg->metadataStyle;
	u8 backgroundAlpha = d->backgroundAlpha;
	u32 i;
	char line[96];
	if (s->magic == SUSAMUNE_METADATA_STYLE_MAGIC &&
	    s->version == SUSAMUNE_METADATA_STYLE_VERSION &&
	    (s->present & SUSAMUNE_METADATA_STYLE_BG_A) &&
	    (backgroundAlpha == SUSAMUNE_INPUT_CFG_U8_UNSET ||
	     backgroundAlpha == s->bgA))
		backgroundAlpha = s->bgA;

	EmitStr(f, err, "[");
	EmitStr(f, err, MetadataDisplaySection);
	EmitStr(f, err, "]\r\n");
	EmitInputU16(f, err, "x", d->x);
	EmitInputU16(f, err, "y", d->y);
	EmitInputU16(f, err, "fields", d->fieldMask);
	EmitInputU8(f, err, "start_visible", d->startVisible);
	EmitInputU8(f, err, "scale", d->scale);
	EmitInputU8(f, err, "label_mode", d->labelMode);
	if ((u8)d->format[0] != SUSAMUNE_METADATA_FORMAT_UNSET)
	{
		EmitStr(f, err, "format = ");
		EmitStr(f, err, d->format);
		EmitStr(f, err, "\r\n");
	}
	if (s->magic != SUSAMUNE_METADATA_STYLE_MAGIC ||
	    s->version != SUSAMUNE_METADATA_STYLE_VERSION)
	{
		EmitInputU8(f, err, "background_alpha", backgroundAlpha);
		return;
	}
	EmitMetadataStyleU8(f, err, "text_r", s->textR, s->present,
	                    SUSAMUNE_METADATA_STYLE_TEXT_R);
	EmitMetadataStyleU8(f, err, "text_g", s->textG, s->present,
	                    SUSAMUNE_METADATA_STYLE_TEXT_G);
	EmitMetadataStyleU8(f, err, "text_b", s->textB, s->present,
	                    SUSAMUNE_METADATA_STYLE_TEXT_B);
	EmitMetadataStyleU8(f, err, "text_alpha", s->textA, s->present,
	                    SUSAMUNE_METADATA_STYLE_TEXT_A);
	EmitMetadataStyleU8(f, err, "background_r", s->bgR, s->present,
	                    SUSAMUNE_METADATA_STYLE_BG_R);
	EmitMetadataStyleU8(f, err, "background_g", s->bgG, s->present,
	                    SUSAMUNE_METADATA_STYLE_BG_G);
	EmitMetadataStyleU8(f, err, "background_b", s->bgB, s->present,
	                    SUSAMUNE_METADATA_STYLE_BG_B);
	if (s->present & SUSAMUNE_METADATA_STYLE_BG_A)
		Emit(f, err, line, (u32)_sprintf(
			line, "background_alpha = %u\r\n", backgroundAlpha));
	else
		EmitInputU8(f, err, "background_alpha", backgroundAlpha);
	EmitMetadataStyleU8(f, err, "text_brightness", s->textBrightness, s->present,
	                    SUSAMUNE_METADATA_STYLE_BRIGHTNESS);
	EmitMetadataStyleU8(f, err, "padding", s->padding, s->present,
	                    SUSAMUNE_METADATA_STYLE_PADDING);
	for (i = 0; i < SUSAMUNE_METADATA_STYLE_TEXT_SLOTS; i++)
	{
		if (s->slotPresent[i >> 3] & (1u << (i & 7)))
		{
			Emit(f, err, line, (u32)_sprintf(
				line, "char_%u_rgb = %u,%u,%u\r\n", i + 1,
				s->textRgb[i][0], s->textRgb[i][1], s->textRgb[i][2]));
		}
	}
}

static void EmitQftU8(FIL *f, int *err, const char *key, u8 value,
		      u32 present, u32 bit)
{
	char line[96];
	if (present & bit)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitQftU16(FIL *f, int *err, const char *key, u16 value,
		       u32 present, u32 bit)
{
	char line[96];
	if (present & bit)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitQftDisplaySection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	const struct SusamuneQftDisplayCfg *d = &cfg->qftDisplay;
	const u16 p = d->present;
	u32 i;
	char line[96];

	EmitStr(f, err, "[");
	EmitStr(f, err, QftDisplaySection);
	EmitStr(f, err, "]\r\n");
	EmitQftU16(f, err, "x", d->x, p, SUSAMUNE_QFT_DISPLAY_X);
	EmitQftU16(f, err, "y", d->y, p, SUSAMUNE_QFT_DISPLAY_Y);
	EmitQftU8(f, err, "scale", d->scale, p, SUSAMUNE_QFT_DISPLAY_SCALE);
	if (d->version == 1 || d->slotPresent == 0)
	{
		EmitQftU8(f, err, "text_r", d->textR, p, SUSAMUNE_QFT_DISPLAY_TEXT_R);
		EmitQftU8(f, err, "text_g", d->textG, p, SUSAMUNE_QFT_DISPLAY_TEXT_G);
		EmitQftU8(f, err, "text_b", d->textB, p, SUSAMUNE_QFT_DISPLAY_TEXT_B);
	}
	else
	{
		for (i = 0; i < SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS; i++)
		{
			if (d->slotPresent & SUSAMUNE_QFT_DISPLAY_SLOT(i))
			{
				Emit(f, err, line, (u32)_sprintf(
					line, "text_%u_rgb = %u,%u,%u\r\n", i + 1,
					d->textRgb[i][0], d->textRgb[i][1], d->textRgb[i][2]));
			}
		}
	}
	EmitQftU8(f, err, "text_alpha", d->textA, p, SUSAMUNE_QFT_DISPLAY_TEXT_A);
	EmitQftU8(f, err, "background_r", d->bgR, p, SUSAMUNE_QFT_DISPLAY_BG_R);
	EmitQftU8(f, err, "background_g", d->bgG, p, SUSAMUNE_QFT_DISPLAY_BG_G);
	EmitQftU8(f, err, "background_b", d->bgB, p, SUSAMUNE_QFT_DISPLAY_BG_B);
	EmitQftU8(f, err, "background_alpha", d->bgA, p, SUSAMUNE_QFT_DISPLAY_BG_A);
	EmitQftU8(f, err, "text_brightness", d->textBrightness, p,
	          SUSAMUNE_QFT_DISPLAY_TEXT_BRIGHTNESS);
	EmitQftU8(f, err, "padding", d->padding, p, SUSAMUNE_QFT_DISPLAY_PADDING);
	EmitQftU8(f, err, "leading_zero", d->leadingZero, p,
	          SUSAMUNE_QFT_DISPLAY_LEADING_ZERO);
}

static void EmitCreationSection(FIL *f, int *err,
	                            const struct SusamuneCfg *cfg)
{
	const struct SusamuneCreationCfg *d = &cfg->creation;
	char key[40];
	char line[160];
	u32 i;
	u32 word;

	EmitStr(f, err, "[");
	EmitStr(f, err, CreationSection);
	EmitStr(f, err, "]\r\n");
	for (i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++)
	{
		if (d->colorPresent & SUSAMUNE_CREATION_COLOR(i))
			Emit(f, err, line, (u32)_sprintf(
				line, "%s = %u,%u,%u\r\n", CreationColorKeys[i],
				d->rgb[i][0], d->rgb[i][1], d->rgb[i][2]));
	}
	if (d->timerLabelVisiblePresent)
		Emit(f, err, line, (u32)_sprintf(line, "show_timer_label = %u\r\n",
			d->timerLabelVisible));
	for (word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++)
	{
		const struct SusamuneCreationWordCfg *w = &d->words[word];
		const u32 n = word + 1;
		Emit(f, err, line, (u32)_sprintf(line, "word%u_text = %s\r\n", n, w->text));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_visible = %u\r\n", n, w->visible));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_x = %u\r\n", n, w->x));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_y = %u\r\n", n, w->y));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_scale = %u\r\n", n, w->scale));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_text_alpha = %u\r\n", n, w->textA));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_text_brightness = %u\r\n", n, w->textBrightness));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_background_r = %u\r\n", n, w->bgR));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_background_g = %u\r\n", n, w->bgG));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_background_b = %u\r\n", n, w->bgB));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_background_alpha = %u\r\n", n, w->bgA));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_padding = %u\r\n", n, w->padding));
		Emit(f, err, line, (u32)_sprintf(
			line, "word%u_text_rgb = %u,%u,%u\r\n", n,
			w->rgb[0][0], w->rgb[0][1], w->rgb[0][2]));
		for (i = 1; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
		{
			if (w->rgb[i][0] == w->rgb[0][0] &&
			    w->rgb[i][1] == w->rgb[0][1] &&
			    w->rgb[i][2] == w->rgb[0][2])
				continue;
			_sprintf(key, "word%u_char_%u_rgb", n, i + 1);
			Emit(f, err, line, (u32)_sprintf(
				line, "%s = %u,%u,%u\r\n", key,
				w->rgb[i][0], w->rgb[i][1], w->rgb[i][2]));
		}
	}
}

static const char kIniBanner[] =
	"; susamune settings\r\n"
	"; Written by the susamune launcher. Values are edited in-game from the\r\n"
	"; mod menu; the section for the game version you are running is rewritten\r\n"
	"; whenever the menu is closed with changes pending, so comments added\r\n"
	"; inside it are lost. Everything else in this file is preserved.\r\n"
	";\r\n"
	"; Each disc has settings, binds, input_display, metadata_display,\r\n"
	"; qft_display and creation sections.\r\n"
	"; Their suffix is jp = GMSJ, us = GMSE, or pal = GMSP.\r\n"
	"; Metadata format uses \\n for lines and placeholders such as <x> or <HSpd|.2>.\r\n"
	";\r\n"
	"; Bind values are button combinations like Y+Start, or none. menu_toggle\r\n"
	"; is what opens the mod menu -- if you rebind it to something you cannot\r\n"
	"; reproduce, set it back here.\r\n"
	"\r\n"
	"[" SUSAMUNE_INI_SECTION_NINTENDONT "]\r\n";

// Rewrite the ini with this version's six sections replaced.
//
// The whole point of the copy-through is that the other versions' settings are
// never materialised: they exist only as the text we are reading back here.
// Everything outside our six sections -- other regions, [nintendont], comments,
// blank lines -- lands in the output unchanged and in its original order.
static int WriteIniFile(const struct SusamuneCfg *cfg)
{
	FIL   f;
	char *buf;
	char *line;
	UINT  read = 0;
	int   ret;
	int   err = FR_OK;
	bool  skipping = false;
	bool  wroteSettings = false;
	bool  wroteBinds = false;
	bool  wroteInputDisplay = false;
	bool  wroteMetadataDisplay = false;
	bool  wroteQftDisplay = false;
	bool  wroteCreation = false;

	buf = (char*)malloca(SUSAMUNE_INI_BUF_SIZE, 32);
	if (buf == NULL)
		return FR_NOT_ENOUGH_CORE;
	buf[0] = '\0';

	ret = f_open_char(&f, SusamuneCfgIniPath(), FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_OK)
	{
		// Refuse rather than truncate: a partial copy-through would silently
		// delete another version's settings.
		if (f_size(&f) >= SUSAMUNE_INI_BUF_SIZE)
		{
			f_close(&f);
			free(buf);
			return FR_NOT_ENOUGH_CORE;
		}
		if (f_read(&f, buf, SUSAMUNE_INI_BUF_SIZE - 1, &read) != FR_OK)
			read = 0;
		buf[read] = '\0';
		f_close(&f);
	}

	ret = f_open_char(&f, SusamuneCfgIniPath(), FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
	{
		free(buf);
		return ret;
	}

	if (read == 0)
		Emit(&f, &err, kIniBanner, (u32)(sizeof(kIniBanner) - 1));

	line = buf;
	while (*line)
	{
		char *next;
		char *text;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		else
			next = line + strlen(line);

		text = Trim(line);  // also drops the \r of a CRLF file

		if (text[0] == '[')
		{
			char *close = strchr(text, ']');
			if (close)
			{
				// Copy the name out rather than punching a NUL into the line:
				// the OTHER branch below has to emit it back verbatim.
				char sect[SUSAMUNE_SECTION_NAME_MAX];
				enum IniSection kind = SECTION_OTHER;
				u32  len = (u32)(close - text - 1);

				if (len < sizeof(sect))
				{
					memcpy(sect, text + 1, len);
					sect[len] = '\0';
					kind = ClassifySection(Trim(sect));
				}

				skipping = (kind != SECTION_OTHER);
				if (kind == SECTION_SETTINGS)
				{
					EmitSettingsSection(&f, &err, cfg);
					wroteSettings = true;
				}
				else if (kind == SECTION_BINDS)
				{
					EmitBindsSection(&f, &err, cfg);
					wroteBinds = true;
				}
				else if (kind == SECTION_INPUT_DISPLAY)
				{
					EmitInputDisplaySection(&f, &err, cfg);
					wroteInputDisplay = true;
				}
				else if (kind == SECTION_METADATA_DISPLAY)
				{
					EmitMetadataDisplaySection(&f, &err, cfg);
					wroteMetadataDisplay = true;
				}
				else if (kind == SECTION_QFT_DISPLAY)
				{
					EmitQftDisplaySection(&f, &err, cfg);
					wroteQftDisplay = true;
				}
				else if (kind == SECTION_CREATION)
				{
					EmitCreationSection(&f, &err, cfg);
					wroteCreation = true;
				}
			}
		}

		if (!skipping)
		{
			EmitStr(&f, &err, text);
			EmitStr(&f, &err, "\r\n");
		}
		line = next;
	}

	// Absent sections (a fresh file, or a version this card has not run yet)
	// are appended.
	if (!wroteSettings)
	{
		EmitStr(&f, &err, "\r\n");
		EmitSettingsSection(&f, &err, cfg);
	}
	if (!wroteBinds)
	{
		EmitStr(&f, &err, "\r\n");
		EmitBindsSection(&f, &err, cfg);
	}
	if (!wroteInputDisplay)
	{
		EmitStr(&f, &err, "\r\n");
		EmitInputDisplaySection(&f, &err, cfg);
	}
	if (!wroteMetadataDisplay)
	{
		EmitStr(&f, &err, "\r\n");
		EmitMetadataDisplaySection(&f, &err, cfg);
	}
	if (!wroteQftDisplay)
	{
		EmitStr(&f, &err, "\r\n");
		EmitQftDisplaySection(&f, &err, cfg);
	}
	if (!wroteCreation)
	{
		EmitStr(&f, &err, "\r\n");
		EmitCreationSection(&f, &err, cfg);
	}

	ret = f_close(&f);
	if (err == FR_OK && ret != FR_OK)
		err = ret;
	free(buf);
	return err;
}

// ---------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------

static void InitCreationDefaults(struct SusamuneCreationCfg *cfg)
{
	u32 i;
	u32 word;

	cfg->magic = SUSAMUNE_CREATION_CFG_MAGIC;
	cfg->version = SUSAMUNE_CREATION_CFG_VERSION;
	cfg->colorPresent = 0;
	cfg->timerScale = 100;
	cfg->timerX = 0xffff;
	cfg->timerY = 0xffff;
	cfg->timerPositionPresent = 0;
	cfg->timerLabelVisible = 1;
	cfg->timerLabelVisiblePresent = 0;
	cfg->reserved1 = 0;
	for (i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++)
	{
		cfg->rgb[i][0] = 255;
		cfg->rgb[i][1] = 255;
		cfg->rgb[i][2] = 255;
	}
	cfg->rgb[SUSAMUNE_CREATION_MENU_BG][0] = 24;
	cfg->rgb[SUSAMUNE_CREATION_MENU_BG][1] = 28;
	cfg->rgb[SUSAMUNE_CREATION_MENU_BG][2] = 40;
	for (word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++)
	{
		struct SusamuneCreationWordCfg *w = &cfg->words[word];
		w->x = 220;
		w->y = (u16)(80 + word * 42);
		w->scale = 100;
		w->textA = 255;
		w->bgR = w->bgG = w->bgB = 0;
		w->bgA = 128;
		w->textBrightness = 100;
		w->padding = 2;
		w->visible = 0;
		w->length = (u8)_sprintf(w->text, "Custom Text %u", word + 1);
		for (i = 0; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
			w->rgb[i][0] = w->rgb[i][1] = w->rgb[i][2] = 255;
	}
}

void SusamuneCfgInit(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	const char *region = SUSAMUNE_MOD_REGION_TAG(GAME_ID);
	FIL   f;
	char *buf;
	UINT  read;
	u32   i;
	int   ret;

	// Zero the block unconditionally: it survives across app launches, and a
	// stale one left by an earlier boot would be adopted wholesale by a mod
	// that happens to be running now.
	memset(cfg, 0, sizeof(struct SusamuneCfg));
	CfgReady = false;
	PbReady = false;

	if (!SusamuneCfgStorageAvailable())
	{
		// The launcher device was different from drive 0 and could not be
		// mounted. Zero magic advertises an unsupported backend to the mod.
		sync_after_write(cfg, sizeof(struct SusamuneCfg));
		return;
	}

	if (region == NULL)
	{
		// Not one of the supported discs, so there is no mod asking for
		// settings and no section of the ini that belongs to this run. Leaving
		// magic zeroed is what makes the mod (if any) report "no launcher".
		sync_after_write(cfg, sizeof(struct SusamuneCfg));
		CfgReady = false;
		return;
	}

	BuildSectionName(SettingsSection, SUSAMUNE_INI_SECTION_SETTINGS, region);
	BuildSectionName(BindsSection, SUSAMUNE_INI_SECTION_BINDS, region);
	BuildSectionName(InputDisplaySection, SUSAMUNE_INI_SECTION_INPUT_DISPLAY, region);
	BuildSectionName(MetadataDisplaySection, SUSAMUNE_INI_SECTION_METADATA_DISPLAY, region);
	BuildSectionName(QftDisplaySection, SUSAMUNE_INI_SECTION_QFT_DISPLAY, region);
	BuildSectionName(CreationSection, SUSAMUNE_INI_SECTION_CREATION, region);

	for (i = 0; i < SUSAMUNE_CFG_MAX_SETTINGS; i++)
		cfg->values[i] = SUSAMUNE_CFG_UNSET;
	for (i = 0; i < SUSAMUNE_CFG_MAX_BINDS; i++)
		cfg->binds[i] = SUSAMUNE_CFG_BIND_UNSET;
	cfg->inputDisplay.magic          = SUSAMUNE_INPUT_CFG_MAGIC;
	cfg->inputDisplay.version        = SUSAMUNE_INPUT_CFG_VERSION;
	cfg->inputDisplay.x              = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->inputDisplay.y              = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->inputDisplay.startVisible   = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.scale          = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.bgR            = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.bgG            = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.bgB            = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.bgA            = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.brightness     = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.valueMode      = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.valueSource    = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.valuePlacement = SUSAMUNE_INPUT_CFG_U8_UNSET;

	memset(&cfg->metadataDisplay, SUSAMUNE_METADATA_FORMAT_UNSET,
	       sizeof(cfg->metadataDisplay));
	cfg->metadataDisplay.magic        = SUSAMUNE_METADATA_CFG_MAGIC;
	cfg->metadataDisplay.version      = SUSAMUNE_METADATA_CFG_VERSION;
	cfg->metadataDisplay.x            = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->metadataDisplay.y            = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->metadataDisplay.fieldMask    = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->metadataDisplay.startVisible = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->metadataDisplay.scale        = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->metadataDisplay.labelMode    = SUSAMUNE_INPUT_CFG_U8_UNSET;

	cfg->qftDisplay.magic   = SUSAMUNE_QFT_DISPLAY_CFG_MAGIC;
	cfg->qftDisplay.version = SUSAMUNE_QFT_DISPLAY_CFG_VERSION;
	cfg->qftDisplay.present = 0;
	cfg->qftDisplay.slotPresent = 0;
	cfg->metadataStyle.magic = SUSAMUNE_METADATA_STYLE_MAGIC;
	cfg->metadataStyle.version = SUSAMUNE_METADATA_STYLE_VERSION;
	cfg->metadataStyle.present = 0;
	cfg->inputStyle.magic = SUSAMUNE_INPUT_STYLE_MAGIC;
	cfg->inputStyle.version = SUSAMUNE_INPUT_STYLE_VERSION;
	cfg->inputStyle.present = 0;
	InitCreationDefaults(&cfg->creation);

	cfg->magic     = SUSAMUNE_CFG_MAGIC;
	cfg->version   = SUSAMUNE_CFG_VERSION;
	cfg->count     = (u16)SETTING_KEY_COUNT;
	cfg->bindCount = (u16)BIND_KEY_COUNT;
	cfg->flags     = SUSAMUNE_CFG_FLAG_INPUT_DISPLAY |
	                 SUSAMUNE_CFG_FLAG_METADATA_DISPLAY |
	                 SUSAMUNE_CFG_FLAG_QFT_DISPLAY |
	                 SUSAMUNE_CFG_FLAG_METADATA_STYLE |
	                 SUSAMUNE_CFG_FLAG_INPUT_STYLE |
	                 SUSAMUNE_CFG_FLAG_CREATION;
	if (InitPbFiles(cfg, region))
		cfg->flags |= SUSAMUNE_CFG_FLAG_ILING_PBS;

	ret = f_open_char(&f, SusamuneCfgIniPath(), FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_OK)
	{
		buf = (char*)malloca(SUSAMUNE_INI_BUF_SIZE, 32);
		if (buf != NULL)
		{
			read = 0;
			if (f_read(&f, buf, SUSAMUNE_INI_BUF_SIZE - 1, &read) != FR_OK)
				read = 0;
			buf[read] = '\0';
			ParseIni(buf, cfg);
			free(buf);
		}
		f_close(&f);
		if (!SawSettingsSection)
		{
			// The file exists but has never been written by this game version.
			cfg->flags |= SUSAMUNE_CFG_FLAG_NO_CONFIG;
		}
		dbgprintf("Susamune: loaded " SUSAMUNE_INI_PATH " [%s]\r\n", SettingsSection);
	}
	else
	{
		// Every value stays UNSET, so the mod keeps its compiled-in defaults
		// and -- seeing this flag -- writes the file out for us.
		cfg->flags |= SUSAMUNE_CFG_FLAG_NO_CONFIG;
		dbgprintf("Susamune: no " SUSAMUNE_INI_PATH " (%d), using defaults\r\n", ret);
	}

	sync_after_write(cfg, sizeof(struct SusamuneCfg));

	CfgAckSeq = 0;
	CfgReady  = true;
}

bool SusamuneCfgPending(void)
{
	struct SusamuneCfg *cfg = CfgBlock();

	if (!CfgReady)
		return false;

	// Line 0 only: the mod owns it, and reading just that keeps this cheap
	// enough to call every pass of the main loop.
	sync_before_read(cfg, 32);
	return cfg->saveSeq != CfgAckSeq;
}

void SusamuneCfgService(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	u32 seq;
	int ret;

	// Keep the independent PB payload out of this cache transaction.
	sync_before_read(cfg, 32);
	sync_before_read(cfg->values,
	                 sizeof(cfg->values) + sizeof(cfg->binds) +
	                 sizeof(cfg->inputDisplay) + sizeof(cfg->metadataDisplay));
	sync_before_read(&cfg->qftDisplay,
	                 sizeof(cfg->qftDisplay) + sizeof(cfg->metadataStyle) +
	                 sizeof(cfg->inputStyle) + sizeof(cfg->creation));
	seq = cfg->saveSeq;

	ret = WriteIniFile(cfg);
	if (ret != FR_OK)
		dbgprintf("Susamune: failed to write " SUSAMUNE_INI_PATH " (%d)\r\n", ret);

	cfg->status = (u32)ret;
	cfg->ackSeq = seq;
	CfgAckSeq   = seq;

	// Line 1 only. Flushing the whole struct would write our stale copy of
	// values[] back over whatever the mod has staged since (see the cache-line
	// ownership note in susamune_cfg.h).
	sync_after_write(&cfg->ackSeq, 32);
}

bool SusamunePbPending(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	struct SusamuneILingPbCfg *pbs = &cfg->ilingPbs;

	if (!CfgReady || !PbReady)
		return false;

	sync_before_read(pbs, 32);
	if (pbs->magic != SUSAMUNE_ILING_PB_MAGIC ||
	    pbs->version != SUSAMUNE_ILING_PB_VERSION)
		return false;
	return pbs->saveSeq != PbAckSeq;
}

void SusamunePbService(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	struct SusamuneILingPbCfg *pbs = &cfg->ilingPbs;
	u32 seq;
	int ret;

	if (!PbReady)
		return;

	sync_before_read(pbs, 32);
	seq = pbs->saveSeq;
	sync_before_read(pbs->values, sizeof(pbs->values));

	ret = WritePbFile(pbs);
	if (ret != FR_OK)
		dbgprintf("Susamune: failed to write ILing PBs (%d)\r\n", ret);

	pbs->status = (u32)ret;
	pbs->ackSeq = seq;
	PbAckSeq = seq;

	// The PPC owns the control and payload lines after boot.
	sync_after_write(&pbs->ackSeq, 32);
}
