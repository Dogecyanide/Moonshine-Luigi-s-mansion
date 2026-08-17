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
    enum { POST_CORONA = 0x80 };

    u8 area;
    u8 episode;
    // TFlagManager flag 0x40003. For a secret/boss area it is the parent
    // episode index; areas with several scenarios behind one id (the Sirena
    // hotel and casino) are picked apart by it too.
    // The high bit is an internal Plaza variant marker and is masked before
    // flag 0x40003 is written.
    u8 gameInt3;
};

// Parent main-stage area for an internal scene, or 0xFF when none exists.
u8 parentArea(u8 area);

// Arm a warp. warpTo() also records the destination for warpToLast().
void warpTo(const Dest &dest);
// Guarded course arm. `selectedIL` lets a late PB abort unwind the attempt;
// the token parameter remains ABI-compatible with the PR4 call sites.
void warpToGuarded(const Dest &dest, u32 legacyToken, bool selectedIL);
// Warp to `dest`, but make the next director see `source` as the stage Mario
// just left. Used by travel practice to select the retail Plaza return point.
void warpFrom(const Dest &source, const Dest &dest);
void warpFromGuarded(const Dest &source, const Dest &dest,
                     u32 legacyToken, bool selectedIL);
void warpToLast();
// Cancel mod-owned arm/prompt/tail state. Snapshot restore can rewind a
// director after the arm was consumed, so its out-of-snapshot tail is stale.
// The disabled-warps service may preserve a pending retail Exit confirmation.
void cancelPending(bool keepExitApproval = false);
// Reload the current area, by the same path a wheel warp takes. keepSpawn
// brings Mario back out of the entrance he arrived through instead of the
// area's default spawn.
void restart(bool keepSpawn);
// Return an internal scene to its parent episode's default start; main stages
// restart at their own default start.
void restartFull();
// Queue a restart from Sunshine's save UI. It cannot leave until the dialog
// has closed and the card worker has remained idle across a game-mode tick.
void restartAfterSave(bool keepSpawn);
void restartFullAfterSave();

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
// Wrap the two destructive retail pause results. Sunshine resumes before the
// raw-pad confirmation opens; no mod state holds the closed pause object.
u8 guardExitArea(u8 nextState);
// Start an IL immediately when safe, or hide the menu and hold it behind the
// shared unsaved-PB confirmation. False means warps are disabled.
bool requestILStart(int entry);
// Savestate restores destroy volatile ghost state. The request opens the same
// PB confirmation when needed; an approved load is returned exactly once.
bool requestSavestateLoad();
bool takeSavestateLoadApproval();
// Called before updateGameMode() so a pending retail stage teardown can be
// held after any result demo, but before moveStage() mutates the director.
bool holdGameModeBeforeUpdate(TMarDirector *director);
// Applies an approved title/Exit action after updateGameMode() has sampled the
// normal game state.
u8 applyPendingGameModeAction(TMarDirector *director, u8 state);
// Disable-warps still permits a user-confirmed retail Exit Area.
bool retailExitPending();
// The global confirmation owns raw input independently of the menu/wheel.
bool promptShown();
// A prompt suspended in the Ghosts save flow still owns the destructive
// action and silences configured binds, even though its overlay is hidden.
bool promptPending();
// Return an exact token-bound Ghosts save flow to its original confirmation.
bool resumePBPrompt(u32 token);
// Call after Menu::draw(), which is what leaves the 2D state set up.
void draw();
bool shown();

}  // namespace WarpWheel

#endif  // _SUSAMUNE_WARP_WHEEL_HXX
