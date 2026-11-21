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

struct BusArrival {
    std::string route;  // e.g. "102"
    int secs;           // seconds until arrival at the time of the fetch
    int stops;          // remaining stops (-1 = unknown)
};

enum class BusStatus {
    kNoConfig,    // ./bus.conf missing or has no key -> feature off
    kWaiting,     // config present, first answer not received yet
    kOk,
    kAuthError,   // key not registered / not approved
    kLimitError,  // daily traffic limit exceeded
    kNetError,    // network / timeout / HTTP error
    kOtherError,  // parse error, unexpected API answer
};

struct BusSnapshot {
    BusStatus status = BusStatus::kNoConfig;  // result of the latest attempt
    bool has_data = false;    // `buses` and `fetched_at` hold the last good answer
    double fetched_at = 0;    // MonotonicSeconds() of the last good answer
    std::vector<BusArrival> buses;  // sorted by secs
    std::string label = "BUS";      // route label to show without a bus line
};

enum class BusStyle {
    kHidden,  // draw the classic clock (HH:MM:SS)
    kNormal,  // "12MIN"
    kStale,   // "12MIN", dimmed: data is getting old
    kSoon,    // "SOON", blinking: next stop or < 1 minute
    kNone,    // "--": data is fresh but every listed bus has passed
    kError,   // "ERR": key or traffic limit problem
};

struct BusView {
    BusStyle style = BusStyle::kHidden;
    std::string route;  // top line, e.g. "102"
    std::string text;   // bottom line, e.g. "3MIN"
};

double MonotonicSeconds();

// Parses the stdout of busfetch.py into `snap` (fetched_at = now on success).
// Returns false if the output does not follow the protocol.
bool ParseFetchOutput(const std::string &out, double now, BusSnapshot *snap);

// Decides what the bus area shows. Pure function, used by the clock and preview.
BusView ComputeBusView(const BusSnapshot &snap, double now);

// Runs the helper command in a background thread and keeps the latest snapshot.
// The render loop only calls Snapshot(), which copies a few bytes under a mutex.
class BusFeed {
public:
    BusFeed(const std::string &helper_cmd, const std::string &conf_path);
    void Start();      // starts a detached thread
    void Stop();       // asks the thread to finish; does not wait for it
    BusSnapshot Snapshot();

private:
    void Run();
    bool Wait(double seconds);  // false if Stop() was called

    const std::string helper_cmd_;
    const std::string conf_path_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    BusSnapshot snap_;
};

#endif  // CLOCK_BUS_H
