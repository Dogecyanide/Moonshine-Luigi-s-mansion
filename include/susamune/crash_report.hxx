#pragma once

#include <Dolphin/types.h>
#include "susamune/crash_report.h"

namespace CrashReport {

void init();
void note(u32 event, u32 arg0 = 0, u32 arg1 = 0);
void observeContext(u32 context);

}  // namespace CrashReport
