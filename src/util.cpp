#include "susamune/util.hxx"

namespace Util {

int appendString(char *out, const char *value) {
    int count = 0;
    while (value[count]) {
        out[count] = value[count];
        count++;
    }
    return count;
}

void copyString(char *out, u32 outSize, const char *value) {
    if (outSize == 0) {
        return;
    }
    u32 count = 0;
    while (value[count] && count < outSize - 1) {
        out[count] = value[count];
        count++;
    }
    out[count] = '\0';
}

int formatUInt(char *out, u32 value) {
    char reversed[10];
    int  count = 0;
    do {
        reversed[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    for (int i = 0; i < count; i++) {
        out[i] = reversed[count - i - 1];
    }
    return count;
}

}  // namespace Util
