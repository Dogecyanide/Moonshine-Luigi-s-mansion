#ifndef _SUSAMUNE_GHOST_STORAGE_HXX
#define _SUSAMUNE_GHOST_STORAGE_HXX

#include <Dolphin/types.h>
#include "susamune/ghost_storage.h"

namespace GhostStorage {

void init();
void update();
void onSavestateLoaded();

bool refresh();
bool save(int slot);
bool save(int slot, u32 expectedSelectionToken);
bool load(int slot);
bool loadObserver(int slot, bool secondary);
bool remove(int slot);
bool exportShare(int slot);
bool importShare(int slot);
bool refreshImported();
bool scanImports();
bool loadImported(int slot);
bool loadImportedObserver(int slot, bool secondary);
bool removeImported(int slot);

bool busy();
bool available();
bool catalogReady();
bool importedCatalogReady();
int profile();
int loadedSlot();
bool loadedImported();
int loadedImportedSlot();
u32 totalDurationQf();
u32 importedTotalDurationQf();
u32 importedOverflowCount();
const char *statusText();
bool copySlotName(int slot, char *out, u32 size);
bool copyImportedSlotName(int slot, char *out, u32 size);
const SusamuneGhostSlotInfo *slot(int slot);
const SusamuneGhostSlotInfo *importedSlot(int slot);

}  // namespace GhostStorage

#endif  // _SUSAMUNE_GHOST_STORAGE_HXX
