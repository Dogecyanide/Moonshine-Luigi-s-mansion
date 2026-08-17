#include "susamune/stage_loader.hxx"

#include "Dolphin/mem.h"
#include "Dolphin/printf.h"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/ghost.hxx"
#include "susamune/iling.hxx"
#include "susamune/menu.hxx"
#include "susamune/mem2_map.h"
#include "susamune/qft_timer.hxx"
#include "susamune/warp_wheel.hxx"

namespace {

typedef JUtility::TColor Color;

enum SessionState {
    STATE_INACTIVE,
    STATE_REQUESTING,
    STATE_WAITING,
    STATE_RUNNING,
    STATE_RETRY_DELAY,
    STATE_RETRY_PENDING,
    STATE_COMPLETE,
    STATE_BLOCKED,
};

enum Outcome {
    OUTCOME_NONE,
    OUTCOME_SUCCESS,
    OUTCOME_RESET,
    OUTCOME_ENDED,
    OUTCOME_WRONG_ROUTE,
    OUTCOME_INELIGIBLE,
    OUTCOME_TARGET_MISS,
    OUTCOME_WARPS_DISABLED,
};

enum {
    kInvalidEntry = 0xffff,
    kRetryDelayFrames = 90,
    kResultDisplayFrames = 240,
};

struct StageLoaderRuntime {
    s32 targetQf;
    s32 lastQf;
    u32 attemptSerial;
    u16 entry;
    u16 goal;
    u16 current;
    u16 retryFrames;
    u16 displayFrames;
    u8 state;
    u8 outcome;
};

#define sRuntime (*reinterpret_cast<StageLoaderRuntime *>( \
    SUSAMUNE_MEM2_STAGE_LOADER_RUNTIME_PPC_BASE))
static_assert(sizeof(StageLoaderRuntime) <= SUSAMUNE_STAGE_LOADER_RUNTIME_SIZE,
              "Stage Loader runtime exceeds its MEM2 window");

void clearRuntime() {
    memset(&sRuntime, 0, sizeof(sRuntime));
    sRuntime.entry = kInvalidEntry;
    sRuntime.targetQf = -1;
    sRuntime.lastQf = -1;
}

void queueFailure(Outcome outcome, s32 qf) {
    sRuntime.current = 0;
    sRuntime.lastQf = qf;
    sRuntime.outcome = outcome;
    sRuntime.retryFrames = kRetryDelayFrames;
    sRuntime.displayFrames = kResultDisplayFrames;
    sRuntime.state = STATE_RETRY_DELAY;
}

bool safeToRetry() {
    return gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE &&
           gpMarDirector && gpMarDirector->_260 != 0 &&
           gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
           gpMarDirector->mDemoState == 0 &&
           (gpMarDirector->mGameState & 0x2) == 0 &&
           gpApplication.mGamePads[0] &&
           (!gMenu || !gMenu->shown()) &&
           !Ghost::observerStatsSuppressed() &&
           !WarpWheel::shown() && !WarpWheel::promptPending();
}

Color accentColor() {
    if (sRuntime.state == STATE_COMPLETE ||
        sRuntime.outcome == OUTCOME_SUCCESS) {
        return Color(80, 220, 120, 255);
    }
    if (sRuntime.state == STATE_BLOCKED ||
        (sRuntime.outcome >= OUTCOME_RESET &&
         sRuntime.outcome <= OUTCOME_TARGET_MISS)) {
        return Color(245, 95, 85, 255);
    }
    return Color(80, 180, 255, 255);
}

}  // namespace

namespace StageLoader {

void init() {
    clearRuntime();
}

bool start(int entry, u16 finishes, s32 targetQf) {
    if (entry < 0 || entry >= ILing::count() || entry >= kInvalidEntry ||
        finishes == 0 || targetQf < -1 ||
        Ghost::observerPreparing() || Ghost::observerStatsSuppressed()) {
        return false;
    }

    clearRuntime();
    sRuntime.entry = (u16)entry;
    sRuntime.goal = finishes;
    sRuntime.targetQf = targetQf;
    sRuntime.state = STATE_REQUESTING;
    if (!WarpWheel::requestILStart(entry)) {
        clearRuntime();
        return false;
    }
    if (sRuntime.state == STATE_REQUESTING) {
        sRuntime.state = STATE_WAITING;
    }
    return true;
}

void cancel() {
    clearRuntime();
}

bool active() {
    return sRuntime.state != STATE_INACTIVE;
}

void update() {
    if (sRuntime.state == STATE_INACTIVE ||
        sRuntime.state == STATE_COMPLETE ||
        sRuntime.state == STATE_BLOCKED) {
        return;
    }

    if (sRuntime.displayFrames > 0) {
        sRuntime.displayFrames--;
    }
    if (sRuntime.state == STATE_RETRY_DELAY) {
        if (sRuntime.retryFrames > 0) sRuntime.retryFrames--;
        if (sRuntime.retryFrames == 0) {
            sRuntime.state = STATE_RETRY_PENDING;
        }
    }
    if (sRuntime.state != STATE_RETRY_PENDING || !safeToRetry()) {
        return;
    }

    sRuntime.state = STATE_REQUESTING;
    if (!WarpWheel::requestILStart(sRuntime.entry)) {
        sRuntime.state = STATE_BLOCKED;
        sRuntime.outcome = OUTCOME_WARPS_DISABLED;
        sRuntime.lastQf = -1;
        sRuntime.displayFrames = kResultDisplayFrames;
        return;
    }
    if (sRuntime.state == STATE_REQUESTING) {
        sRuntime.state = STATE_WAITING;
    }
}

void onILAttemptStarted(int entry) {
    if (!active() || sRuntime.state == STATE_COMPLETE ||
        sRuntime.state == STATE_BLOCKED) {
        return;
    }

    const u32 serial = gQFTTimer.attemptSerial();
    if (entry != (int)sRuntime.entry) {
        if (sRuntime.state == STATE_RUNNING) {
            queueFailure(OUTCOME_WRONG_ROUTE, -1);
        } else if (sRuntime.state == STATE_WAITING) {
            sRuntime.state = STATE_RETRY_PENDING;
        }
        return;
    }

    if (sRuntime.state == STATE_RUNNING) {
        if (serial == sRuntime.attemptSerial) return;
        queueFailure(OUTCOME_RESET, -1);
    }
    sRuntime.attemptSerial = serial;
    sRuntime.retryFrames = 0;
    sRuntime.state = STATE_RUNNING;
}

void onILAttemptEnded() {
    if (sRuntime.state == STATE_RUNNING) {
        queueFailure(OUTCOME_ENDED, -1);
    } else if (sRuntime.state == STATE_WAITING) {
        sRuntime.state = STATE_RETRY_PENDING;
    }
}

void onILResult(int entry, s32 qf, bool eligible) {
    if (sRuntime.state != STATE_RUNNING) return;

    if (entry != (int)sRuntime.entry) {
        queueFailure(OUTCOME_WRONG_ROUTE, qf);
        return;
    }
    if (!eligible || qf < 0) {
        queueFailure(OUTCOME_INELIGIBLE, qf);
        return;
    }
    if (sRuntime.targetQf >= 0 && qf > sRuntime.targetQf) {
        queueFailure(OUTCOME_TARGET_MISS, qf);
        return;
    }

    if (sRuntime.current < sRuntime.goal) sRuntime.current++;
    sRuntime.lastQf = qf;
    sRuntime.outcome = OUTCOME_SUCCESS;
    sRuntime.displayFrames = kResultDisplayFrames;
    if (sRuntime.current >= sRuntime.goal) {
        sRuntime.retryFrames = 0;
        sRuntime.state = STATE_COMPLETE;
    } else {
        sRuntime.retryFrames = kRetryDelayFrames;
        sRuntime.state = STATE_RETRY_DELAY;
    }
}

void onILWarpCancelled() {
    if (sRuntime.state == STATE_WAITING) {
        sRuntime.retryFrames = 0;
        sRuntime.state = STATE_RETRY_PENDING;
    }
}

void draw(Menu *menu) {
    if (!menu || menu->shown() || !active()) return;
    if (sRuntime.state != STATE_COMPLETE &&
        sRuntime.state != STATE_BLOCKED && sRuntime.displayFrames == 0) {
        return;
    }

    char status[80];
    char time[20];
    time[0] = '\0';
    if (sRuntime.lastQf >= 0) {
        ILing::formatTime(sRuntime.lastQf, time, sizeof(time));
    }

    switch ((Outcome)sRuntime.outcome) {
    case OUTCOME_SUCCESS:
        snprintf(status, sizeof(status), "%s %u/%u  %s",
                 sRuntime.state == STATE_COMPLETE ? "Complete" : "Success",
                 (unsigned)sRuntime.current, (unsigned)sRuntime.goal, time);
        break;
    case OUTCOME_RESET:
        snprintf(status, sizeof(status), "Reset - streak %u/%u",
                 (unsigned)sRuntime.current, (unsigned)sRuntime.goal);
        break;
    case OUTCOME_ENDED:
        snprintf(status, sizeof(status), "Attempt ended - streak %u/%u",
                 (unsigned)sRuntime.current, (unsigned)sRuntime.goal);
        break;
    case OUTCOME_WRONG_ROUTE:
        snprintf(status, sizeof(status), "Wrong finish - streak %u/%u%s%s",
                 (unsigned)sRuntime.current, (unsigned)sRuntime.goal,
                 time[0] ? "  " : "", time);
        break;
    case OUTCOME_INELIGIBLE:
        snprintf(status, sizeof(status), "Ineligible - streak %u/%u%s%s",
                 (unsigned)sRuntime.current, (unsigned)sRuntime.goal,
                 time[0] ? "  " : "", time);
        break;
    case OUTCOME_TARGET_MISS:
        snprintf(status, sizeof(status), "Target missed - streak %u/%u  %s",
                 (unsigned)sRuntime.current, (unsigned)sRuntime.goal, time);
        break;
    case OUTCOME_WARPS_DISABLED:
        snprintf(status, sizeof(status), "Stopped - warps disabled");
        break;
    default:
        return;
    }

    const int x = 150;
    const int y = 350;
    const int w = 340;
    const int h = 58;
    menu->fillBox(x, y, w, h, Color(8, 12, 20, 210));
    menu->fillBox(x, y, 4, h, accentColor());

    const char *name = ILing::label(sRuntime.entry);
    int nameSize = 15;
    while (nameSize > 10 && Menu::textWidth(name, nameSize) > w - 22) {
        nameSize--;
    }
    menu->drawText(name, x + 12, y + 7, nameSize, nameSize,
                   Color(255, 255, 255, 255));
    menu->drawText(status, x + 12, y + 32, 14, 14,
                   Color(230, 236, 245, 255));
}

}  // namespace StageLoader
