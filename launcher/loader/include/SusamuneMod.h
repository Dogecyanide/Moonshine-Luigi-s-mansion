#ifndef SUSAMUNE_MOD_LOADER_H
#define SUSAMUNE_MOD_LOADER_H

#include <gctypes.h>

/* Stage mod_<tag>.bin for the detected disc into the MEM2 window the kernel
 * injects from (see mod_bin.h). Always leaves a valid-or-blank header behind,
 * so it must be called on every boot -- including boots of games we have no mod
 * for. Requires the FAT devices to still be mounted, i.e. before CloseDevices().
 */
void SusamuneLoadMod(u32 gameID);

#endif
