/*
 * Raspberry Pi LED Matrix Clock - bus arrival feed
 *
 * Copyright (C) 2026 Ksyear
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "bus.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

namespace {

// Polling (calls to busfetch.py; each call is one API request).
const double kPollOk = 90;         // a bus is listed
const double kPollEmpty = 300;     // no bus in service (night)
const double kPollAuthError = 600;
const double kPollLimitError = 1800;
const double kPollNoConfig = 30;   // waiting for ./bus.conf to appear
const int kDailyCap = 950;         // safety net below a 1,000/day traffic limit

// Display rules.
const double kStaleAge = 150;      // older data is dimmed
const double kMaxAge = 300;        // older data is not shown at all
const double kPassedGrace = 30;    // keep a bus this long after its estimated arrival
const int kSoonSecs = 60;
const int kSoonStops = 1;

const char *StatusName(BusStatus s) {
    switch (s) {
    case BusStatus::kNoConfig:    return "no-config";
    case BusStatus::kWaiting:     return "waiting";
    case BusStatus::kOk:          return "ok";
    case BusStatus::kAuthError:   return "auth-error";
    case BusStatus::kLimitError:  return "limit-error";
    case BusStatus::kNetError:    return "net-error";
    case BusStatus::kOtherError:  return "error";
    }
    return "?";
}

BusStatus StatusFromKind(const std::string &kind) {
    if (kind == "CONF")  return BusStatus::kNoConfig;
    if (kind == "AUTH")  return BusStatus::kAuthError;
    if (kind == "LIMIT") return BusStatus::kLimitError;
    if (kind == "NET")   return BusStatus::kNetError;
    return BusStatus::kOtherError;
}

// Runs `cmd` through /bin/sh and collects its stdout (stderr goes to the journal).
std::string RunCommand(const std::string &cmd) {
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    if (p == NULL) return out;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0 && out.size() < 8192) {
        out.append(buf, n);
    }
    pclose(p);
    return out;
}

}  // namespace

double MonotonicSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

bool ParseFetchOutput(const std::string &out, double now, BusSnapshot *snap) {
    std::istringstream lines(out);
    std::string line, word;
    if (!std::getline(lines, line)) return false;

    std::istringstream head(line);
    head >> word;
    bool ok;
    BusStatus status;
    if (word == "OK") {
        ok = true;
        status = BusStatus::kOk;
    } else if (word == "ERR") {
        std::string kind;
        head >> kind;
        ok = false;
        status = StatusFromKind(kind);
    } else {
        return false;
    }

    std::vector<BusArrival> buses;
    std::string label = snap->label;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        in >> word;
        if (word == "LABEL") {
            in >> label;
        } else if (word == "BUS") {
            BusArrival b;
            if (in >> b.route >> b.secs >> b.stops) buses.push_back(b);
        }
    }

    snap->status = status;
    snap->label = label.empty() ? "BUS" : label;
    if (ok) {
        std::sort(buses.begin(), buses.end(),
                  [](const BusArrival &a, const BusArrival &b) { return a.secs < b.secs; });
        snap->buses = buses;
        snap->has_data = true;
        snap->fetched_at = now;
    } else if (status == BusStatus::kNoConfig) {
        snap->has_data = false;
        snap->buses.clear();
    }
    // Other errors keep the previous good data; ComputeBusView ages it out.
    return true;
}

BusView ComputeBusView(const BusSnapshot &snap, double now) {
    BusView v;
    const bool hard_error = snap.status == BusStatus::kAuthError ||
                            snap.status == BusStatus::kLimitError;
    const double age = now - snap.fetched_at;

    if (!snap.has_data || age > kMaxAge) {
        if (hard_error) {
            v.style = BusStyle::kError;
            v.route = snap.label;
            v.text = "ERR";
        }
        return v;  // otherwise hidden: classic clock
    }
    if (snap.buses.empty()) return v;  // no bus in service: classic clock

    for (const BusArrival &b : snap.buses) {
        const double left = b.secs - age;
        if (left < -kPassedGrace) continue;  // this one has already passed
        v.route = b.route;
        if ((b.stops >= 0 && b.stops <= kSoonStops) || left < kSoonSecs) {
            v.style = BusStyle::kSoon;
            v.text = "SOON";
        } else {
            const int mins = std::min(99, static_cast<int>(left / 60));
            v.style = age > kStaleAge ? BusStyle::kStale : BusStyle::kNormal;
            v.text = std::to_string(mins) + "MIN";
        }
        return v;
    }
    v.style = BusStyle::kNone;
    v.route = snap.label;
    v.text = "--";
    return v;
}

BusFeed::BusFeed(const std::string &helper_cmd, const std::string &conf_path)
    : helper_cmd_(helper_cmd), conf_path_(conf_path) {}

void BusFeed::Start() {
    std::thread(&BusFeed::Run, this).detach();
}

void BusFeed::Stop() {
    std::lock_guard<std::mutex> l(mu_);
    stop_ = true;
    cv_.notify_all();
}

BusSnapshot BusFeed::Snapshot() {
    std::lock_guard<std::mutex> l(mu_);
    return snap_;
}

bool BusFeed::Wait(double seconds) {
    std::unique_lock<std::mutex> l(mu_);
    return !cv_.wait_for(l, std::chrono::duration<double>(seconds),
                         [this] { return stop_; });
}

void BusFeed::Run() {
    int failures = 0;
    int day = -1;
    int calls_today = 0;
    BusStatus logged_status = BusStatus::kWaiting;
    size_t logged_count = static_cast<size_t>(-1);

    for (;;) {
        BusSnapshot snap = Snapshot();  // only this thread writes snap_
        double next;

        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        if (lt.tm_yday != day) {
            day = lt.tm_yday;
            calls_today = 0;
        }

        if (access(conf_path_.c_str(), R_OK) != 0) {
            snap.status = BusStatus::kNoConfig;
            snap.has_data = false;
            snap.buses.clear();
            next = kPollNoConfig;
        } else if (calls_today >= kDailyCap) {
            snap.status = BusStatus::kLimitError;
            next = 60;  // re-check for the date change
        } else {
            ++calls_today;
            const std::string out = RunCommand(helper_cmd_);
            if (!ParseFetchOutput(out, MonotonicSeconds(), &snap)) {
                snap.status = BusStatus::kOtherError;
            }
            switch (snap.status) {
            case BusStatus::kOk:
                failures = 0;
                next = snap.buses.empty() ? kPollEmpty : kPollOk;
                break;
            case BusStatus::kNoConfig:
                --calls_today;  // no API request was made
                next = kPollNoConfig;
                break;
            case BusStatus::kAuthError:   next = kPollAuthError; break;
            case BusStatus::kLimitError:  next = kPollLimitError; break;
            default:
                // 15 s, 30 s, 60 s, then the normal interval.
                next = std::min(kPollOk, 15.0 * (1 << std::min(failures, 3)));
                ++failures;
                break;
            }
        }

        {
            std::lock_guard<std::mutex> l(mu_);
            if (stop_) return;
            snap_ = snap;
        }

        if (snap.status != logged_status || snap.buses.size() != logged_count) {
            fprintf(stderr, "[bus] %s, %zu bus(es), %d call(s) today, next in %.0fs\n",
                    StatusName(snap.status), snap.buses.size(), calls_today, next);
            logged_status = snap.status;
            logged_count = snap.buses.size();
        }

        if (!Wait(next)) return;
    }
}
