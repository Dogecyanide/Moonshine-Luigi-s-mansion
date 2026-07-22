#ifndef _SUSAMUNE_FEATURES_HXX
#define _SUSAMUNE_FEATURES_HXX

#include <Dolphin/types.h>

// =====================================================================
// features.hxx
//
// Runtime ports of the SMS practice "gecko" codes that are simple memory
// patches -- overwriting a game instruction or datum. Each such code maps
// to a BOOL setting (settings.hxx); when the setting is On the patched word
// is written, when Off the original word (captured live from the game at
// first apply) is restored. featuresApply() re-applies the whole table each
// frame, mirroring how the gecko code handler runs every frame, and only
// touches memory when a target word actually needs to change.
//
// See doc/gecko_porting.md for how a gecko code is reverse-engineered into
// a table entry, and for the classes of code this mechanism does and does
// not cover.
// =====================================================================

// Apply every enabled feature's patches and restore every disabled one's.
// Call once per frame (from onUpdate). Cheap: it early-outs on any patch
// whose target already holds the desired word.
void featuresApply();

#endif  // _SUSAMUNE_FEATURES_HXX
