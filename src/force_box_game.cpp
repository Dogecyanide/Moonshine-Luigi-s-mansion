#include "SMS/Manager/FlagManager.hxx"
#include "susamune/settings.hxx"

namespace {

constexpr u32 kBoxGame1Flag = 0x1005Eu;
constexpr u32 kBoxGame2Flag = 0x1005Fu;

}  // namespace

extern "C" u32 susamuneGetSystemFlag(const TFlagManager *manager, u32 flag) {
    const u8 game = gSettings.get(SETTING_FORCE_BOX_GAME);
    if (game != 0 && (flag == kBoxGame1Flag || flag == kBoxGame2Flag)) {
        // Game 1 precedes both shines; game 2 follows only the first.
        return game == 2 && flag == kBoxGame1Flag;
    }
    return manager->getFlag(flag);
}
