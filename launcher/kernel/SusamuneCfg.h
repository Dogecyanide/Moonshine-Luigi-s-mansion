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

#endif
