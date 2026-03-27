#ifndef THP_H
#define THP_H

#include <Dolphin/types.h>
#include <Dolphin/GX_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VideoInfo {
    u32 mHeight;
    u32 mWidth;
} VideoInfo;

bool THPPlayerInit(u32);
bool THPPlayerOpen(u8 *, u32);
bool THPPlayerClose();
u32 THPPlayerCalcNeedMemory();
bool THPPlayerSetBuffer(u8 *);
bool THPPlayerPlay();
bool THPPlayerPrepare(u32, u32, u32);
void THPPlayerStop();
bool THPPlayerPause();
void THPPlayerDrawDone();
u32 THPPlayerDrawCurrentFrame(GXRenderModeObj *, u32, u32, u32, u32);
bool THPPlayerPrepare(u32, u32, u32);
void THPPlayerQuit();
bool THPPlayerGetVideoInfo(VideoInfo *);

#ifdef __cplusplus
}
#endif

#endif