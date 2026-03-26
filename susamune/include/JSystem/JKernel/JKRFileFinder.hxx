#pragma once

#include <Dolphin/types.h>

class JKRArchive;

class JKRFileFinder {
public:
    u32 _0;
    u32 _4;
    u16 _8;
    u16 _A;

    virtual ~JKRFileFinder();
};

class JKRArcFinder : public JKRFileFinder {
public:
    JKRArcFinder(JKRArchive *, u32, u32);
    virtual ~JKRArcFinder() override;

    virtual bool findNextFile();

    u8 _10;
    u8 _11;
    u8 _12;                // padding?
    u8 _13;                // ^^
    JKRArchive *mArchive;  // _14
    u32 _18;
    u32 _1C;
    u32 _20;
};