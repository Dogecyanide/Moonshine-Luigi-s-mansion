#ifndef _SUSAMUNE_VISIBLE_GOOP_HXX
#define _SUSAMUNE_VISIBLE_GOOP_HXX

// Reset the lazy-activation state after a stage has finished setting up.
void visibleGoopOnStageSetup();

// Reconcile the current stage's pollution materials with the setting. While
// disabled this is inert unless it has materials to restore.
void visibleGoopUpdate();

#endif  // _SUSAMUNE_VISIBLE_GOOP_HXX
