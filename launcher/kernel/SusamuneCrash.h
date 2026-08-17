#ifndef __SUSAMUNE_CRASH_H__
#define __SUSAMUNE_CRASH_H__

#include "global.h"

void SusamuneCrashInit(void);
bool SusamuneCrashPending(void);
void SusamuneCrashService(void);

#endif
