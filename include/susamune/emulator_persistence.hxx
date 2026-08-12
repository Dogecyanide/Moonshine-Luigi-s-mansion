#ifndef _SUSAMUNE_EMULATOR_PERSISTENCE_HXX
#define _SUSAMUNE_EMULATOR_PERSISTENCE_HXX

#include <Dolphin/types.h>

#include "susamune/susamune_cfg.h"

namespace EmulatorPersistence {

enum InitResult {
    INIT_WAITING,
    INIT_READY,
    INIT_UNAVAILABLE,
};

enum SaveResult {
    SAVE_PENDING,
    SAVE_OK,
    SAVE_ERROR,
};

// Called from onUpdate after Sunshine has constructed its slot-A manager.
InitResult init();

// The returned configuration stays locked until unlock() or commit().
SusamuneCfg *lock();
void unlock();
u32 commit();

SaveResult poll(u32 ticket, u32 *error);
bool needsInitialSave();
u32 initError();

}  // namespace EmulatorPersistence

#endif  // _SUSAMUNE_EMULATOR_PERSISTENCE_HXX
