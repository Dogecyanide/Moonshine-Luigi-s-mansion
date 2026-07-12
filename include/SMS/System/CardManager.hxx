#pragma once

#include <Dolphin/CARD.h>
#include <Dolphin/OS.h>
#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/macros.h>

class TCardBookmarkInfo {
public:
    struct TBlockData {
        enum EBlockState {
            READY,
            CORRUPT,
        };

        EBlockState mState;
        u32 _04;           // Always 4?
        OSTime mLastSave;  // 0x0008
        u32 _10;
        u32 _14;
        size_t mSaveCount;  // 0x0018
        u16 mShineCount;    // 0x001C
        u16 _01;            // 0x001E
    };

    TBlockData mFileData[3];
};

class TCardManager {
public:
    enum Commands {
        NOP,
        FORMAT,
        CREATE,
        GETBOOKMARKS,
        NOP4,
        LOADBLOCK,
        SAVEBLOCK,
        LOADFILE,
        SAVEFILE,
        EXIT
    };

    enum Blocks { BLOCK_A, BLOCK_B, BLOCK_C };

    SMS_PACKED_CLASS(TCriteria) {
    public:
        enum TEBlockStat {
            SAVING,
            ERASING,
            INVALID,
            VALID,
        };

        TCriteria();

        void set(TEBlockStat, size_t, const void *);

        TEBlockStat mBlockStat;
        size_t mSectorSaveCount;  // 0x0004
        u32 _08;                  // Always 4?
        OSTime mLastSave;         // 0x000C
        u32 _14;                  //?
        u32 _18;                  //?
        size_t mBlockSaveCount;   // 0x001C
        u16 mShineCount;          // 0x0020
        u16 __padding;            // 0x0022
        u32 _24;                  //?
    };

    TCardManager(void *, void *, s32, s32, void *, size_t);
    ~TCardManager();

    void cmdLoop();
    void copyTo(TCriteria *, TCardBookmarkInfo *);
    s32 createFile_();
    void createFile();
    s32 decideUseSector(TCriteria *);
    void filledInitData(CARDFileInfo *);
    void format();
    TCardBookmarkInfo *getBookmarkInfos_();
    void getBookmarkInfos(TCardBookmarkInfo *);
    s32 getLastStatus();
    void getOptionReadStream(JSUMemoryInputStream *);
    void getOptionWriteStream(JSUMemoryOutputStream *);
    void getReadStream(JSUMemoryInputStream *);
    void getWriteStream(JSUMemoryOutputStream *);
    s32 mount_(bool);
    s32 open_(CARDFileInfo *);
    s32 probe();
    s32 readBlock_(u32);
    void readBlock(u32);
    void readOptionBlock(u32);
    void setCardStat(CARDFileInfo *);
    void unmount();
    s32 writeBlock_(u32);
    void writeBlock(u32);
    s32 writeOptionBlock_();
    void writeOptionBlock();

    s32 mChannel;
    u32 _04;
    u32 _08;
    TCriteria mCriteriaArray[7];
    u8 _124;
    bool mMounted;               // 0x0125
    bool mRefuseOverwriteMount;  // 0x0126
    s32 mLastStatus;             // 0x0128
    void *mCardWorkArea;         // 0x012C
    void *mCARDBlock;            // 0x0130
    u32 _134;
    OSThread mThread;
    Commands mCommand;  // 0x0448
    OSMutex mMutex;
    OSCond mCond;
    TCardBookmarkInfo *mBookMarks;  // 0x046C
    u32 _470;
    Blocks mSaveBlock;  // 0x0474
};

void cardmain(void *);
void detachCallback(s32, s32);

extern TCardManager *gpCardManager;