#ifndef __SUSAMUNE_CFG_H__
#define __SUSAMUNE_CFG_H__

#include "global.h"

// Susamune settings persistence, launcher side. See susamune_cfg.h (shared
// with the mod) for the MEM2 block layout and the save protocol.

// Parse this disc's sections of /susamune.ini into the MEM2 handoff block. Call
// once at boot, after the FAT device is mounted and DIinit has published
// GAME_ID (the sections are per game version), and before the game is patched:
// the mod reads the block from TApplication::initialize, i.e. before the first
// frame, so every feature has its setting live for the whole boot sequence.
void SusamuneCfgInit(void);

// True when the mod has rung the save doorbell. Cheap: reads one cache line.
bool SusamuneCfgPending(void);

// Rewrite /susamune.ini from the block and acknowledge the request. Only call
// from the main loop when no async disc read is in flight (same constraint as
// GCNCard_Save) -- FatFS is not reentrant against the DI thread.
void SusamuneCfgService(void);

// Binary-journal service slot for ILing PBs, global progress, and Stage Loader
// playlists. Keeping these separate avoids regenerating susamune.ini; PBs
// retain priority when several payloads are pending.
bool SusamunePbPending(void);
void SusamunePbService(void);

// Path to open susamune.ini by. The ini lives on the device the launcher was
// run from, which is drive 1 when that is not the device the game is read from
// and drive 0 (i.e. the unprefixed path) when it is. Defined in main.c, next to
// the mount that decides it.
const char *SusamuneCfgIniPath(void);

// Prefix for other Susamune files on the same device: "1:" when the
// launcher's device is mounted as drive 1, otherwise the empty string.
const char *SusamuneCfgStoragePrefix(void);

// False when the launcher and game use different devices and the launcher's
// device could not be mounted. In that case persistence must stay disabled;
// falling back to drive 0 would read and write the wrong files.
bool SusamuneCfgStorageAvailable(void);

#endif
