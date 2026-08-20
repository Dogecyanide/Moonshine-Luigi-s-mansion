#ifndef SUSAMUNE_MUSIC_H
#define SUSAMUNE_MUSIC_H

#include <stdbool.h>

#define SUSAMUNE_THEME_BGM_MAX_SIZE (4u * 1024u * 1024u)

void SusamuneMusicInit(void);
bool SusamuneMusicLoad(const char *launcherDevice, const char *launchDir);
bool SusamuneMusicStart(void);
void SusamuneMusicService(void);
const char *SusamuneMusicWarning(void);
void SusamuneMusicShutdown(void);

#endif
