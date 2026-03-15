#include "sms.h"

int doublejumped;
int preva;

int g_load_menu = 0;

uint8_t OnUpdateGameMode(MarDirector* director) {
    uint8_t state = MarDirector_updateGameMode(director);
    if ((ControllerOne->buttons & PRESS_X) && (ControllerOne->buttons & PRESS_Y)) {
        g_load_menu = 1;
        state = 12; // finish ??
    }
    return state;
}

int OnUpdate(MarDirector* director) {    
    int (*GameUpdate)(MarDirector* director) = GetObjectFunction(director, Director_GameUpdate);
    
    //Update
    MarioActor* mario = (MarioActor*)*gpMarioAddress;
	
	//Check if mario is in the air
    if (mario->status & STATE_AIRBORN)
    {
		//Check if mario has a double jump available and also if the user just pressed the A button this frame
        if (!doublejumped && ControllerOne->buttons & PRESS_A && !preva)
        {
            doublejumped = 1;
            **gpMarioSpeedY = 60.0f;
        }
    }
    else	//if mario isn't in the air give him another double jump to use.
        doublejumped = 0;

	//Save the current A press so it can be used to check if its pressed next frame (think of this as a 0.5 A press)
    preva = ControllerOne->buttons & PRESS_A;
    int state = GameUpdate(director);
    if (g_load_menu) {
        g_load_menu = 0;
        return 9;
    } else {
        return state;
    }
}