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

The ini is plain text with three sections. [settings] holds the in-game
options, keyed by the stable names in settings_list.h -- shared with the mod,
so the key table here cannot drift from the mod's SettingId order. [binds]
holds one button combination per configurable action, keyed by binds_list.h and
written as `+`-joined button tokens ("X+DUp") from the same shared list.
[nintendont] is reserved for launcher options (ISO path and friends) that will
eventually replace the loader's command-line flags; it is recognised but has no
keys yet.

NOTE: a save rewrites the whole file, so hand-added keys the launcher does not
recognise are dropped. Nothing is lost today because [nintendont] is empty.

*/

#include "SusamuneCfg.h"
#include "string.h"
#include "alloc.h"
#include "debug.h"
#include "ff_utf8.h"

#include "susamune/susamune_cfg.h"

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

// Button bit <-> token, from the list the mod renders combos with, so the
// spelling in the file matches the spelling in the menu.
struct BindButton { u16 bit; const char *token; };
#define SUSAMUNE_BIND_BUTTON_ROW(bit, token) { (u16)(bit), token },
static const struct BindButton BindButtons[] = { SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_BIND_BUTTON_ROW) };
#undef SUSAMUNE_BIND_BUTTON_ROW

#define BIND_BUTTON_COUNT ((u32)(sizeof(BindButtons) / sizeof(BindButtons[0])))

// Enough for the whole file: ~24 keys at well under 40 bytes each, plus the
// section headers and comment banner.
#define SUSAMUNE_INI_BUF_SIZE 4096

static bool CfgReady = false;
static u32  CfgAckSeq = 0;

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

// Which section the parser is currently inside. Keys outside a known section
// (i.e. [nintendont]) are not ours yet.
enum IniSection { SECTION_OTHER, SECTION_SETTINGS, SECTION_BINDS };

static void ParseIni(char *text, struct SusamuneCfg *cfg)
{
	char *line = text;
	enum IniSection section = SECTION_OTHER;

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
				if (strcmp(name, SUSAMUNE_INI_SECTION_SETTINGS) == 0)
					section = SECTION_SETTINGS;
				else if (strcmp(name, SUSAMUNE_INI_SECTION_BINDS) == 0)
					section = SECTION_BINDS;
				else
					section = SECTION_OTHER;
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

static u32 WriteIniText(char *buf, u32 cap, const struct SusamuneCfg *cfg)
{
	u32 n = 0;
	u32 i;
	u32 count = cfg->count;
	u32 bindCount = cfg->bindCount;
	char combo[64];

	if (count > SETTING_KEY_COUNT)
		count = SETTING_KEY_COUNT;
	if (bindCount > BIND_KEY_COUNT)
		bindCount = BIND_KEY_COUNT;

	n += (u32)_sprintf(buf + n,
	                   "; susamune settings\r\n"
	                   "; Written by the susamune launcher. Values are edited in-game\r\n"
	                   "; from the mod menu; this file is rewritten whenever the menu is\r\n"
	                   "; closed with changes pending, so comments added here are lost.\r\n"
	                   ";\r\n"
	                   "; [binds] values are button combinations like Y+Start, or none.\r\n"
	                   "; menu_toggle is what opens the mod menu -- if you rebind it to\r\n"
	                   "; something you cannot reproduce, set it back here.\r\n"
	                   "\r\n"
	                   "[" SUSAMUNE_INI_SECTION_NINTENDONT "]\r\n"
	                   "\r\n"
	                   "[" SUSAMUNE_INI_SECTION_SETTINGS "]\r\n");

	for (i = 0; i < count; i++)
	{
		if (cfg->values[i] == SUSAMUNE_CFG_UNSET)
			continue;
		if (n + 64 > cap)
			break;
		n += (u32)_sprintf(buf + n, "%s = %u\r\n", SettingKeys[i], cfg->values[i]);
	}

	if (n + 64 <= cap)
		n += (u32)_sprintf(buf + n, "\r\n[" SUSAMUNE_INI_SECTION_BINDS "]\r\n");

	for (i = 0; i < bindCount; i++)
	{
		if (cfg->binds[i] == SUSAMUNE_CFG_BIND_UNSET)
			continue;
		if (n + 96 > cap)
			break;
		FormatBindMask(combo, cfg->binds[i]);
		n += (u32)_sprintf(buf + n, "%s = %s\r\n", BindKeys[i], combo);
	}

	return n;
}

static int WriteIniFile(const struct SusamuneCfg *cfg)
{
	FIL   f;
	char *buf;
	u32   len;
	UINT  wrote;
	int   ret;

	buf = (char*)malloca(SUSAMUNE_INI_BUF_SIZE, 32);
	if (buf == NULL)
		return FR_NOT_ENOUGH_CORE;

	len = WriteIniText(buf, SUSAMUNE_INI_BUF_SIZE, cfg);

	ret = f_open_char(&f, SUSAMUNE_INI_PATH, FA_WRITE | FA_CREATE_ALWAYS);
	if (ret == FR_OK)
	{
		f_lseek(&f, 0);
		ret = f_write(&f, buf, len, &wrote);
		if (ret == FR_OK && wrote != len)
			ret = FR_DISK_ERR;
		f_close(&f);
	}

	free(buf);
	return ret;
}

// ---------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------

void SusamuneCfgInit(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	FIL   f;
	char *buf;
	UINT  read;
	u32   i;
	int   ret;

	memset(cfg, 0, sizeof(struct SusamuneCfg));
	for (i = 0; i < SUSAMUNE_CFG_MAX_SETTINGS; i++)
		cfg->values[i] = SUSAMUNE_CFG_UNSET;
	for (i = 0; i < SUSAMUNE_CFG_MAX_BINDS; i++)
		cfg->binds[i] = SUSAMUNE_CFG_BIND_UNSET;

	cfg->magic     = SUSAMUNE_CFG_MAGIC;
	cfg->version   = SUSAMUNE_CFG_VERSION;
	cfg->count     = (u16)SETTING_KEY_COUNT;
	cfg->bindCount = (u16)BIND_KEY_COUNT;

	ret = f_open_char(&f, SUSAMUNE_INI_PATH, FA_READ | FA_OPEN_EXISTING);
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
		dbgprintf("Susamune: loaded " SUSAMUNE_INI_PATH "\r\n");
	}
	else
	{
		// Every value stays UNSET, so the mod keeps its compiled-in defaults
		// and -- seeing this flag -- writes the file out for us.
		cfg->flags |= SUSAMUNE_CFG_FLAG_NO_FILE;
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
