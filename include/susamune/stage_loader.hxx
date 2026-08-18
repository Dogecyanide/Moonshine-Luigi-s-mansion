#ifndef _SUSAMUNE_STAGE_LOADER_HXX
#define _SUSAMUNE_STAGE_LOADER_HXX

#include <Dolphin/types.h>

class Menu;
class TMarDirector;

namespace StageLoader {

enum Mode {
    MODE_LOADER,
    MODE_STREAKING,
};

enum { QUEUE_CAPACITY = 120 };
enum { CUSTOM_PLAYLIST_COUNT = 7 };

struct SessionStats {
    u32 attempts;
    u32 eligibleCompletes;
    u32 qualifyingSuccesses;
    u64 totalObservedActiveQf;
    s32 successfulAverageQf;
    u32 bestStreak;
    u32 golds;
};

void init();

int queueCount();
int queueEntry(int position);
bool appendQueue(int entry);
bool removeQueue(int position);
bool moveQueue(int position, int direction);
void clearQueue();
bool startLoader();
bool loadCustomPlaylist(int slot);
bool saveCustomPlaylist(int slot);
bool customPlaylistsAvailable();
bool customPlaylistSavePending();
int customPlaylistEntryCount(int slot);
u32 customPlaylistLastError();

// Start one exact IL route repeatedly. A negative target accepts any eligible
// finish; otherwise the result must be at or below targetQf.
bool startStreak(int entry, u16 finishes, s32 targetQf);
bool start(int entry, u16 finishes, s32 targetQf);
void cancel();
bool active();
Mode mode();
void getStats(SessionStats *out);
bool modal();
bool resultOwnsInput();
// Restart inputs are sampled before IL results. Defer them until the result
// pass so a finish on the same frame wins deterministically.
bool deferRestartInput();
bool acceptDeferredRestart();
bool holdGameModeBeforeUpdate(TMarDirector *director);

void update();
void draw(Menu *menu);

// ILing owns attempt identity and reports only exact catalogue routes here.
void onILAttemptStarted(int entry);
void onILAttemptEnded();
void onILResult(int entry, s32 qf, bool eligible);
void onILWarpCancelled();

}  // namespace StageLoader

#endif  // _SUSAMUNE_STAGE_LOADER_HXX
