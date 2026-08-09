/*

The susamune launcher GUI.

Three screens, all driven from SusamuneMenuRun():

  main      Launch Game / Version / Path / Settings, centre justified.
  browse    A file browser rooted at the list of devices, plus a pseudo-entry
            for the disc drive. Reached with A on Path.
  settings  The Nintendont options that moved into susamune.ini, one
            column, with help text under a rule at the bottom.

Everything the user changes here is persisted to [nintendont] in susamune.ini
on the device the launcher was run from -- see SusamuneIni.c. NIN_CFG is
filled in only at the moment of launch: it is the transport to the kernel, not
a second place settings live.

Rendering is a full redraw every frame rather than the dirty-flag scheme the
old Nintendont menu used. The path sentinel blinks, so something is animating
anyway, and at 60 Hz over a handful of text rows the cost is invisible.

*/
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "global.h"
#include "font.h"
#include "exi.h"
#include "FPad.h"
#include "menu.h"
#include "SusamuneIni.h"
#include "SusamuneMenu.h"
#include "ff_utf8.h"
#include "diskio.h"

// Grey, for options and devices that exist but cannot be used here.
#define DARK_GRAY 0x666666FF

// Shown in place of a path that has never been configured. Red, and blinks
// when the user tries to launch anyway.
static const char kPathUnset[] = "<not set - press A>";

// Rows of the main menu.
enum
{
	ROW_LAUNCH = 0,
	ROW_VERSION,
	ROW_PATH,
	ROW_SETTINGS,

	ROW_COUNT
};

// Vertical layout. The header occupies rows 0-2 at MENU_POS_Y.
#define MAIN_Y_LAUNCH   (MENU_POS_Y + 20*6)
#define MAIN_Y_VERSION  (MENU_POS_Y + 20*8)
#define MAIN_Y_PATH     (MENU_POS_Y + 20*9)
#define MAIN_Y_SETTINGS (MENU_POS_Y + 20*10)
#define MAIN_Y_ERROR    (MENU_POS_Y + 20*12)

// Blink the sentinel for about a second and a half: 8 frames lit, 8 dark.
#define BLINK_HALF_PERIOD 8
#define BLINK_FRAMES      (BLINK_HALF_PERIOD * 12)

// Longest path the main menu will show before ellipsizing from the left.
#define PATH_DISPLAY_MAX 46

// The file browser gives up past this many entries in one directory. Games
// directories are small; this is only here so a pathological directory cannot
// exhaust the heap.
#define BROWSE_MAX_ENTRIES 1024
#define BROWSE_ROWS 12

// Browser layout. The list, the "n more below" line, the rule and the help
// line have to fit above 480 with room for overscan.
#define BROWSE_Y_PATH  (MENU_POS_Y + 20*5)
#define BROWSE_Y_RULE_TOP (MENU_POS_Y + 20*6 + 4)
#define BROWSE_Y_LIST  (MENU_POS_Y + 20*7)
#define BROWSE_Y_MORE  (BROWSE_Y_LIST + BROWSE_ROWS*20)
#define BROWSE_Y_RULE  (BROWSE_Y_MORE + 22)
#define BROWSE_Y_HELP  (BROWSE_Y_RULE + 8)

static const char *LauncherDev = "sd";
static bool CanSave = false;
static bool IniDirty = false;

// Error line under the main menu, cleared by any cursor movement.
static char ErrorLine[128];
static u32  BlinkFrames = 0;

/** Shared drawing helpers **/

// Centre a formatted line horizontally. The TTF is monospace at DEFAULT_SIZE,
// 10 pixels per character, which is what makes this arithmetic work.
static void PrintCenter(u32 color, int y, const char *fmt, ...)
	__attribute__ ((format (printf, 3, 4)));

static void PrintCenter(u32 color, int y, const char *fmt, ...)
{
	char buf[128];
	va_list ap;
	int len;
	int cols = 0;
	int i;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (len < 0)
		return;
	if (len > (int)sizeof(buf) - 1)
		len = (int)sizeof(buf) - 1;

	// Count characters, not bytes: the cursor arrows are three-byte UTF-8 and
	// each still occupies one 10px cell.
	for (i = 0; i < len; i++)
	{
		if ((buf[i] & 0xC0) != 0x80)
			cols++;
	}

	PrintFormat(DEFAULT_SIZE, color, (640 - cols*10) / 2, y, "%s", buf);
}

// Horizontal rule separating a list from the help text below it.
static void DrawRule(int y)
{
	GRRLIB_Rectangle(MENU_POS_X, y, 640 - MENU_POS_X*2, 1, BLACK, true);
}

// Left-ellipsize: a path is far more identifiable by its tail.
static const char *ShortenPath(const char *path, char *buf, u32 bufSize, u32 maxLen)
{
	u32 len = (u32)strlen(path);

	if (len <= maxLen)
		return path;
	if (maxLen < 4 || bufSize < maxLen + 1)
		return path;

	snprintf(buf, bufSize, "...%s", path + (len - (maxLen - 3)));
	return buf;
}

/** Key repeat **/

typedef struct
{
	u32 Up;
	u32 Down;
	u32 Left;
	u32 Right;
} HeldCounters;

#define FPAD_REPEAT(Key) \
static int Repeat##Key(HeldCounters *h) \
{ \
	int ret = 0; \
	if (FPAD_##Key(1)) { \
		ret = !((h->Key == 0 || h->Key > 10) ? (h->Key & 0b111) : 1); \
		h->Key++; \
	} else { \
		h->Key = 0; \
	} \
	return ret; \
}
FPAD_REPEAT(Up)
FPAD_REPEAT(Down)

/** Devices **/

// FATFS objects for the two devices, owned by global.c. NULL == not mounted.
extern FATFS *devices[2];

// Set by the power/reset callbacks in menu.c.
extern u32 Shutdown;

static const char *const kDevName[2]      = { "sd", "usb" };
static const char *const kDevLabel[2]     = { "SD Card", "USB Storage" };

static bool DeviceMounted(int dev)
{
	return devices[dev] != NULL;
}

// Which device a stored path lives on, or -1 for the disc drive / a path with
// no recognisable prefix.
static int DeviceOfPath(const char *path)
{
	if (strncmp(path, "sd:", 3) == 0)
		return DEV_SD;
	if (strncmp(path, "usb:", 4) == 0)
		return DEV_USB;
	return -1;
}

static bool IsDiscPath(const char *path)
{
	return strcmp(path, SUSA_PATH_DISC) == 0;
}

/** Persistence **/

static bool SaveIfDirty(void)
{
	if (!IniDirty)
		return true;
	IniDirty = false;

	if (!CanSave)
	{
		// Non-fatal: the user's choices still apply to this boot.
		snprintf(ErrorLine, sizeof(ErrorLine),
			 "Settings were not saved: %s:/susamune.ini is not writable",
			 LauncherDev);
		return false;
	}
	if (SusamuneIniSave(LauncherDev) != FR_OK)
	{
		// Non-fatal: the user's choices still apply to this boot.
		snprintf(ErrorLine, sizeof(ErrorLine),
			 "Could not write %s:/susamune.ini", LauncherDev);
		CanSave = false;
		return false;
	}
	return true;
}

/** Header **/

static void DrawHeader(const char *btnHome, const char *btnA, const char *btnB)
{
	PrintInfo();
	PrintButtonActions(btnHome, btnA, btnB, NULL);
	if (!CanSave)
	{
		PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*3,
			    "Settings cannot be saved: %s: is not writable", LauncherDev);
	}
}

/* =====================================================================
 * File browser
 * ===================================================================== */

typedef struct
{
	char *name;
	u32   size;
	u8    isDir;
	u8    supported;	// selectable file; directories are always selectable
} BrowseEntry;

static int CompareEntries(const void *a, const void *b)
{
	const BrowseEntry *ea = (const BrowseEntry*)a;
	const BrowseEntry *eb = (const BrowseEntry*)b;

	// Directories first, then alphabetical.
	if (ea->isDir != eb->isDir)
		return ea->isDir ? -1 : 1;
	return strcasecmp(ea->name, eb->name);
}

static void FreeEntries(BrowseEntry *list, u32 count)
{
	u32 i;
	for (i = 0; i < count; i++)
		free(list[i].name);
	free(list);
}

// Read a directory into a freshly allocated, sorted array.
static BrowseEntry *ReadDirectory(const char *path, u32 *pCount,
				  FRESULT *pResult)
{
	BrowseEntry *list;
	DIR pdir;
	FILINFO fInfo;
	FRESULT result;
	u32 count = 0;
	char open[SUSA_PATH_MAX];
	u32 len;

	*pCount = 0;
	*pResult = FR_OK;

	// f_opendir cannot take a trailing slash: create_name() parses the empty
	// segment after it and returns FR_INVALID_NAME. Paths are carried around
	// with the slash because that is what child paths are built from, so strip
	// it here -- except at a device root ("sd:/"), which needs it.
	len = (u32)strlen(path);
	if (len >= sizeof(open))
	{
		*pResult = FR_INVALID_NAME;
		return NULL;
	}
	memcpy(open, path, len + 1);
	if (len > 1 && open[len-1] == '/' && open[len-2] != ':')
		open[len-1] = '\0';

	result = f_opendir_char(&pdir, open);
	if (result != FR_OK)
	{
		*pResult = result;
		return NULL;
	}

	list = (BrowseEntry*)malloc(sizeof(BrowseEntry) * BROWSE_MAX_ENTRIES);
	if (list == NULL)
	{
		f_closedir(&pdir);
		*pResult = FR_NOT_ENOUGH_CORE;
		return NULL;
	}

	while ((result = f_readdir(&pdir, &fInfo)) == FR_OK &&
	       fInfo.fname[0] != '\0')
	{
		// Skip "." / ".." and hidden entries, as the old game scanner did.
		const char *name = wchar_to_char(fInfo.fname);
		if (name == NULL || name[0] == '.')
			continue;

		list[count].name = strdup(name);
		if (list[count].name == NULL)
		{
			result = FR_NOT_ENOUGH_CORE;
			break;
		}
		list[count].isDir = (fInfo.fattrib & AM_DIR) ? 1 : 0;
		list[count].size = fInfo.fsize;
		list[count].supported = list[count].isDir ||
					IsSupportedFileExt(name);
		count++;

		if (count >= BROWSE_MAX_ENTRIES)
			break;
	}
	if (result == FR_OK)
		result = f_closedir(&pdir);
	else
		f_closedir(&pdir);

	if (result != FR_OK)
	{
		FreeEntries(list, count);
		*pResult = result;
		return NULL;
	}

	if (count > 1)
		qsort(list, count, sizeof(BrowseEntry), CompareEntries);

	*pCount = count;
	*pResult = FR_OK;
	return list;
}

static void FormatSize(char *buf, u32 bufSize, u32 size)
{
	if (size >= 1024u*1024u*1024u)
		snprintf(buf, bufSize, "%u.%02u GB", size >> 30,
			 (u32)(((u64)(size & 0x3FFFFFFFu) * 100) >> 30));
	else if (size >= 1024u*1024u)
		snprintf(buf, bufSize, "%u MB", size >> 20);
	else if (size >= 1024u)
		snprintf(buf, bufSize, "%u KB", size >> 10);
	else
		snprintf(buf, bufSize, "%u B", size);
}

// The device list, i.e. the root of the browser. Returns:
//   -2  cancelled
//   -1  disc drive chosen
//   0/1 device chosen
static int BrowseDevices(u8 version)
{
	// Disc drive first, then the two devices.
	static const int kRowCount = 3;
	HeldCounters held;
	int pos = 0;

	memset(&held, 0, sizeof(held));

	while (1)
	{
		int i;

		FPAD_Update();
		if (Shutdown)
			LoaderShutdown();

		if (FPAD_Start(0))
		{
			SaveIfDirty();
			ExitToLoader(0);
		}
		if (FPAD_Cancel(0))
			return -2;

		if (RepeatDown(&held))
			pos = (pos + 1) % kRowCount;
		if (RepeatUp(&held))
			pos = (pos + kRowCount - 1) % kRowCount;

		if (FPAD_OK(0))
		{
			if (pos == 0)
			{
				// The disc drive is always offered on Wii: an empty drive
				// is a runtime problem, not a configuration error. It is
				// the Wii U and Wii VC that genuinely cannot do this.
				if (!IsWiiU() && !isWiiVC)
					return -1;
			}
			else if (DeviceMounted(pos - 1))
			{
				return pos - 1;
			}
		}

		ClearScreen();
		DrawHeader("Exit", "Select", "Cancel");

		PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, MENU_POS_Y + 20*5,
			    "Select the disc image for: %s", SusaVersionName(version));
		DrawRule(MENU_POS_Y + 20*6 + 4);

		for (i = 0; i < kRowCount; i++)
		{
			const int y = MENU_POS_Y + 20*8 + i*20;
			bool usable;
			u32 color;

			if (i == 0)
				usable = (!IsWiiU() && !isWiiVC);
			else
				usable = DeviceMounted(i - 1);

			color = usable ? BLACK : DARK_GRAY;

			if (i == 0)
			{
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X + 20, y, "Disc Drive");
			}
			else
			{
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X + 20, y, "%s", kDevLabel[i-1]);
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X + 220, y,
					    "(%s:/)", kDevName[i-1]);
			}

			if (i == pos)
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X + 400, y, ARROW_LEFT);
		}

		DrawRule(MENU_POS_Y + 20*13 + 4);
		if (pos == 0)
		{
			if (!IsWiiU() && !isWiiVC)
				PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, MENU_POS_Y + 20*14 + 6,
					    "Boot from the GameCube disc in the drive. No ISO needed.");
			else
				PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*14 + 6,
					    "The disc drive cannot be used on this console.");
		}
		else if (!DeviceMounted(pos - 1))
		{
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*14 + 6,
				    "No %s device was detected.", kDevLabel[pos-1]);
		}
		else
		{
			PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, MENU_POS_Y + 20*14 + 6,
				    "Browse %s: for a disc image.", kDevName[pos-1]);
		}

		GRRLIB_Render();
	}
}

// Browse one device. Writes the chosen file's full path (with the device
// prefix) into out. Returns true if something was chosen, false to go back to
// the device list.
static bool BrowseDevice(int dev, u8 version, char *out, u32 outSize)
{
	char cwd[SUSA_PATH_MAX];
	BrowseEntry *list = NULL;
	u32 count = 0;
	FRESULT browseResult = FR_OK;
	int pos = 0, scroll = 0;
	bool reload = true;
	HeldCounters held;

	memset(&held, 0, sizeof(held));
	snprintf(cwd, sizeof(cwd), "%s:/", kDevName[dev]);

	while (1)
	{
		u32 i;
		u32 shown;

		if (reload)
		{
			if (list)
				FreeEntries(list, count);
			list = ReadDirectory(cwd, &count, &browseResult);
			pos = 0;
			scroll = 0;
			reload = false;
		}

		FPAD_Update();
		if (Shutdown)
			LoaderShutdown();

		if (FPAD_Start(0))
		{
			SaveIfDirty();
			ExitToLoader(0);
		}

		if (FPAD_Cancel(0))
		{
			// Up one level, or back to the device list from the root.
			char *slash;

			// cwd always ends in '/'; drop it before searching.
			u32 len = (u32)strlen(cwd);
			if (len > 0 && cwd[len-1] == '/')
				cwd[len-1] = '\0';
			slash = strrchr(cwd, '/');
			if (slash == NULL || slash[-1] == ':')
			{
				if (list)
					FreeEntries(list, count);
				return false;
			}
			slash[1] = '\0';
			reload = true;
			continue;
		}

		if (count > 0)
		{
			if (RepeatDown(&held))
			{
				pos++;
				if ((u32)pos >= count)
				{
					pos = 0;
					scroll = 0;
				}
				else if (pos >= scroll + BROWSE_ROWS)
				{
					scroll = pos - BROWSE_ROWS + 1;
				}
			}
			if (RepeatUp(&held))
			{
				pos--;
				if (pos < 0)
				{
					pos = (int)count - 1;
					scroll = (int)count - BROWSE_ROWS;
					if (scroll < 0)
						scroll = 0;
				}
				else if (pos < scroll)
				{
					scroll = pos;
				}
			}
			if (FPAD_Right(0))
			{
				pos += BROWSE_ROWS;
				if ((u32)pos >= count)
					pos = (int)count - 1;
				if (pos >= scroll + BROWSE_ROWS)
					scroll = pos - BROWSE_ROWS + 1;
			}
			if (FPAD_Left(0))
			{
				pos -= BROWSE_ROWS;
				if (pos < 0)
					pos = 0;
				if (pos < scroll)
					scroll = pos;
			}

			if (FPAD_OK(0))
			{
				const BrowseEntry *e = &list[pos];
				if (e->isDir)
				{
					u32 len = (u32)strlen(cwd);
					snprintf(cwd + len, sizeof(cwd) - len, "%s/", e->name);
					reload = true;
					continue;
				}
				if (e->supported)
				{
					snprintf(out, outSize, "%s%s", cwd, e->name);
					FreeEntries(list, count);
					return true;
				}
			}
		}

		ClearScreen();
		DrawHeader("Exit", "Select", "Up");

		PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, BROWSE_Y_PATH, "%s", cwd);
		DrawRule(BROWSE_Y_RULE_TOP);

		shown = count - (u32)scroll;
		if (shown > BROWSE_ROWS)
			shown = BROWSE_ROWS;

		for (i = 0; i < shown; i++)
		{
			const BrowseEntry *e = &list[scroll + i];
			const int y = BROWSE_Y_LIST + (int)i*20;
			const u32 color = e->supported ? BLACK : DARK_GRAY;

			if (e->isDir)
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X + 20, y, "[%.40s]", e->name);
			else
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X + 20, y, "%.42s", e->name);

			if (!e->isDir)
			{
				char sizeStr[16];
				FormatSize(sizeStr, sizeof(sizeStr), e->size);
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X + 460, y, "%8s", sizeStr);
			}

			if ((u32)(scroll + (int)i) == (u32)pos)
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X, y, ARROW_RIGHT);
		}

		if (browseResult != FR_OK)
		{
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X + 20, BROWSE_Y_LIST,
				    "Could not read this folder (error %u).",
				    (u32)browseResult);
		}
		else if (count == 0)
		{
			PrintFormat(DEFAULT_SIZE, DARK_GRAY, MENU_POS_X + 20, BROWSE_Y_LIST,
				    "(empty)");
		}
		else if (count > (u32)scroll + shown)
		{
			PrintCenter(DARK_GRAY, BROWSE_Y_MORE,
				    "(%u more below)", count - (u32)scroll - shown);
		}

		DrawRule(BROWSE_Y_RULE);
		if (browseResult != FR_OK)
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, BROWSE_Y_HELP,
				    "The storage device may be missing or unavailable. B: go up");
		else if (count == 0)
			PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, BROWSE_Y_HELP,
				    "B: go up");
		else if (list[pos].isDir)
			PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, BROWSE_Y_HELP,
				    "A: enter folder");
		else if (list[pos].supported)
			PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, BROWSE_Y_HELP,
				    "A: use this disc image for %s", SusaVersionName(version));
		else
			PrintFormat(DEFAULT_SIZE, DARK_GRAY, MENU_POS_X, BROWSE_Y_HELP,
				    "Not a GameCube disc image (.iso .gcm .ciso .cso)");

		GRRLIB_Render();
	}
}

// Full browse flow: device list, then that device, looping until the user
// either picks something or backs all the way out.
static void BrowseForPath(u8 version)
{
	while (1)
	{
		int dev = BrowseDevices(version);

		if (dev == -2)
			return;

		if (dev == -1)
		{
			strcpy(gIni.path[version], SUSA_PATH_DISC);
			IniDirty = true;
			ErrorLine[0] = '\0';
			return;
		}

		if (BrowseDevice(dev, version, gIni.path[version], SUSA_PATH_MAX))
		{
			IniDirty = true;
			ErrorLine[0] = '\0';
			return;
		}
	}
}

/* =====================================================================
 * Settings
 * ===================================================================== */

enum
{
	SET_AUTOBOOT = 0,
	SET_NATIVE_CONTROLS,
	SET_UNLOCK_READ_SPEED,
	SET_CHEATS,
	SET_FORCE_PROGRESSIVE,
	SET_DISABLE_RUMBLE,
	SET_LANGUAGE,

	SET_COUNT
};

static const char *const kSettingNames[SET_COUNT] =
{
	"Auto Boot",
	"Native Controls",
	"Unlock Read Speed",
	"Cheats",
	"Force Progressive",
	"Disable Rumble",
	"Language",
};

static const char *const kLanguageNames[NIN_LAN_LAST + 1] =
{
	"Eng", "Ger", "Fre", "Spa", "Ita", "Dut", "Auto"
};

// Help text per setting, NULL terminated. Carried over from the Nintendont
// settings menu, rewrapped for the full screen width.
static const char *const kHelpAutoBoot[] =
{
	"Skip this menu and boot the selected version straight",
	"away.",
	"",
	"Hold B while the launcher starts to get the menu back.",
	"",
	"Auto Boot also skips scanning for storage devices, so it",
	"is noticeably faster when only the SD card is in use.",
	NULL
};
static const char *const kHelpNativeControls[] =
{
	"Native Control enables GBA link support on original Wii",
	"systems.",
	"",
	"NOTE: Enabling Native Control will disable Bluetooth and",
	"USB controllers.",
	"",
	"Wii U and Wii Family Edition have no native controller",
	"ports, so this option is unavailable there.",
	NULL
};
static const char *const kHelpReadSpeed[] =
{
	"Disc read speed emulation is an attempt to reproduce the",
	"original GameCube disc drive speed, but it makes loading",
	"times much slower.",
	"",
	"Unlocking read speed is recommended for any game.",
	NULL
};
static const char *const kHelpCheats[] =
{
	"Apply Gecko codes from a .gct file.",
	"",
	"Codes are loaded from /codes/<GAMEID>.gct on the game's",
	"device. This is independent of the mod's own features.",
	NULL
};
static const char *const kHelpForceProgressive[] =
{
	"Force games to render in 480p.",
	"",
	"For PAL Super Mario Sunshine this also patches the game's",
	"progressive-mode check using the method from Swiss.",
	"",
	"Requires component video or a compatible digital adapter.",
	NULL
};
static const char *const kHelpDisableRumble[] =
{
	"Keep Super Mario Sunshine's controller rumble disabled",
	"automatically on every launch.",
	"",
	"This changes only the motor output. Controller input and",
	"Native Control support are unaffected.",
	NULL
};
static const char *const kHelpLanguage[] =
{
	"Set the system language.",
	"",
	"This option is normally only found on PAL GameCubes, so it",
	"usually has no effect on NTSC games.",
	"",
	"Auto follows the Wii's own language setting.",
	NULL
};

static const char *const *const kSettingHelp[SET_COUNT] =
{
	kHelpAutoBoot,
	kHelpNativeControls,
	kHelpReadSpeed,
	kHelpCheats,
	kHelpForceProgressive,
	kHelpDisableRumble,
	kHelpLanguage,
};

static const char *SettingValue(int setting, char *buf, u32 bufSize)
{
	switch (setting)
	{
		case SET_AUTOBOOT:
			return gIni.autoboot ? "On" : "Off";
		case SET_NATIVE_CONTROLS:
			return gIni.nativeControls ? "On" : "Off";
		case SET_UNLOCK_READ_SPEED:
			return gIni.unlockReadSpeed ? "On" : "Off";
		case SET_CHEATS:
			return gIni.enableCheats ? "On" : "Off";
		case SET_FORCE_PROGRESSIVE:
			return gIni.forceProgressive ? "On" : "Off";
		case SET_DISABLE_RUMBLE:
			return gIni.disableRumble ? "On" : "Off";
		case SET_LANGUAGE:
		{
			s32 lan = gIni.language;
			if (lan < 0 || lan >= NIN_LAN_LAST)
				lan = NIN_LAN_LAST;	// Auto
			snprintf(buf, bufSize, "%s", kLanguageNames[lan]);
			return buf;
		}
		default:
			return "";
	}
}

// Native Control is a Wii-only option; the kernel ignores it on Wii U.
static bool SettingUsable(int setting)
{
	if (setting == SET_NATIVE_CONTROLS)
		return !IsWiiU();
	return true;
}

static void CycleSetting(int setting)
{
	switch (setting)
	{
		case SET_AUTOBOOT:
			gIni.autoboot = !gIni.autoboot;
			break;
		case SET_NATIVE_CONTROLS:
			gIni.nativeControls = !gIni.nativeControls;
			break;
		case SET_UNLOCK_READ_SPEED:
			gIni.unlockReadSpeed = !gIni.unlockReadSpeed;
			break;
		case SET_CHEATS:
			gIni.enableCheats = !gIni.enableCheats;
			break;
		case SET_FORCE_PROGRESSIVE:
			gIni.forceProgressive = !gIni.forceProgressive;
			break;
		case SET_DISABLE_RUMBLE:
			gIni.disableRumble = !gIni.disableRumble;
			break;
		case SET_LANGUAGE:
			// Auto -> Eng -> ... -> Dut -> Auto
			if (gIni.language < 0 || gIni.language >= NIN_LAN_LAST - 1)
				gIni.language = (gIni.language < 0) ? NIN_LAN_FIRST : NIN_LAN_AUTO;
			else
				gIni.language++;
			break;
		default:
			return;
	}
	IniDirty = true;
}

static void SettingsScreen(void)
{
	HeldCounters held;
	int pos = 0;

	memset(&held, 0, sizeof(held));
	while (!SettingUsable(pos))
		pos++;

	while (1)
	{
		int i;
		const char *const *help;

		FPAD_Update();
		if (Shutdown)
			LoaderShutdown();

		if (FPAD_Start(0))
		{
			SaveIfDirty();
			ExitToLoader(0);
		}

		if (FPAD_Cancel(0))
		{
			// Same convention as the in-game menu: write on close.
			SaveIfDirty();
			return;
		}

		if (RepeatDown(&held))
		{
			do {
				pos = (pos + 1) % SET_COUNT;
			} while (!SettingUsable(pos));
		}
		if (RepeatUp(&held))
		{
			do {
				pos = (pos + SET_COUNT - 1) % SET_COUNT;
			} while (!SettingUsable(pos));
		}
		if (FPAD_OK(0) && SettingUsable(pos))
			CycleSetting(pos);

		ClearScreen();
		DrawHeader("Exit", "Change", "Back");

		for (i = 0; i < SET_COUNT; i++)
		{
			const int y = MENU_POS_Y + 20*6 + i*20;
			const u32 color = SettingUsable(i) ? BLACK : DARK_GRAY;
			char valBuf[16];
			const char *value = SettingValue(i, valBuf, sizeof(valBuf));
			char leader[64];
			int nameLen = (int)strlen(kSettingNames[i]);
			int valLen = (int)strlen(value);
			// Dot leader between the name and the right-justified value.
			int dots = 50 - nameLen - valLen;
			int d;

			if (dots < 2)
				dots = 2;
			if (dots > (int)sizeof(leader) - 1)
				dots = (int)sizeof(leader) - 1;
			for (d = 0; d < dots; d++)
				leader[d] = '.';
			leader[dots] = '\0';

			PrintFormat(DEFAULT_SIZE, color, MENU_POS_X + 20, y,
				    "%s %s %s", kSettingNames[i], leader, value);

			if (i == pos)
				PrintFormat(DEFAULT_SIZE, color, MENU_POS_X, y, ARROW_RIGHT);
		}

		DrawRule(MENU_POS_Y + 20*(6 + SET_COUNT));

		help = kSettingHelp[pos];
		for (i = 0; help[i] != NULL; i++)
		{
			if (help[i][0] == '\0')
				continue;
			PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X,
				    MENU_POS_Y + 20*(7 + SET_COUNT) + i*20, "%s", help[i]);
		}
		GRRLIB_Render();
	}
}

/* =====================================================================
 * Launch validation
 * ===================================================================== */

bool SusamuneCheckGameID(u32 gameID)
{
	return gameID == SusaVersionGameID(gIni.version);
}

const char *SusamuneSelectedVersionName(void)
{
	return SusaVersionName(gIni.version);
}

// Read the disc header of a disc image and hand back its game ID. Handles the
// CISO case the same way the old game scanner did: the GameCube header lives
// at 0x8000 rather than 0.
static bool ReadImageGameID(const char *path, u32 *pGameID)
{
	static const u8 CISO_MAGIC[8] = {'C','I','S','O',0x00,0x00,0x20,0x00};
	u8 buf[0x100];
	FIL in;
	UINT read;

	if (f_open_char(&in, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;

	if (f_read(&in, buf, sizeof(buf), &read) != FR_OK || read != sizeof(buf))
	{
		f_close(&in);
		return false;
	}

	if (!memcmp(buf, CISO_MAGIC, sizeof(CISO_MAGIC)) && !IsGCGame(buf))
	{
		f_lseek(&in, 0x8000);
		if (f_read(&in, buf, sizeof(buf), &read) != FR_OK || read != sizeof(buf))
		{
			f_close(&in);
			return false;
		}
	}
	f_close(&in);

	if (!IsGCGame(buf))
		return false;

	memcpy(pGameID, buf, 4);
	return true;
}

// Everything that can be checked before the kernel is handed the disc. The
// disc drive is checked later, in main.c, once DI has read the header.
// Returns true if the game may be launched; otherwise fills ErrorLine.
static bool ValidateSelection(void)
{
	const char *path = gIni.path[gIni.version];
	int dev;
	u32 gameID = 0;

	if (path[0] == '\0')
	{
		// Handled by the caller, which blinks the sentinel instead.
		return false;
	}

	if (IsDiscPath(path))
		return true;

	dev = DeviceOfPath(path);
	if (dev < 0)
	{
		snprintf(ErrorLine, sizeof(ErrorLine),
			 "Path has no device prefix: %s", path);
		return false;
	}
	if (!DeviceMounted(dev))
	{
		snprintf(ErrorLine, sizeof(ErrorLine),
			 "%s is not available", kDevLabel[dev]);
		return false;
	}

	if (!ReadImageGameID(path, &gameID))
	{
		snprintf(ErrorLine, sizeof(ErrorLine), "Not found or not a GC disc image:");
		return false;
	}

	if (!SusamuneCheckGameID(gameID))
	{
		snprintf(ErrorLine, sizeof(ErrorLine),
			 "That image is %.4s, but %s is selected",
			 (const char*)&gameID, SusaVersionName(gIni.version));
		return false;
	}

	return true;
}

// Fill in NIN_CFG for the kernel. This is the only place the launcher's
// settings become NIN_CFG bits: the ini is where they live, this is transport.
static void ApplyToNinCFG(void)
{
	const char *path = gIni.path[gIni.version];
	int gameDev;

	if (IsDiscPath(path))
	{
		// RealDI. The storage device is still needed for cheats and the ini,
		// so leave it pointing at the launcher's own device.
		strcpy(ncfg->GamePath, SUSA_PATH_DISC);
		gameDev = (strcmp(LauncherDev, "usb") == 0) ? DEV_USB : DEV_SD;
	}
	else
	{
		gameDev = DeviceOfPath(path);
		if (gameDev < 0)
			gameDev = DEV_SD;
		// NIN_CFG::GamePath is device-relative; the prefix is carried by
		// NIN_CFG_USB instead.
		strncpy(ncfg->GamePath, strchr(path, ':') + 1, sizeof(ncfg->GamePath));
		ncfg->GamePath[sizeof(ncfg->GamePath)-1] = '\0';
	}

	if (gameDev == DEV_USB)
		ncfg->Config |= NIN_CFG_USB;
	else
		ncfg->Config &= ~NIN_CFG_USB;

	// Which device susamune.ini lives on. When this disagrees with
	// NIN_CFG_USB the kernel mounts it as a second volume rather than making
	// the user keep one ini per device.
	if (strcmp(LauncherDev, "usb") == 0)
		ncfg->Config |= NIN_CFG_CFG_ON_USB;
	else
		ncfg->Config &= ~NIN_CFG_CFG_ON_USB;

	if (gIni.nativeControls)
		ncfg->Config |= NIN_CFG_NATIVE_SI;
	else
		ncfg->Config &= ~NIN_CFG_NATIVE_SI;

	if (gIni.unlockReadSpeed)
		ncfg->Config |= NIN_CFG_REMLIMIT;
	else
		ncfg->Config &= ~NIN_CFG_REMLIMIT;

	if (gIni.enableCheats)
		ncfg->Config |= NIN_CFG_CHEATS;
	else
		ncfg->Config &= ~NIN_CFG_CHEATS;

	if (gIni.forceProgressive)
		ncfg->Config |= NIN_CFG_FORCE_PROG;
	else
		ncfg->Config &= ~NIN_CFG_FORCE_PROG;

	if (gIni.disableRumble)
		ncfg->Config |= NIN_CFG_DISABLE_RUMBLE;
	else
		ncfg->Config &= ~NIN_CFG_DISABLE_RUMBLE;

	ncfg->Language = gIni.language;

	ncfg->GameID = SusaVersionGameID(gIni.version);
	UseSD = (ncfg->Config & NIN_CFG_USB) == 0;

	DCFlushRange((void*)ncfg, sizeof(NIN_CFG));
}

int SusamuneAutoBootDevice(void)
{
	const char *path = gIni.path[gIni.version];

	if (path[0] == '\0' || IsDiscPath(path))
		return -1;
	return DeviceOfPath(path);
}

bool SusamuneAutoBoot(const char *launcherDev)
{
	LauncherDev = launcherDev;

	// Leaves ErrorLine set on failure, which SusamuneMenuRun then shows: an
	// autoboot that cannot find its game falls through to the menu rather
	// than stranding the user on an error screen with nothing to do.
	if (gIni.path[gIni.version][0] == '\0')
	{
		snprintf(ErrorLine, sizeof(ErrorLine),
			 "Auto Boot: no path configured for %s",
			 SusaVersionName(gIni.version));
		return false;
	}
	if (!ValidateSelection())
		return false;

	ApplyToNinCFG();
	return true;
}

/* =====================================================================
 * Main menu
 * ===================================================================== */

static void DrawMainMenu(int pos)
{
	const char *path = gIni.path[gIni.version];
	char shortBuf[PATH_DISPLAY_MAX + 8];
	bool pathSet = (path[0] != '\0');

	DrawHeader("Exit", "Select", NULL);

	PrintCenter(BLACK, MAIN_Y_LAUNCH, "Launch Game%s",
		    pos == ROW_LAUNCH ? " " ARROW_LEFT : "");

	PrintCenter(BLACK, MAIN_Y_VERSION, "Version: %s%s",
		    SusaVersionName(gIni.version),
		    pos == ROW_VERSION ? " " ARROW_LEFT : "");

	if (pathSet)
	{
		const char *shown = IsDiscPath(path)
			? "Disc Drive"
			: ShortenPath(path, shortBuf, sizeof(shortBuf), PATH_DISPLAY_MAX);
		PrintCenter(BLACK, MAIN_Y_PATH, "Path: %s%s", shown,
			    pos == ROW_PATH ? " " ARROW_LEFT : "");
	}
	else if (BlinkFrames == 0 || ((BlinkFrames / BLINK_HALF_PERIOD) & 1) == 0)
	{
		// While blinking, the dark half of the cycle draws nothing at all.
		PrintCenter(MAROON, MAIN_Y_PATH, "Path: %s%s", kPathUnset,
			    pos == ROW_PATH ? " " ARROW_LEFT : "");
	}

	PrintCenter(BLACK, MAIN_Y_SETTINGS, "Settings%s",
		    pos == ROW_SETTINGS ? " " ARROW_LEFT : "");

	if (ErrorLine[0] != '\0')
	{
		PrintCenter(MAROON, MAIN_Y_ERROR, "%s", ErrorLine);
		if (!IsDiscPath(path) && pathSet)
		{
			char buf[PATH_DISPLAY_MAX + 8];
			PrintCenter(MAROON, MAIN_Y_ERROR + 20, "%s",
				    ShortenPath(path, buf, sizeof(buf), PATH_DISPLAY_MAX));
		}
	}
}

void SusamuneMenuRun(const char *launcherDev, bool canSave)
{
	HeldCounters held;
	int pos = ROW_LAUNCH;

	LauncherDev = launcherDev;
	CanSave = canSave;
	memset(&held, 0, sizeof(held));
	// ErrorLine is deliberately left alone: a failed auto boot put its reason
	// there, and that is the first thing the user needs to see.

	while (1)
	{
		FPAD_Update();
		if (Shutdown)
			LoaderShutdown();

		if (FPAD_Start(0))
		{
			SaveIfDirty();
			ShowMessageScreenAndExit("Returning to loader...", 0);
		}

		if (RepeatDown(&held))
		{
			pos = (pos + 1) % ROW_COUNT;
			ErrorLine[0] = '\0';
			BlinkFrames = 0;
		}
		if (RepeatUp(&held))
		{
			pos = (pos + ROW_COUNT - 1) % ROW_COUNT;
			ErrorLine[0] = '\0';
			BlinkFrames = 0;
		}

		if (FPAD_OK(0))
		{
			switch (pos)
			{
				case ROW_LAUNCH:
					if (gIni.path[gIni.version][0] == '\0')
					{
						// Nothing to boot: blink the sentinel rather
						// than showing a message that says the same
						// thing the red text already does.
						BlinkFrames = BLINK_FRAMES;
					}
					else if (ValidateSelection())
					{
						if (!SaveIfDirty())
							break;
						ApplyToNinCFG();
						return;
					}
					break;

				case ROW_VERSION:
					gIni.version = (u8)((gIni.version + 1) % SUSA_VER_COUNT);
					IniDirty = true;
					ErrorLine[0] = '\0';
					BlinkFrames = 0;
					break;

				case ROW_PATH:
					BrowseForPath(gIni.version);
					SaveIfDirty();
					BlinkFrames = 0;
					memset(&held, 0, sizeof(held));
					break;

				case ROW_SETTINGS:
					SettingsScreen();
					memset(&held, 0, sizeof(held));
					break;

				default:
					break;
			}
		}

		if (BlinkFrames > 0)
			BlinkFrames--;

		ClearScreen();
		DrawMainMenu(pos);
		GRRLIB_Render();
	}
}
