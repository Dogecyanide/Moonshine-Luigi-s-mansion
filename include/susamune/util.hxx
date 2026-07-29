#pragma once

#include <Dolphin/types.h>

namespace Util {

int  appendString(char *out, const char *value);
void copyString(char *out, u32 outSize, const char *value);
int  formatUInt(char *out, u32 value);

}  // namespace Util
