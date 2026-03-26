#include "SMS/System/Application.hxx"

int g_load_menu = 0;

extern "C" u8 onUpdateGameMode(TMarDirector* director) {
    u8 state = director->updateGameMode();

    auto controller = gpApplication.mGamePads[0];
    if ((controller->mButtons.mInput & TMarioGamePad::X) && (controller->mButtons.mInput & TMarioGamePad::Y)) {
        g_load_menu = 1;
        state = 12; 
    }
    return state;
}

extern "C" s32 onUpdate(JDrama::TDirector* director) {    
    // TODO: should be just director->direct(); but it's not working...
    void* vtable_ptr_addr = *((void**)director);
    void** func_addr = (void**)((char*)vtable_ptr_addr + 100);
    int state = ((u32 (*)(void*))(*func_addr))(director);

    if (g_load_menu) {
        g_load_menu = 0;
        return 9;
    } else {
        return state;
    }
}