#ifndef __SUSAMUNE_GHOST_DIRS_H__
#define __SUSAMUNE_GHOST_DIRS_H__

#include <gctypes.h>

// Prepare the fixed libraries plus the one user-facing import directory while
// the launcher owns the device.
bool SusamuneGhostEnsureDirectories(const char *device);

#endif
