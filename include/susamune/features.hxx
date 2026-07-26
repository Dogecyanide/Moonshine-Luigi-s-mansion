#ifndef _SUSAMUNE_FEATURES_HXX
#define _SUSAMUNE_FEATURES_HXX

#include <Dolphin/types.h>

// =====================================================================
// features.hxx
//
// Ports of the SMS practice "gecko" codes that reduce to memory patches,
// each driven by a setting. See doc/gecko_porting.md for the method and for
// which codes belong in C instead.
// =====================================================================

// Apply enabled features' patches and restore disabled ones'. Call once per
// frame (from onUpdate); early-outs on patches already holding the right word.
void featuresApply();

// Reset per-stage feature state. Call on every stage load (from onSetup).
void featuresOnStageLoad();

#endif  // _SUSAMUNE_FEATURES_HXX
