#include "Dolphin/CARD.h"
#include "susamune/addresses.hxx"

namespace {

typedef void (*CardSyncCallback)(s32, s32);
typedef s32 (*CardSync)(s32);

s32 mountCard(s32 channel, void *workArea, CARDCallback detachCallback) {
    CardSyncCallback callback = reinterpret_cast<CardSyncCallback>(
        SUSAMUNE_ADDR_CARD_SYNC_CALLBACK);
    const s32 result = CARDMountAsync(channel, workArea, detachCallback,
                                      callback);
    if (result < 0) return result;

    CardSync sync = reinterpret_cast<CardSync>(SUSAMUNE_ADDR_CARD_SYNC);
    return sync(channel);
}

}  // namespace

extern "C" s32 susamuneCardMount(s32 channel, void *workArea,
                                  CARDCallback detachCallback) {
    s32 result = mountCard(channel, workArea, detachCallback);
    if (result != CARD_ERROR_ENCODING) return result;

    volatile u16 *encoding = reinterpret_cast<volatile u16 *>(
        SUSAMUNE_ADDR_FONT_ENCODING);
    const u16 original = *encoding;
    if (original > 1) return result;

    CARDUnmount(channel);
    *encoding = original ^ 1;
    result = mountCard(channel, workArea, detachCallback);
    if (result == CARD_ERROR_ENCODING) *encoding = original;
    return result;
}
