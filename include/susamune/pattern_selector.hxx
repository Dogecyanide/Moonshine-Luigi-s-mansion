#ifndef SUSAMUNE_PATTERN_SELECTOR_HXX
#define SUSAMUNE_PATTERN_SELECTOR_HXX

class Menu;
class TSpineEnemy;

namespace PatternSelector {

// L + D-pad edits the three Chomplet patterns. Digits are hexadecimal, as in
// sup39's original code; zero or an unavailable branch means random.
void update();
void draw(Menu *menu);

}  // namespace PatternSelector

// Whole-function replacement installed by scripts/patches.py. Keeping the
// hook here, rather than in a Gecko cave, makes it coexist with ordinary GCTs.
extern "C" void susamuneGoToRandomNextGraphNode(TSpineEnemy *enemy);

#endif  // SUSAMUNE_PATTERN_SELECTOR_HXX
