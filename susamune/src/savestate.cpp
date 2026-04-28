#include "susamune/savestate.hxx"
#include "J2D/J2DOrthoGraph.hxx"
#include "SMS/System/Application.hxx"

#define GC_BASE 0xC0000000
#define SAVE_BASE 0xD0000000 // Nintendont
//#define SAVE_BASE 0x70000000 // Dolphin
#define RAM_SIZE 0x01800000

#define PRE_CODE_SIZE 0x3100
#define TOTAL_CODE_SIZE 0x36CAA0

//#define DATA2_SIZE 0xD00000
//#define DATA2_OFFSET 0x032F1000 // space at end of nintendont
//
//#define DATA1_SIZE (RAM_SIZE - TOTAL_CODE_SIZE - DATA2_SIZE)
//#define DATA1_OFFSET (0x02E80000 - DATA1_SIZE) // cram it before the end of nintendont cache

#define DATA1_OFFSET 0x01000000
#define DATA1_SIZE (RAM_SIZE - TOTAL_CODE_SIZE)

typedef struct {
    u32 gc_ptr;
    u32 save_buf_ptr;
    size_t size;
} savestate_segment_t;

static const savestate_segment_t sSavestateSegments[] = {
    //{ .gc_ptr = 0, .save_buf_ptr = DATA1_OFFSET, .size = PRE_CODE_SIZE },
    //{ .gc_ptr = PRE_CODE_SIZE + TOTAL_CODE_SIZE, .save_buf_ptr = DATA1_OFFSET + PRE_CODE_SIZE, .size = DATA1_SIZE - PRE_CODE_SIZE },
    { .gc_ptr = 0, .save_buf_ptr = DATA1_OFFSET, .size = DATA1_OFFSET }
};
static const int sNumSavestateSegments = (sizeof(sSavestateSegments) / sizeof(*sSavestateSegments));

// stupid thing to display memory state, more or less
//static u8 memoryData[49*34] = {0};
//static u8 memoryDataDisplay[49*34] = {0};
//static int gEnableGrid = 0;

 /*int chunkCount = 0;
        int chunkOfs = 0;
        u64 chunkSum = 0;
        //for (int i = 0; i < num_segments; i++) {
            u32 save_ptr = GC_BASE; // segments[i].save_buf_ptr + SAVE_BASE;
            for (int j = 0; j < (RAM_SIZE) >> 2; j++) {
                chunkSum += ((u32*)(save_ptr))[j];
                chunkOfs++; 
                if (chunkOfs == 3777) {
                    chunkOfs = 0;
                    u8 chunkRes = (u8)(((chunkSum / 3777) >> 24));
                    memoryDataDisplay[chunkCount] = (chunkRes - memoryData[chunkCount]) ? 0xFF : 0;
                    memoryData[chunkCount] = chunkRes;
                    chunkSum = 0;
                    chunkCount++;
                }
            }
        //} */
    
        //if (ri & (JUTGamePad::X)) {
            //    gEnableGrid++;
            //}

SavestateManager::SavestateManager() {
    mChksumText             = new J2DTextBox(gpSystemFont->mFont, "00000000");
    mChksumText->mCharSizeX = 30;
    mChksumText->mCharSizeY = 30;
    mChksumText->mGradientBottom = {255, 0, 128, 255};
    mChksumText->mGradientTop    = {255, 0, 128, 255};
}

void memcpy_word(void* dst, const void* src, size_t words) {
    for (size_t i = 0; i < words; i++) {
        ((u32*)dst)[i] = ((u32*)src)[i];
    }
}

void SavestateManager::updateHook(TMarioGamePad* controller) {
    u32 ri = controller->mButtons.mRapidInput;

    if (ri & (JUTGamePad::DPAD_DOWN | JUTGamePad::DPAD_UP)) {
        // potentially useless: the wrong interrupts will be on the stack when reloading a savestate
        bool interrupts = OSDisableInterrupts(); 
        
        u32 save_mask = (ri & JUTGamePad::DPAD_DOWN) ? 0xFFFFFFFF : 0;
        for (int i = 0; i < sNumSavestateSegments; i++) {
            u32 save_ptr = sSavestateSegments[i].save_buf_ptr + SAVE_BASE;
            u32 gc_ptr = sSavestateSegments[i].gc_ptr + GC_BASE;
            u32 src = (gc_ptr & save_mask) | (save_ptr & ~save_mask);
            u32 dest = (gc_ptr & ~save_mask) | (save_ptr & save_mask);
            memcpy_word((void*)(dest), (void*)(src), sSavestateSegments[i].size >> 2);
        }
        OSRestoreInterrupts(interrupts);
    }

    if (ri & (JUTGamePad::DPAD_LEFT | JUTGamePad::DPAD_RIGHT)) {
        bool interrupts = OSDisableInterrupts(); 
        u32 checksum = 0; 
        u32 save_mask = (ri & JUTGamePad::DPAD_LEFT) ? 0xFFFFFFFF : 0;
        for (int i = 0; i < sNumSavestateSegments; i++) {
            u32 save_ptr = sSavestateSegments[i].save_buf_ptr + SAVE_BASE;
            u32 gc_ptr = sSavestateSegments[i].gc_ptr + GC_BASE;
            u32 src = (gc_ptr & save_mask) | (save_ptr & ~save_mask);
            for (int j = 0; j < sSavestateSegments[i].size >> 2; j++) {
                checksum -= ((u32*)(src))[j];
            }
        }
        OSRestoreInterrupts(interrupts);
        char *mInfo = mChksumText->getStringPtr();
        for (int i = 7; i >= 0; i--, checksum >>= 4) {
            int low   = checksum & 0xF;
            char c   = (char)(((low <= 9) ? 0x30 : 0x37) + low);
            mInfo[i] = c;
        }
    }
}

void SavestateManager::draw(J2DOrthoGraph *ortho) {
    // mChksumText->draw(0, 30);

    //const float l = 10.;
    //const float gap = 3.;
    //const int numBoxesX = (int)((640.)/(l+gap));
    //const int numBoxesY = (int)((448.)/(l+gap));
    //if (gEnableGrid) {
    //    u8* memoryDataSrc = (gEnableGrid & 1) ? memoryDataDisplay : memoryData;
    //    for (int x = 0; x < numBoxesX; x++) {
    //        for (int y = 0; y < numBoxesY; y++) {
    //            J2DFillBox((int)(gap + (l+gap)*x), (int)(gap + (l+gap)*y), (int)l, (int)l, {memoryDataSrc[y*numBoxesX + x],0,0,255});
    //        }
    //    }
    //}
}