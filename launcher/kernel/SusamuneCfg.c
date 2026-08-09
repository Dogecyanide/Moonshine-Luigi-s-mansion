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
optional hand-authored text template.
[nintendont] holds
the launcher's own options -- game version, per-version disc image paths, and
the Nintendont settings that used to live in nincfg.bin -- and belongs to
the loader (SusamuneIni.c); this side only copies it through.

The file lives on the device the launcher was run from, which is not
necessarily the one the game is read from; SusamuneCfgIniPath() (main.c) names
the drive it ended up on.

One launcher now serves GMSJ/GMSE/GMSP, and each keeps its own settings and
binds, hence the region tag on the section names. Only the running version's
sections are ever parsed: the handoff block holds one set of values, not three.
Consequently a save cannot simply re-emit the file from the block -- it would
drop the other versions. Instead it reads the old file back and copies every
line through verbatim, substituting freshly written sections in place of this
version's four. Sections the launcher does not know stay byte-for-byte intact.

NOTE: keys the launcher does not recognise *inside our own four sections* are
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

// Same, for the [binds] section.
#define SUSAMUNE_BIND_KEY(id, key) key,
static const char *const BindKeys[] = { SUSAMUNE_BIND_LIST(SUSAMUNE_BIND_KEY) };
#undef SUSAMUNE_BIND_KEY

#define BIND_KEY_COUNT ((u32)(sizeof(BindKeys) / sizeof(BindKeys[0])))

// Button bit <-> ini token. The third list field is the mod's font glyph.
struct BindButton { u16 bit; const char *token; };
#define SUSAMUNE_BIND_BUTTON_ROW(bit, token, display) { (u16)(bit), token },
static const struct BindButton BindButtons[] = { SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_BIND_BUTTON_ROW) };
#undef SUSAMUNE_BIND_BUTTON_ROW

#define BIND_BUTTON_COUNT ((u32)(sizeof(BindButtons) / sizeof(BindButtons[0])))

// Enough for the whole file: the settings plus both display payloads for all
// three versions, section headers, and the comment banner.
// A file larger than this is refused rather than truncated (see WriteIniFile).
#define SUSAMUNE_INI_BUF_SIZE 12288

// Longest section name we build: "settings" + '_' + "pal" + NUL.
#define SUSAMUNE_SECTION_NAME_MAX 24

static bool CfgReady = false;
static u32  CfgAckSeq = 0;

// "[settings_jp]" and friends for the running disc, built once in
// SusamuneCfgInit. Empty for a game we have no mod for, which is also what
// leaves CfgReady false.
static char SettingsSection[SUSAMUNE_SECTION_NAME_MAX];
static char BindsSection[SUSAMUNE_SECTION_NAME_MAX];
static char InputDisplaySection[SUSAMUNE_SECTION_NAME_MAX];
static char MetadataDisplaySection[SUSAMUNE_SECTION_NAME_MAX];

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
// this version's four sections -- [nintendont], or another version's settings --
// is SECTION_OTHER and left alone.
enum IniSection {
	SECTION_OTHER,
	SECTION_SETTINGS,
	SECTION_BINDS,
	SECTION_INPUT_DISPLAY,
	SECTION_METADATA_DISPLAY
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
		}
		else if (section == SECTION_METADATA_DISPLAY)
		{
			ApplyMetadataDisplayKey(&cfg->metadataDisplay, Trim(line), Trim(eq + 1));
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

static void EmitInputDisplaySection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	const struct SusamuneInputDisplayCfg *d = &cfg->inputDisplay;

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
}

static void EmitMetadataDisplaySection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	const struct SusamuneMetadataDisplayCfg *d = &cfg->metadataDisplay;

	EmitStr(f, err, "[");
	EmitStr(f, err, MetadataDisplaySection);
	EmitStr(f, err, "]\r\n");
	EmitInputU16(f, err, "x", d->x);
	EmitInputU16(f, err, "y", d->y);
	EmitInputU16(f, err, "fields", d->fieldMask);
	EmitInputU8(f, err, "start_visible", d->startVisible);
	EmitInputU8(f, err, "scale", d->scale);
	EmitInputU8(f, err, "label_mode", d->labelMode);
	EmitInputU8(f, err, "background_alpha", d->backgroundAlpha);
	if ((u8)d->format[0] != SUSAMUNE_METADATA_FORMAT_UNSET)
	{
		EmitStr(f, err, "format = ");
		EmitStr(f, err, d->format);
		EmitStr(f, err, "\r\n");
	}
}

static const char kIniBanner[] =
	"; susamune settings\r\n"
	"; Written by the susamune launcher. Values are edited in-game from the\r\n"
	"; mod menu; the section for the game version you are running is rewritten\r\n"
	"; whenever the menu is closed with changes pending, so comments added\r\n"
	"; inside it are lost. Everything else in this file is preserved.\r\n"
	";\r\n"
	"; Each disc has settings, binds, input_display and metadata_display sections.\r\n"
	"; Their suffix is jp = GMSJ, us = GMSE, or pal = GMSP.\r\n"
	"; Metadata format uses \\n for lines and placeholders such as <x> or <HSpd|.2>.\r\n"
	";\r\n"
	"; Bind values are button combinations like Y+Start, or none. menu_toggle\r\n"
	"; is what opens the mod menu -- if you rebind it to something you cannot\r\n"
	"; reproduce, set it back here.\r\n"
	"\r\n"
	"[" SUSAMUNE_INI_SECTION_NINTENDONT "]\r\n";

// Rewrite the ini with this version's four sections replaced.
//
// The whole point of the copy-through is that the other versions' settings are
// never materialised: they exist only as the text we are reading back here.
// Everything outside our four sections -- other regions, [nintendont], comments,
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

	ret = f_close(&f);
	if (err == FR_OK && ret != FR_OK)
		err = ret;
	free(buf);
	return err;
}

// ---------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------

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

	cfg->magic     = SUSAMUNE_CFG_MAGIC;
	cfg->version   = SUSAMUNE_CFG_VERSION;
	cfg->count     = (u16)SETTING_KEY_COUNT;
	cfg->bindCount = (u16)BIND_KEY_COUNT;
	cfg->flags     = SUSAMUNE_CFG_FLAG_INPUT_DISPLAY |
	                 SUSAMUNE_CFG_FLAG_METADATA_DISPLAY;

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

	sync_before_read(cfg, sizeof(struct SusamuneCfg));
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
