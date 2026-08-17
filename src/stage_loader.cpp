#include "susamune/stage_loader.hxx"

#include "Dolphin/mem.h"
#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "JSystem/JUtility/JUTGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/binds.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/ghost.hxx"
#include "susamune/iling.hxx"
#include "susamune/menu.hxx"
#include "susamune/mem2_map.h"
#include "susamune/qft_timer.hxx"
#include "susamune/settings.hxx"
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

enum ModalState {
    MODAL_NONE,
    MODAL_PENDING,
    MODAL_VISIBLE,
};

enum {
    kRetryDelayFrames = 90,
    kResultDisplayFrames = 240,
    kModalMask = JUTGamePad::A | JUTGamePad::B | JUTGamePad::START,
};

struct StageLoaderRuntime {
    u64 totalObservedActiveQf;
    u64 successfulQfTotal;
    u32 attemptSerial;
    u32 attempts;
    u32 eligibleCompletes;
    u32 qualifyingSuccesses;
    u32 golds;
    s32 targetQf;
    s32 lastQf;
    s32 lastObservedQf;
    u16 goal;
    u16 progress;
    u16 currentStreak;
    u16 bestStreak;
    u16 retryFrames;
    u16 displayFrames;
    u16 modalPrevious;
    u8 draftQueue[StageLoader::QUEUE_CAPACITY];
    u8 activeQueue[StageLoader::QUEUE_CAPACITY];
    u8 draftCount;
    u8 activeCount;
    u8 activeIndex;
    u8 lastEntry;
    u8 state;
    u8 outcome;
    u8 mode;
    u8 modalState;
    u8 modalReady;
    u8 holdingDeparture;
};

#define sRuntime (*reinterpret_cast<StageLoaderRuntime *>( \
    SUSAMUNE_MEM2_STAGE_LOADER_RUNTIME_PPC_BASE))
static_assert(sizeof(StageLoaderRuntime) == 136,
              "Stage Loader runtime layout changed");
static_assert(sizeof(StageLoaderRuntime) <= SUSAMUNE_STAGE_LOADER_RUNTIME_SIZE,
              "Stage Loader runtime exceeds its MEM2 window");

void resetSession() {
    sRuntime.totalObservedActiveQf = 0;
    sRuntime.successfulQfTotal = 0;
    sRuntime.attemptSerial = 0;
    sRuntime.attempts = 0;
    sRuntime.eligibleCompletes = 0;
    sRuntime.qualifyingSuccesses = 0;
    sRuntime.golds = 0;
    sRuntime.targetQf = -1;
    sRuntime.lastQf = -1;
    sRuntime.lastObservedQf = -1;
    sRuntime.goal = 0;
    sRuntime.progress = 0;
    sRuntime.currentStreak = 0;
    sRuntime.bestStreak = 0;
    sRuntime.retryFrames = 0;
    sRuntime.displayFrames = 0;
    sRuntime.modalPrevious = 0;
    memset(sRuntime.activeQueue, 0, sizeof(sRuntime.activeQueue));
    sRuntime.activeCount = 0;
    sRuntime.activeIndex = 0;
    sRuntime.lastEntry = 0;
    sRuntime.state = STATE_INACTIVE;
    sRuntime.outcome = OUTCOME_NONE;
    sRuntime.mode = StageLoader::MODE_LOADER;
    sRuntime.modalState = MODAL_NONE;
    sRuntime.modalReady = 0;
    sRuntime.holdingDeparture = 0;
}

void resetAll() {
    memset(&sRuntime, 0, sizeof(sRuntime));
    sRuntime.targetQf = -1;
    sRuntime.lastQf = -1;
    sRuntime.lastObservedQf = -1;
}

void incrementSaturated(u32 &value) {
    if (value != 0xffffffffu) value++;
}

void addSaturated(u64 &total, u64 amount) {
    const u64 maximum = ~(u64)0;
    total = maximum - total < amount ? maximum : total + amount;
}

int expectedEntry() {
    return sRuntime.activeIndex < sRuntime.activeCount
               ? sRuntime.activeQueue[sRuntime.activeIndex]
               : -1;
}

s32 liveQf() {
    s32 qf = -1;
    return gQFTTimer.currentQf(&qf) && qf >= 0 ? qf : -1;
}

void observeQf(s32 qf) {
    if (qf < 0) return;
    if (sRuntime.lastObservedQf < 0) {
        addSaturated(sRuntime.totalObservedActiveQf, (u32)qf);
    } else if (qf > sRuntime.lastObservedQf) {
        addSaturated(sRuntime.totalObservedActiveQf,
                     (u32)(qf - sRuntime.lastObservedQf));
    }
    if (qf > sRuntime.lastObservedQf) sRuntime.lastObservedQf = qf;
}

void observeLive() {
    if (sRuntime.state == STATE_RUNNING &&
        sRuntime.attemptSerial == gQFTTimer.attemptSerial()) {
        observeQf(liveQf());
    }
}

bool safeToRetry() {
    return gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE &&
           gpMarDirector && gpMarDirector->_260 != 0 &&
           gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
           gpMarDirector->mDemoState == 0 &&
           ((gpMarDirector->mGameState & 0x2) == 0 ||
            sRuntime.holdingDeparture) &&
           gpApplication.mGamePads[0] &&
           (!gMenu || !gMenu->shown()) &&
           !Ghost::observerStatsSuppressed() &&
           !WarpWheel::shown() && !WarpWheel::promptPending();
}

bool liveResultDirector(const TMarDirector *director) {
    return director &&
           gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE &&
           gpMarDirector == director && director->_260 != 0 &&
           director->mCurState == TMarDirector::STATE_NORMAL &&
           director->mDemoState == 0;
}

bool requestCurrent() {
    const int entry = expectedEntry();
    if (entry < 0 || entry >= ILing::count()) return false;

    sRuntime.state = STATE_REQUESTING;
    if (!WarpWheel::requestILStart(entry)) {
        sRuntime.state = STATE_BLOCKED;
        sRuntime.outcome = OUTCOME_WARPS_DISABLED;
        sRuntime.lastEntry = (u8)entry;
        sRuntime.lastQf = -1;
        sRuntime.displayFrames = kResultDisplayFrames;
        sRuntime.holdingDeparture = 0;
        return false;
    }
    if (sRuntime.state == STATE_REQUESTING) {
        sRuntime.state = STATE_WAITING;
    }
    sRuntime.holdingDeparture = 0;
    return true;
}

void beginAttempt(u32 serial) {
    sRuntime.attemptSerial = serial;
    incrementSaturated(sRuntime.attempts);
    sRuntime.lastObservedQf = -1;
    observeQf(liveQf());
    sRuntime.retryFrames = 0;
    sRuntime.holdingDeparture = 0;
    sRuntime.state = STATE_RUNNING;
}

void queueFailure(Outcome outcome, s32 qf) {
    const int entry = expectedEntry();
    if (qf >= 0) observeQf(qf);
    if (entry >= 0) sRuntime.lastEntry = (u8)entry;
    sRuntime.lastQf = qf;
    sRuntime.outcome = outcome;
    sRuntime.currentStreak = 0;
    if (sRuntime.mode == StageLoader::MODE_STREAKING) {
        sRuntime.progress = 0;
    }
    sRuntime.retryFrames = kRetryDelayFrames;
    sRuntime.displayFrames = kResultDisplayFrames;
    sRuntime.holdingDeparture = 0;
    sRuntime.state = STATE_RETRY_DELAY;
}

void queueSuccess(s32 qf) {
    const int entry = expectedEntry();
    observeQf(qf);
    sRuntime.lastEntry = (u8)entry;
    sRuntime.lastQf = qf;
    sRuntime.outcome = OUTCOME_SUCCESS;
    incrementSaturated(sRuntime.qualifyingSuccesses);
    addSaturated(sRuntime.successfulQfTotal, (u32)qf);
    if (sRuntime.currentStreak != 0xffff) sRuntime.currentStreak++;
    if (sRuntime.currentStreak > sRuntime.bestStreak) {
        sRuntime.bestStreak = sRuntime.currentStreak;
    }
    sRuntime.displayFrames = kResultDisplayFrames;

    if (sRuntime.mode == StageLoader::MODE_STREAKING) {
        if (sRuntime.progress < sRuntime.goal) sRuntime.progress++;
        if (sRuntime.progress >= sRuntime.goal) {
            sRuntime.retryFrames = 0;
            sRuntime.state = STATE_COMPLETE;
            sRuntime.modalState = MODAL_PENDING;
        } else {
            sRuntime.retryFrames = kRetryDelayFrames;
            sRuntime.state = STATE_RETRY_DELAY;
        }
        return;
    }

    if (sRuntime.progress < sRuntime.activeCount) sRuntime.progress++;
    if (sRuntime.activeIndex < sRuntime.activeCount) sRuntime.activeIndex++;
    if (sRuntime.activeIndex >= sRuntime.activeCount) {
        sRuntime.retryFrames = 0;
        sRuntime.state = STATE_COMPLETE;
    } else {
        sRuntime.retryFrames = kRetryDelayFrames;
        sRuntime.state = STATE_RETRY_DELAY;
    }
}

u16 modalHeld() {
    return (u16)(JUTGamePad::mPadStatus[0].mButton & kModalMask);
}

void showModal() {
    sRuntime.modalState = MODAL_VISIBLE;
    sRuntime.modalPrevious = modalHeld();
    sRuntime.modalReady = sRuntime.modalPrevious == 0;
    gBinds.suppressUntilRelease();
}

void updateModal() {
    gBinds.suppressUntilRelease();
    const u16 current = modalHeld();
    if (!sRuntime.modalReady) {
        sRuntime.modalPrevious = current;
        if (current == 0) sRuntime.modalReady = 1;
        return;
    }
    const u16 pressed = (u16)(current & ~sRuntime.modalPrevious);
    sRuntime.modalPrevious = current;
    if (!pressed) return;

    sRuntime.modalState = MODAL_NONE;
    sRuntime.modalReady = 0;
    sRuntime.holdingDeparture = 0;
    sRuntime.state = STATE_INACTIVE;
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

void formatDuration(u64 qf, char *out, u32 size) {
    // Keep this in 32-bit arithmetic: the freestanding PPC link has no
    // 64-bit modulo helper. The cap is roughly 49 days of active QFT.
    const u32 maxQf = (0xffffffffu / 1001u) * 120u;
    const u32 frames = qf > maxQf ? maxQf : (u32)qf;
    const u32 millis = (frames / 120u) * 1001u +
                       ((frames % 120u) * 1001u) / 120u;
    snprintf(out, size, "%u:%02u:%02u.%03u",
             (unsigned)(millis / 3600000u),
             (unsigned)((millis / 60000u) % 60u),
             (unsigned)((millis / 1000u) % 60u),
             (unsigned)(millis % 1000u));
}

void drawModalRow(Menu *menu, int y, const char *name, const char *value) {
    menu->drawText(name, 108, y, 15, 15, Color(200, 206, 220, 255));
    menu->drawText(value, 532 - Menu::textWidth(value, 15), y, 15, 15,
                   Color(120, 220, 150, 255));
}

void drawFinalModal(Menu *menu) {
    const int x = 82;
    const int y = 66;
    const int w = 476;
    const int h = 326;
    menu->fillBox(0, 0, 640, 480, Color(0, 0, 0, 150));
    menu->fillBox(x, y, w, h, Color(8, 12, 20, 245));
    menu->fillBox(x, y, w, 4, Color(80, 220, 120, 255));

    const char *title = "STREAK COMPLETE";
    menu->drawText(title, 320 - Menu::textWidth(title, 20) / 2,
                   y + 16, 20, 20, Color(255, 255, 255, 255));

    const char *route = ILing::label(sRuntime.lastEntry);
    int routeSize = 16;
    while (routeSize > 10 && Menu::textWidth(route, routeSize) > w - 44) {
        routeSize--;
    }
    menu->drawText(route, 320 - Menu::textWidth(route, routeSize) / 2,
                   y + 48, routeSize, routeSize,
                   Color(120, 220, 150, 255));

    char target[32];
    if (sRuntime.targetQf < 0) {
        strcpy(target, "Target: Any finish");
    } else {
        char time[20];
        ILing::formatTime(sRuntime.targetQf, time, sizeof(time));
        snprintf(target, sizeof(target), "Target: %s", time);
    }
    char streak[24];
    snprintf(streak, sizeof(streak), "Streak: %u/%u",
             (unsigned)sRuntime.progress, (unsigned)sRuntime.goal);
    menu->drawText(target, x + 28, y + 77, 13, 13,
                   Color(184, 194, 214, 255));
    menu->drawText(streak, x + w - 28 - Menu::textWidth(streak, 13),
                   y + 77, 13, 13, Color(184, 194, 214, 255));

    char value[32];
    int rowY = y + 119;
    snprintf(value, sizeof(value), "%u / %u",
             (unsigned)sRuntime.qualifyingSuccesses,
             (unsigned)sRuntime.attempts);
    drawModalRow(menu, rowY, "Completes / attempts", value);
    rowY += 34;
    formatDuration(sRuntime.totalObservedActiveQf, value, sizeof(value));
    drawModalRow(menu, rowY, "Active time", value);
    rowY += 34;
    if (sRuntime.qualifyingSuccesses) {
        formatDuration(sRuntime.successfulQfTotal /
                           sRuntime.qualifyingSuccesses,
                       value, sizeof(value));
    } else {
        strcpy(value, "--");
    }
    drawModalRow(menu, rowY, "Average time", value);
    rowY += 34;
    drawModalRow(menu, rowY, "Golds", "--");

    const char *hint = "A / B / START  Continue";
    menu->drawText(hint, 320 - Menu::textWidth(hint, 12) / 2,
                   y + h - 25, 12, 12, Color(104, 114, 136, 255));
}

void drawFullNotice(Menu *menu) {
    char status[96];
    char time[24];
    time[0] = '\0';
    if (sRuntime.lastQf >= 0) {
        ILing::formatTime(sRuntime.lastQf, time, sizeof(time));
    }

    if (sRuntime.mode == StageLoader::MODE_LOADER) {
        const unsigned item = sRuntime.activeIndex < sRuntime.activeCount
                                  ? sRuntime.activeIndex + 1
                                  : sRuntime.activeCount;
        switch ((Outcome)sRuntime.outcome) {
        case OUTCOME_SUCCESS:
            snprintf(status, sizeof(status), "%s %u/%u%s%s",
                     sRuntime.state == STATE_COMPLETE ? "Playlist complete"
                                                      : "Finished",
                     (unsigned)sRuntime.progress,
                     (unsigned)sRuntime.activeCount,
                     time[0] ? "  " : "", time);
            break;
        case OUTCOME_RESET:
            snprintf(status, sizeof(status), "Reset - retry %u/%u", item,
                     (unsigned)sRuntime.activeCount);
            break;
        case OUTCOME_ENDED:
            snprintf(status, sizeof(status), "Ended - retry %u/%u", item,
                     (unsigned)sRuntime.activeCount);
            break;
        case OUTCOME_WRONG_ROUTE:
            snprintf(status, sizeof(status), "Wrong finish - retry %u/%u",
                     item, (unsigned)sRuntime.activeCount);
            break;
        case OUTCOME_INELIGIBLE:
            snprintf(status, sizeof(status), "Ineligible - retry %u/%u",
                     item, (unsigned)sRuntime.activeCount);
            break;
        case OUTCOME_WARPS_DISABLED:
            strcpy(status, "Stopped - warps disabled");
            break;
        default:
            return;
        }
    } else {
        switch ((Outcome)sRuntime.outcome) {
        case OUTCOME_SUCCESS:
            snprintf(status, sizeof(status), "%s %u/%u%s%s",
                     sRuntime.state == STATE_COMPLETE ? "Complete" : "Success",
                     (unsigned)sRuntime.progress, (unsigned)sRuntime.goal,
                     time[0] ? "  " : "", time);
            break;
        case OUTCOME_RESET:
            snprintf(status, sizeof(status), "Reset - streak 0/%u",
                     (unsigned)sRuntime.goal);
            break;
        case OUTCOME_ENDED:
            snprintf(status, sizeof(status), "Ended - streak 0/%u",
                     (unsigned)sRuntime.goal);
            break;
        case OUTCOME_WRONG_ROUTE:
            snprintf(status, sizeof(status), "Wrong finish - streak 0/%u",
                     (unsigned)sRuntime.goal);
            break;
        case OUTCOME_INELIGIBLE:
            snprintf(status, sizeof(status), "Ineligible - streak 0/%u",
                     (unsigned)sRuntime.goal);
            break;
        case OUTCOME_TARGET_MISS:
            snprintf(status, sizeof(status),
                     "Target missed - streak 0/%u%s%s",
                     (unsigned)sRuntime.goal, time[0] ? "  " : "", time);
            break;
        case OUTCOME_WARPS_DISABLED:
            strcpy(status, "Stopped - warps disabled");
            break;
        default:
            return;
        }
    }

    const int x = 150;
    const int y = 350;
    const int w = 340;
    const int h = 58;
    menu->fillBox(x, y, w, h, Color(8, 12, 20, 210));
    menu->fillBox(x, y, 4, h, accentColor());
    const char *name = ILing::label(sRuntime.lastEntry);
    int size = 15;
    while (size > 10 && Menu::textWidth(name, size) > w - 22) size--;
    menu->drawText(name, x + 12, y + 7, size, size,
                   Color(255, 255, 255, 255));
    menu->drawText(status, x + 12, y + 32, 13, 13,
                   Color(230, 236, 245, 255));
}

void drawCounter(Menu *menu) {
    char text[32];
    if (sRuntime.state == STATE_BLOCKED) {
        strcpy(text, "Stopped");
    } else if (sRuntime.mode == StageLoader::MODE_STREAKING) {
        snprintf(text, sizeof(text), "%u/%u",
                 (unsigned)sRuntime.progress, (unsigned)sRuntime.goal);
    } else if (sRuntime.state == STATE_COMPLETE) {
        strcpy(text, "Playlist complete");
    } else {
        snprintf(text, sizeof(text), "%u/%u",
                 (unsigned)sRuntime.progress,
                 (unsigned)sRuntime.activeCount);
    }
    gCreationExtras.drawStageSessionCounter(menu, text);
}

}  // namespace

namespace StageLoader {

void init() {
    resetAll();
}

int queueCount() {
    return sRuntime.draftCount;
}

int queueEntry(int position) {
    return position >= 0 && position < sRuntime.draftCount
               ? sRuntime.draftQueue[position]
               : -1;
}

bool appendQueue(int entry) {
    if (entry < 0 || entry >= ILing::count() || entry > 0xff ||
        sRuntime.draftCount >= QUEUE_CAPACITY) {
        return false;
    }
    // Repeated routes are distinct playlist positions by design.
    sRuntime.draftQueue[sRuntime.draftCount++] = (u8)entry;
    return true;
}

bool removeQueue(int position) {
    if (position < 0 || position >= sRuntime.draftCount) return false;
    for (int i = position + 1; i < sRuntime.draftCount; i++) {
        sRuntime.draftQueue[i - 1] = sRuntime.draftQueue[i];
    }
    sRuntime.draftQueue[--sRuntime.draftCount] = 0;
    return true;
}

bool moveQueue(int position, int direction) {
    const int destination = position +
        (direction < 0 ? -1 : direction > 0 ? 1 : 0);
    if (position < 0 || position >= sRuntime.draftCount ||
        destination < 0 || destination >= sRuntime.draftCount ||
        destination == position) {
        return false;
    }
    const u8 entry = sRuntime.draftQueue[position];
    sRuntime.draftQueue[position] = sRuntime.draftQueue[destination];
    sRuntime.draftQueue[destination] = entry;
    return true;
}

void clearQueue() {
    memset(sRuntime.draftQueue, 0, sizeof(sRuntime.draftQueue));
    sRuntime.draftCount = 0;
}

bool startLoader() {
    if (!sRuntime.draftCount || Ghost::observerPreparing() ||
        Ghost::observerStatsSuppressed()) {
        return false;
    }
    const u8 count = sRuntime.draftCount;
    resetSession();
    memcpy(sRuntime.activeQueue, sRuntime.draftQueue, count);
    sRuntime.activeCount = count;
    sRuntime.goal = count;
    sRuntime.mode = MODE_LOADER;
    if (requestCurrent()) return true;
    resetSession();
    return false;
}

bool startStreak(int entry, u16 finishes, s32 targetQf) {
    if (entry < 0 || entry >= ILing::count() || entry > 0xff ||
        finishes == 0 || targetQf < -1 || Ghost::observerPreparing() ||
        Ghost::observerStatsSuppressed()) {
        return false;
    }
    resetSession();
    sRuntime.activeQueue[0] = (u8)entry;
    sRuntime.activeCount = 1;
    sRuntime.goal = finishes;
    sRuntime.targetQf = targetQf;
    sRuntime.mode = MODE_STREAKING;
    if (requestCurrent()) return true;
    resetSession();
    return false;
}

bool start(int entry, u16 finishes, s32 targetQf) {
    return startStreak(entry, finishes, targetQf);
}

void cancel() {
    resetSession();
}

bool active() {
    return sRuntime.state != STATE_INACTIVE;
}

Mode mode() {
    return (Mode)sRuntime.mode;
}

void getStats(SessionStats *out) {
    if (!out) return;
    out->attempts = sRuntime.attempts;
    out->eligibleCompletes = sRuntime.eligibleCompletes;
    out->qualifyingSuccesses = sRuntime.qualifyingSuccesses;
    out->totalObservedActiveQf = sRuntime.totalObservedActiveQf;
    out->successfulAverageQf = sRuntime.qualifyingSuccesses
                                   ? (s32)(sRuntime.successfulQfTotal /
                                           sRuntime.qualifyingSuccesses)
                                   : -1;
    out->bestStreak = sRuntime.bestStreak;
    out->golds = sRuntime.golds;
}

bool modal() {
    return sRuntime.modalState == MODAL_VISIBLE;
}

bool resultOwnsInput() {
    return sRuntime.modalState == MODAL_VISIBLE ||
           (sRuntime.modalState == MODAL_PENDING &&
            liveResultDirector(gpMarDirector));
}

bool holdGameModeBeforeUpdate(TMarDirector *director) {
    const bool live = liveResultDirector(director);

    if (sRuntime.modalState == MODAL_VISIBLE) return live;
    if (sRuntime.modalState == MODAL_PENDING) {
        if (!live) return false;
        showModal();
        return true;
    }

    if ((sRuntime.state == STATE_RETRY_DELAY ||
         sRuntime.state == STATE_RETRY_PENDING) &&
        live &&
        (director->mGameState & 0x2)) {
        sRuntime.holdingDeparture = 1;
        return true;
    }
    if (sRuntime.state != STATE_RETRY_DELAY &&
        sRuntime.state != STATE_RETRY_PENDING) {
        sRuntime.holdingDeparture = 0;
    }
    return false;
}

void update() {
    if (sRuntime.modalState == MODAL_VISIBLE) {
        updateModal();
        return;
    }
    if (sRuntime.modalState == MODAL_PENDING) {
        if (liveResultDirector(gpMarDirector) &&
            (!gMenu || !gMenu->shown()) &&
            !WarpWheel::shown() && !WarpWheel::promptPending()) {
            showModal();
        }
        return;
    }

    observeLive();
    if (sRuntime.displayFrames > 0) sRuntime.displayFrames--;
    if ((sRuntime.state == STATE_COMPLETE &&
         sRuntime.mode == MODE_LOADER) ||
        sRuntime.state == STATE_BLOCKED) {
        if (sRuntime.displayFrames == 0) resetSession();
        return;
    }
    if (sRuntime.state == STATE_INACTIVE ||
        sRuntime.state == STATE_COMPLETE) {
        return;
    }

    if (sRuntime.state == STATE_RETRY_DELAY) {
        if (sRuntime.retryFrames > 0) sRuntime.retryFrames--;
        if (sRuntime.retryFrames == 0) {
            sRuntime.state = STATE_RETRY_PENDING;
        }
    }
    if (sRuntime.state != STATE_RETRY_PENDING || !safeToRetry()) return;
    requestCurrent();
}

void onILAttemptStarted(int entry) {
    if (!active() || sRuntime.state == STATE_COMPLETE ||
        sRuntime.state == STATE_BLOCKED) {
        return;
    }

    const u32 serial = gQFTTimer.attemptSerial();
    if (entry != expectedEntry()) {
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
    beginAttempt(serial);
}

void onILAttemptEnded() {
    if (sRuntime.state == STATE_RUNNING) {
        queueFailure(OUTCOME_ENDED, liveQf());
    } else if (sRuntime.state == STATE_WAITING) {
        sRuntime.state = STATE_RETRY_PENDING;
    }
}

void onILResult(int entry, s32 qf, bool eligible) {
    if (sRuntime.state != STATE_RUNNING) return;
    if (entry != expectedEntry()) {
        queueFailure(OUTCOME_WRONG_ROUTE, qf);
        return;
    }
    if (!eligible || qf < 0) {
        queueFailure(OUTCOME_INELIGIBLE, qf);
        return;
    }

    incrementSaturated(sRuntime.eligibleCompletes);
    if (sRuntime.mode == MODE_STREAKING && sRuntime.targetQf >= 0 &&
        qf > sRuntime.targetQf) {
        queueFailure(OUTCOME_TARGET_MISS, qf);
        return;
    }
    queueSuccess(qf);
}

void onILWarpCancelled() {
    if (sRuntime.state == STATE_WAITING) {
        sRuntime.retryFrames = 0;
        sRuntime.state = STATE_RETRY_PENDING;
    }
}

void draw(Menu *menu) {
    if (!menu) return;
    if (sRuntime.modalState == MODAL_VISIBLE) {
        drawFinalModal(menu);
        return;
    }
    if (menu->shown() || !active()) return;

    const u8 display = gSettings.get(SETTING_STAGE_SESSION_DISPLAY);
    if (display == 1) {
        drawCounter(menu);
    } else if (display == 0 &&
               (sRuntime.displayFrames > 0 ||
                sRuntime.state == STATE_BLOCKED)) {
        drawFullNotice(menu);
    }
}

}  // namespace StageLoader
