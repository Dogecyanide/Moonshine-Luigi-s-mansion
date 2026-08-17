#ifndef SUSAMUNE_THEME_H
#define SUSAMUNE_THEME_H

#include <gccore.h>
#include <stdbool.h>

#include "grrlib.h"

#define SUSAMUNE_THEME_BACKGROUND_MAX_SIZE (2u * 1024u * 1024u)

bool SusamuneThemeLoad(const char *launcherDevice, const char *launchDir,
	GRRLIB_texImg **backgroundPtr);
const char *SusamuneThemeWarning(void);
bool SusamuneThemeDrawBackground(u8 alpha, f32 xScale, int xPos);
void SusamuneThemeShutdown(GRRLIB_texImg **backgroundPtr);

#endif
