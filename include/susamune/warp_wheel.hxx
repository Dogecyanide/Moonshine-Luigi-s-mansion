#ifndef _SUSAMUNE_WARP_WHEEL_HXX
#define _SUSAMUNE_WARP_WHEEL_HXX

#include <Dolphin/types.h>

#include "SMS/Player/MarioGamePad.hxx"

class TMarDirector;

// =====================================================================
// warp_wheel.hxx
//
// Instant warps: the destination tables, the three restart binds and the
// on-screen wheel that picks between them (doc/gecko_codes.md, "Warp
// Wheel" and "Level Restart").
//
// A warp is two-phase and never happens mid-frame. Arming one only records
// a destination; kick() then pushes the director into its stage-exit state
// from the game-mode hook, and onDirected() writes the destination once the
// exit fade has run and direct() finally asks for an app state.
// =====================================================================

namespace LevelWarp {

struct Dest {
    u8 area;
    u8 episode;
    // TFlagManager flag 0x40003. For a secret/boss area it is the parent
    // episode index; areas with several scenarios behind one id (the Sirena
    // hotel and casino) are picked apart by it too.
    u8 gameInt3;
};

// Arm a warp. warpTo() also records the destination for warpToLast().
void warpTo(const Dest &dest);
void warpToLast();
// Reload the current area, by the same path a wheel warp takes. keepSpawn
// brings Mario back out of the entrance he arrived through instead of the
// area's default spawn.
void restart(bool keepSpawn);

// From onUpdateGameMode. Starts the exit transition when a warp is armed,
// returning the director state to enter; otherwise returns `state` as-is.
u8 kick(TMarDirector *director, u8 state);

// From onUpdate, with direct()'s return value. Applies the armed
// destination when the transition completes, and drives Area Lock.
s32 onDirected(s32 appState);

}  // namespace LevelWarp

namespace WarpWheel {

// Call before director->direct(): while the wheel is open it eats the pad,
// so the game must not have sampled it yet.
void update(TMarioGamePad *pad);
// Call after Menu::draw(), which is what leaves the 2D state set up.
void draw();
bool shown();

}  // namespace WarpWheel

#endif  // _SUSAMUNE_WARP_WHEEL_HXX
