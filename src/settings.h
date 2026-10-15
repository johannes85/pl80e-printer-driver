#ifndef CLEAN_SETTINGS_H
#define CLEAN_SETTINGS_H
#include <windows.h>
#include <stdint.h>
#define CLEAN_SETTINGS_MAGIC 0x43535044u
typedef struct { DWORD magic, speed, density, media, gap; } CleanPrivate;
typedef struct { DEVMODEW publicMode; CleanPrivate settings; } CleanDevMode;
static inline const CleanPrivate *clean_private(const DEVMODEW *dm) {
    if (!dm || dm->dmSize != sizeof(DEVMODEW) ||
        (dm->dmDriverExtra != sizeof(CleanPrivate) && dm->dmDriverExtra != 12 && dm->dmDriverExtra != 8)) return NULL;
    const CleanPrivate *p=(const CleanPrivate *)((const BYTE *)dm+dm->dmSize);
    if(p->magic!=CLEAN_SETTINGS_MAGIC || p->speed<2 || p->speed>6) return NULL;
    if(dm->dmDriverExtra>=12 && (p->density<1 || p->density>15)) return NULL;
    if(dm->dmDriverExtra==sizeof(CleanPrivate) && (p->media>2 || p->gap>100)) return NULL;
    return p;
}
#endif
