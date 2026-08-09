/*

Susamune launcher settings, loader side.

The [nintendont] section of susamune.ini holds everything the launcher GUI
edits: which game version to boot, where each version's disc image lives, and
the handful of Nintendont options that used to live in nincfg.bin. nincfg.bin
is gone -- the kernel takes NIN_CFG through the MEM2 handoff, so the file was
only ever loader-side persistence, and keeping it would have meant two files
disagreeing about the same options.

Reads and writes go to the device the launcher was run from, never the game's
device. The kernel mounts that device as drive 1 when the two differ, so there
is exactly one susamune.ini no matter where the ISO lives.

The other sections belong to the mod ([settings_<region>], [binds_<region>])
and are copied through untouched, which is the mirror image of what the kernel
does to this section in SusamuneCfg.c.

*/
#ifndef __SUSAMUNE_INI_H__
#define __SUSAMUNE_INI_H__

#include <gctypes.h>

// Game versions, in the order the GUI cycles them.
typedef enum
{
	SUSA_VER_JP = 0,
	SUSA_VER_US,
	SUSA_VER_PAL,

	SUSA_VER_COUNT
} SusaVersion;

// Longest path we will store, including the "sd:/" prefix. NIN_CFG::GamePath
// is 255 bytes and holds the device-relative tail, so this is the tighter of
// the two limits and the one the file browser enforces.
#define SUSA_PATH_MAX 250

// The disc drive, stored in place of a path. Matches what NIN_CFG::GamePath
// wants for RealDI, so it is written through verbatim.
#define SUSA_PATH_DISC "di"

typedef struct
{
	u8   version;                        // SusaVersion
	char path[SUSA_VER_COUNT][SUSA_PATH_MAX];  // "" == not configured

	u8   autoboot;
	u8   nativeControls;
	u8   unlockReadSpeed;
	u8   enableCheats;
	u8   forceProgressive;
	u8   disableRumble;
	s32  language;                       // NIN_LAN_*, NIN_LAN_AUTO for auto
} SusamuneIni;

extern SusamuneIni gIni;

// Region tag as it appears in the key name ("jp"/"us"/"pal").
const char *SusaVersionTag(u8 version);
// Display name for the GUI ("JP"/"US"/"PAL").
const char *SusaVersionName(u8 version);
// GameID the disc header must report for this version, e.g. 'GMSE'.
u32 SusaVersionGameID(u8 version);

/**
 * Load the [nintendont] section into gIni. Missing file, missing section and
 * missing keys all leave the compiled-in defaults in place, which is also what
 * a first run sees.
 * @param device "sd" or "usb" -- the device the launcher was run from.
 */
void SusamuneIniLoad(const char *device);

/**
 * Rewrite [nintendont] from gIni, copying every other section through
 * verbatim so the mod's settings and binds survive.
 * @param device "sd" or "usb" -- the device the launcher was run from.
 * @return FRESULT; FR_OK on success.
 */
int SusamuneIniSave(const char *device);

/**
 * True if the file has no [nintendont] section yet, i.e. the compiled-in
 * defaults are in force and should be written out so the user can find them.
 * Only meaningful after SusamuneIniLoad().
 */
bool SusamuneIniNeedsWrite(void);

/**
 * Can susamune.ini be written on this device? Probed once at startup so the
 * menu can say up front that nothing will persist, instead of only finding out
 * when the user changes something.
 */
bool SusamuneIniWritable(const char *device);

#endif
