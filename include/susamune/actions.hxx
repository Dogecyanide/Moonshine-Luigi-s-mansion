#ifndef _SUSAMUNE_ACTIONS_HXX
#define _SUSAMUNE_ACTIONS_HXX

#include <Dolphin/types.h>

// =====================================================================
// actions.hxx
//
// Ports of the SMS practice gecko codes that *do* something when a button
// combination is pressed, rather than toggling a setting: Regrab Last Held
// Object, Spawn Yoshi and Fast Forward (doc/gecko_codes.md, "Simple
// Actions/Binds"). Each is driven by a configurable bind (binds.*) instead
// of the combo its gecko original hardcodes.
//
// Unlike features.cpp these are not gecko bytes: the behaviour is written
// as C against the decomp's types. See doc/gecko_porting.md.
// =====================================================================

// Poll the binds and run whatever they ask for. Call once per frame from
// onUpdate, after gBinds.update().
void actionsApply();

#endif  // _SUSAMUNE_ACTIONS_HXX
