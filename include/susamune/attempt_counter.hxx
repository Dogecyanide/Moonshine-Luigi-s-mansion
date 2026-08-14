#ifndef SUSAMUNE_ATTEMPT_COUNTER_HXX
#define SUSAMUNE_ATTEMPT_COUNTER_HXX

#include <Dolphin/types.h>

class Menu;
class TMarDirector;

// Native version of sup39's Attempt Counter and its two companion control
// codes. State stays outside the game's savestate snapshot, so loading a
// practice state never rewinds the counts.
class AttemptCounter {
public:
    void init();
    void onStageSetup(TMarDirector *director);
    void update();
    void draw(Menu *menu) const;

private:
    void show();
    void addAttempt();
    void addSuccess();

    TMarDirector *mDirector;
    u32           mLastShineSerial;
    u32           mLastDepartureSerial;
    u16           mSuccessCount;
    u16           mAttemptCount;
    u16           mPreviousArea;
    u16           mDisplayFrames;
    bool          mHaveArea;
    bool          mGotShine;
    bool          mWasEnabled;
};

extern AttemptCounter &gAttemptCounter;

#endif  // SUSAMUNE_ATTEMPT_COUNTER_HXX
