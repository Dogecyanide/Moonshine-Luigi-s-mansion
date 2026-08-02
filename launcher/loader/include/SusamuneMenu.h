#ifndef __SUSAMUNE_MENU_H__
#define __SUSAMUNE_MENU_H__

#include <gctypes.h>

/**
 * Run the susamune launcher menu. Returns once the user has launched a game,
 * with ncfg filled in (game path, storage device, and the four Nintendont
 * options that live in susamune.ini). Does not return otherwise -- Home exits
 * to the loader from inside.
 *
 * @param launcherDev "sd" or "usb": the device the launcher was run from, and
 *                    therefore the one susamune.ini lives on.
 * @param canSave     False if that device could not be written to; the menu
 *                    says so and still lets the user launch.
 */
void SusamuneMenuRun(const char *launcherDev, bool canSave);

/**
 * Which device the configured path for the selected version lives on, so the
 * caller can mount just that one. -1 when nothing needs mounting: the disc
 * drive, or no path configured.
 */
int SusamuneAutoBootDevice(void);

/**
 * Validate the configured path and fill in ncfg without showing the menu.
 * @return True if the game can be booted. On false the reason is remembered
 *         and shown by the next SusamuneMenuRun(), so a failed auto boot lands
 *         in the menu with an explanation rather than on a dead end.
 */
bool SusamuneAutoBoot(const char *launcherDev);

/**
 * Check a disc header's game ID against the version the user selected.
 * Called for the disc drive, whose ID is only known after DI is up.
 * @return True if it matches.
 */
bool SusamuneCheckGameID(u32 gameID);

/**
 * Name of the version the user selected, for error messages ("JP"/"US"/"PAL").
 */
const char *SusamuneSelectedVersionName(void);

#endif
