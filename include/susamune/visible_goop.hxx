#ifndef _SUSAMUNE_VISIBLE_GOOP_HXX
#define _SUSAMUNE_VISIBLE_GOOP_HXX

// Reconcile the current stage's pollution materials with the setting. Safe to
// call every frame; display lists are rebuilt only when their state differs.
void visibleGoopUpdate();

#endif  // _SUSAMUNE_VISIBLE_GOOP_HXX
