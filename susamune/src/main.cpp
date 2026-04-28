#include "Dolphin/GX_types.h"
#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "J2D/J2DTextBox.hxx"
#include "JKernel/JKRHeap.hxx"
#include "JUtility/JUTGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "JSystem/J2D/J2DPane.hxx"
#include "JSystem/J2D/J2DPicture.hxx"
#include "JSystem/J2D/J2DOrthoGraph.hxx"
#include "Dolphin/THP.h"
#include "susamune/settings_menu.hxx"
#include "SMS/Manager/RumbleManager.hxx"

#define SAVESTATE_START 

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

static const savestate_segment_t segments[] = {
    //{ .gc_ptr = 0, .save_buf_ptr = DATA1_OFFSET, .size = PRE_CODE_SIZE },
    //{ .gc_ptr = PRE_CODE_SIZE + TOTAL_CODE_SIZE, .save_buf_ptr = DATA1_OFFSET + PRE_CODE_SIZE, .size = DATA1_SIZE - PRE_CODE_SIZE },
    { .gc_ptr = 0, .save_buf_ptr = DATA1_OFFSET, .size = DATA1_OFFSET }
};
static const int num_segments = (sizeof(segments) / sizeof(*segments));

int gLoadMenu = 0;

SettingsMenu* gSettingsMenu = nullptr;

extern "C" u8 onUpdateGameMode(TMarDirector* director) {
    u8 state = director->updateGameMode();

    auto controller = gpApplication.mGamePads[0];

    // changing to pause menu state, and Y is held? don't pause
    if (director->mCurState != state && state == 0x5 && (controller->mButtons.mInput & TMarioGamePad::Y)) {
        state = director->mCurState;
    }

    if (gSettingsMenu && gSettingsMenu->mChangeStageReady) {
        gSettingsMenu->changeStageHook();
        gSettingsMenu->mChangeStageReady = false;
        // QF timer reset flag
        volatile u8* flag = ((volatile u8*)(0x817f00b3));
        *flag = 1;
        director->moveStage();
        state = 9;
    }

    //if ((controller->mButtons.mInput & TMarioGamePad::X) && (controller->mButtons.mInput & TMarioGamePad::Y)) {
    //    gLoadMenu = 1;
    //    state = 12; 
    //}
    return state;
}

extern "C" void onFinishAppState(RumbleMgr* rumble) {
    rumble->init();
}

// TODO: this isnt really the init hook we want.. this runs every time a stage loads
extern "C" void onSetup(TMarDirector* director) {
    static bool inited = false;
    director->setupObjects();
    
    if (inited) return; else inited = true;

    JKRHeap *oldHeap = JKRHeap::sSystemHeap->becomeCurrentHeap();
    gSettingsMenu = new SettingsMenu();
    
    if (oldHeap) {
        oldHeap->becomeCurrentHeap(); //8041434c
    } else {
        JKRHeap::sCurrentHeap = nullptr;
    }
}

void memcpy_word(void* dst, const void* src, size_t words) {
    for (size_t i = 0; i < words; i++) {
        ((u32*)dst)[i] = ((u32*)src)[i];
    }
}

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
    
u32 checksum = 0;

extern "C" s32 onUpdate(JDrama::TDirector* director) {    
    int state = director->direct();


    u32 ri = gpApplication.mGamePads[0]->mButtons.mRapidInput;



    if (ri & (JUTGamePad::DPAD_DOWN | JUTGamePad::DPAD_UP)) {
        // potentially useless: the wrong interrupts will be on the stack when reloading a savestate
        bool interrupts = OSDisableInterrupts(); 
        
        u32 save_mask = (ri & JUTGamePad::DPAD_DOWN) ? 0xFFFFFFFF : 0;
        for (int i = 0; i < num_segments; i++) {
            u32 save_ptr = segments[i].save_buf_ptr + SAVE_BASE;
            u32 gc_ptr = segments[i].gc_ptr + GC_BASE;
            u32 src = (gc_ptr & save_mask) | (save_ptr & ~save_mask);
            u32 dest = (gc_ptr & ~save_mask) | (save_ptr & save_mask);
            memcpy_word((void*)(dest), (void*)(src), segments[i].size >> 2);
        }
        OSRestoreInterrupts(interrupts);
    }








    if (ri & (JUTGamePad::DPAD_LEFT | JUTGamePad::DPAD_RIGHT)) {
        bool interrupts = OSDisableInterrupts(); 
        checksum = 0; 
        u32 save_mask = (ri & JUTGamePad::DPAD_LEFT) ? 0xFFFFFFFF : 0;
        for (int i = 0; i < num_segments; i++) {
            u32 save_ptr = segments[i].save_buf_ptr + SAVE_BASE;
            u32 gc_ptr = segments[i].gc_ptr + GC_BASE;
            u32 src = (gc_ptr & save_mask) | (save_ptr & ~save_mask);
            for (int j = 0; j < segments[i].size >> 2; j++) {
                checksum -= ((u32*)(src))[j];
            }
        }
        OSRestoreInterrupts(interrupts);
    }

    if (gSettingsMenu) {
        gSettingsMenu->setInfo(checksum);
        gSettingsMenu->processInput(gpApplication.mGamePads[0]);
    }


    if (gLoadMenu) {
        gLoadMenu = 0;
        return 9;
    } else {
        return state;
    }
}

extern "C" void afterDraw() {
    THPPlayerDrawDone();
    {
        J2DOrthoGraph ortho(0, 0, 640, 480);
        ortho.setup2D();

        GXSetViewport(0, 0, 640, 480, 0, 1);
        {
            Mtx44 mtx;
            C_MTXOrtho(mtx, 0, 480,0, 640, -1, 1);
            GXSetProjection(mtx, GX_ORTHOGRAPHIC);
        }
        
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
        
        if (gSettingsMenu)
            gSettingsMenu->draw(&ortho);
    }
}