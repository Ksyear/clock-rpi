/*
 * Raspberry Pi LED Matrix Clock - bus arrival feed
 *
 * Copyright (C) 2026 Ksyear
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef CLOCK_BUS_H
#define CLOCK_BUS_H

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "settings.h"

struct BusArrival {
    std::string route;     // e.g. "102" (printable ASCII, at most 4 characters)
    int secs;              // seconds until arrival at the time of the fetch
    int stops;             // remaining stops (-1 = unknown)
    bool waiting = false;  // still at its terminal: `secs` is a timetable estimate
    int min_secs = 0;      // waiting: never show less than this (the drive here)
};

enum class BusStatus {
    kNoConfig,    // ./bus.conf missing or has no key -> no bus data
    kWaiting,     // config present, first answer not received yet
    kOk,
    kAuthError,   // key not registered / not approved
    kLimitError,  // daily traffic limit exceeded (API or our own cap)
    kNetError,    // network / timeout / HTTP error
    kOtherError,  // parse error, unexpected API answer
};

struct BusSnapshot {
    BusStatus status = BusStatus::kNoConfig;  // result of the latest attempt
    bool has_data = false;    // `buses` and `fetched_at` hold the last good answer
    double fetched_at = 0;    // MonotonicSeconds() of the last good answer
    std::vector<BusArrival> buses;  // sorted by secs
    std::string label = "BUS";
    std::string via;          // API that answered ("daejeon", "tago")
    std::string notes;        // NOTE lines from the helper, space separated
};

// What the screens show: one entry per route, sorted by RouteLess().
enum class BoardState {
    kHidden,  // nothing to show: draw the classic clock
    kData,    // `entries` is valid
    kError,   // key or traffic-limit problem and no usable data
};

enum class EntryStyle {
    kNormal,  // minutes left
    kStale,   // minutes left, but the data is getting old (dimmed)
    kSoon,    // next stop or under a minute (blinking)
    kPassed,  // every listed bus of this route has passed; waiting for new data
    kWait,    // still at its terminal: minutes from the timetable (own color)
};

struct BusEntry {
    std::string route;
    int mins;  // 0..99
    EntryStyle style;
};

struct BusBoard {
    BoardState state = BoardState::kHidden;
    BusStatus error = BusStatus::kOk;  // set when state == kError
    std::vector<BusEntry> entries;
};

double MonotonicSeconds();

// Parses the stdout of busfetch.py into `snap` (fetched_at = now on success).
// Returns false if the output does not follow the protocol.
bool ParseFetchOutput(const std::string &out, double now, BusSnapshot *snap);

// Decides what can be shown at `now`. Pure function, shared by every screen.
BusBoard ComputeBusBoard(const BusSnapshot &snap, double now);

// Route order on screen: numbers by value first ("11" < "102"), then text.
bool RouteLess(const std::string &a, const std::string &b);

// "23MIN", "SOON" or "--".
std::string MinutesText(const BusEntry &e);

// Entry for `route`, or NULL.
const BusEntry *FindEntry(const BusBoard &board, const std::string &route);

// Why the feed is not polling right now.
enum class FeedPause {
    kNone,
    kModeOff,        // ./clockmode 0
    kClockUnsynced,  // boot before NTP: the wall clock (quiet hours) can't be trusted
    kQuietHours,     // e.g. 01:00-05:00, starting 30 s early so no helper is running
};

const char *PauseName(FeedPause p);

// Pure decision used by the render loop every frame.
FeedPause FeedPauseFor(const DisplaySettings &s, int secs_of_day, bool clock_ok);

// Runs the helper command in a background thread and keeps the latest snapshot.
// The render loop only calls Snapshot(), which copies a few bytes under a mutex.
class BusFeed {
public:
    // start_delay: seconds before the first helper run, so a crash/restart loop
    // cannot turn into an API request loop.
    BusFeed(const std::string &helper_cmd, const std::string &conf_path,
            double start_delay = 20);
    void Start();                  // starts a detached thread
    void Stop();                   // asks the thread to finish; does not wait for it
    void SetPause(FeedPause p, const std::string &why = "");  // no helper runs while paused
    BusSnapshot Snapshot();
    int Runs();                    // helper runs so far (tests)

private:
    void Run();
    bool WaitTurn();               // false if Stop() was called

    const std::string helper_cmd_;
    const std::string conf_path_;
    const double start_delay_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    FeedPause pause_ = FeedPause::kNone;
    double next_at_ = 0;           // MonotonicSeconds() of the next allowed run
    int runs_ = 0;
    int calls_today_ = 0;
    BusSnapshot snap_;
};

#endif  // CLOCK_BUS_H
