#ifndef _SUSAMUNE_RECORDS_PERSISTENCE_HXX
#define _SUSAMUNE_RECORDS_PERSISTENCE_HXX

#include <Dolphin/types.h>

#include "susamune/records.hxx"

namespace RecordsPersistence {

enum Scope : u8 {
    SCOPE_GLOBAL,
    SCOPE_JP,
    SCOPE_US,
    SCOPE_PAL,
    SCOPE_COUNT,
};

// Console builds adopt the region-neutral launcher journal. Dolphin keeps the
// same UI/runtime surface but is session-only in this first alpha.
void init();
void update();
void checkpoint();
void resetAll();

u32 stat(Scope scope, Records::StatId id);
const char *scopeName(Scope scope);
Scope currentRegionScope();

bool persistent();
bool writable();
bool pending();
bool dirty();
u32 lastError();

}  // namespace RecordsPersistence

#endif  // _SUSAMUNE_RECORDS_PERSISTENCE_HXX
