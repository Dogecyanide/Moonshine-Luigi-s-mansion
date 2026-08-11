#include "susamune/packed_text.hxx"

namespace PackedText {

const char *at(const char *pool, int index) {
    while (index-- > 0) {
        while (*pool++) {}
    }
    return pool;
}

}  // namespace PackedText
