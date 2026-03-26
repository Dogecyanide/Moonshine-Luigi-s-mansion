#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRDisposer.hxx>
#include <JSystem/JKernel/JKRFileFinder.hxx>
#include <JSystem/JSupport/JSUList.hxx>

class JKRArchive;

class JKRFileLoader : public JKRDisposer {
public:
    JKRFileLoader();
    virtual ~JKRFileLoader();

    virtual void unmount();

    static JKRArchive *getVolume(const char *);
    static void changeDirectory(const char *dirName);
    static void *getGlbResource(const char *);
    static void *getGlbResource(const char *, JKRFileLoader *);
    static u32 getResSize(void *resource, JKRFileLoader *);
    static void *findVolume(const char **);
    static JKRArcFinder *findFirstFile(const char *);
    static u8 *fetchVolumeName(u8 *, u32, const char *);

    JSUPtrLink mPtrLink2;  // _18
    u32 _28;
    u32 _2C;
    u8 _30;
    u8 _31;  // padding?
    u8 _32;  // ^^
    u8 _33;  // ^^
    u32 _34;
};