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

#include <time.h>

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

// From 노선정보조회 (busfetch.py --routes): index 0 weekday, 1 Saturday, 2 Sunday.
struct RouteInfo {
    std::string route;
    int headway[3];  // minutes, 0 = unknown
    int last[3];     // last departure HHMM (00:30 = 30 = after midnight), 0 = unknown
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
    std::vector<RouteInfo> routes;  // kept across fetches; refreshed by --routes runs
    double routes_at = -1;    // MonotonicSeconds() of the last ROUTE lines, -1 = never
};

// What the screens show: one entry per route, sorted by RouteLess().
enum class BoardState {
    kHidden,   // nothing to show (mode 1: the clock; modes 2/3: "--" in the bus area)
    kWaiting,  // no answer since the start yet (modes 2/3: "..." in the bus area)
    kData,     // `entries` is valid
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
    int mins = 0;  // 0..99
    EntryStyle style = EntryStyle::kNormal;
    // The bus after it: a real second bus in the answer, or an estimate from the
    // route's headway (only before its last departure). Estimates are kWait.
    bool has_next = false;
    int next_mins = 0;
    EntryStyle next_style = EntryStyle::kWait;
    bool next_estimated = false;
};

// The bus a screen should show for a route, given a minimum lead time.
struct BusPick {
    int mins;
    EntryStyle style;
    bool second;     // it is the bus after the nearest one
    bool estimated;  // from the headway, not from a real bus
};

// The first bus that is at least `min_mins` away and not SOON/passed, else the
// next one under the same rule. min_mins 0: the nearest live bus (SOON allowed),
// else the next one. Returns false if there is none.
bool CatchableBus(const BusEntry &e, int min_mins, BusPick *out);

struct BusBoard {
    BoardState state = BoardState::kHidden;
    BusStatus error = BusStatus::kOk;  // set when state == kError
    std::vector<BusEntry> entries;
};

double MonotonicSeconds();

// Parses the stdout of busfetch.py into `snap` (fetched_at = now on success).
// Returns false if the output does not follow the protocol.
bool ParseFetchOutput(const std::string &out, double now, BusSnapshot *snap);

// Decides what can be shown at `now` (monotonic) / `wall` (local time, for the
// day type and the last departure). Pure function, shared by every screen.
BusBoard ComputeBusBoard(const BusSnapshot &snap, double now, const struct tm &wall);

// Route order on screen: numbers by value first ("11" < "102"), then text.
bool RouteLess(const std::string &a, const std::string &b);

// "23M", "SOON" or "--".
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
    // routes_cmd: the helper with --routes (also fetches headways). It is used
    // for the first run, then when the route table is over 6 h old or a route is
    // missing from it (at most every 30 min); empty = never.
    BusFeed(const std::string &helper_cmd, const std::string &conf_path,
            double start_delay = 20, const std::string &routes_cmd = "");
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
    const std::string routes_cmd_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    FeedPause pause_ = FeedPause::kNone;
    double next_at_ = 0;           // MonotonicSeconds() of the next allowed run
    int runs_ = 0;
    int calls_today_ = 0;
    BusSnapshot snap_;             // status kWaiting until the first helper run
};

#endif  // CLOCK_BUS_H
