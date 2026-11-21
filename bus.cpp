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
#include <map>
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
const double kHoldMinGap = 60;     // unchanged estimate after this long = holding
const int kSoonSecs = 60;
const int kSoonStops = 1;
const size_t kMaxEntries = 12;
const size_t kMaxRouteLen = 4;

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

// Keeps printable ASCII only (the BDF fonts have no Hangul) and at most 4 chars.
std::string CleanRoute(const std::string &s) {
    std::string out;
    for (unsigned char ch : s) {
        if (ch >= 0x21 && ch <= 0x7E && out.size() < kMaxRouteLen) out += ch;
    }
    return out;
}

bool AllDigits(const std::string &s) {
    if (s.empty()) return false;
    for (unsigned char ch : s) {
        if (ch < '0' || ch > '9') return false;
    }
    return true;
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

bool RouteLess(const std::string &a, const std::string &b) {
    const bool da = AllDigits(a), db = AllDigits(b);
    if (da && db) {
        if (a.size() != b.size()) return a.size() < b.size();
        return a < b;
    }
    if (da != db) return da;  // numbers first
    return a < b;
}

std::string MinutesText(const BusEntry &e) {
    switch (e.style) {
    case EntryStyle::kSoon:   return "SOON";
    case EntryStyle::kPassed: return "--";
    default:                  return std::to_string(e.mins) + "MIN";
    }
}

const BusEntry *FindEntry(const BusBoard &board, const std::string &route) {
    for (const BusEntry &e : board.entries) {
        if (e.route == route) return &e;
    }
    return NULL;
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
            std::string route;
            if (!(in >> route >> b.secs >> b.stops)) continue;
            b.route = CleanRoute(route);
            if (!b.route.empty()) buses.push_back(b);
        }
    }

    snap->status = status;
    snap->label = label.empty() ? "BUS" : CleanRoute(label);
    if (ok) {
        // A bus waiting at its terminal keeps exactly the same estimate between
        // fetches. Counting it down locally would make "23MIN" drop to 22 and
        // jump back on every fetch, so such buses are marked as holding.
        if (snap->has_data && now - snap->fetched_at >= kHoldMinGap) {
            for (BusArrival &b : buses) {
                for (const BusArrival &p : snap->buses) {
                    if (p.route == b.route && p.secs == b.secs && p.stops == b.stops) {
                        b.holding = true;
                        break;
                    }
                }
            }
        }
        std::sort(buses.begin(), buses.end(),
                  [](const BusArrival &a, const BusArrival &b) { return a.secs < b.secs; });
        snap->buses = buses;
        snap->has_data = true;
        snap->fetched_at = now;
    } else if (status == BusStatus::kNoConfig) {
        snap->has_data = false;
        snap->buses.clear();
    }
    // Other errors keep the previous good data; ComputeBusBoard ages it out.
    return true;
}

BusBoard ComputeBusBoard(const BusSnapshot &snap, double now) {
    BusBoard board;
    const bool hard_error = snap.status == BusStatus::kAuthError ||
                            snap.status == BusStatus::kLimitError;
    const double age = now - snap.fetched_at;

    if (!snap.has_data || age > kMaxAge) {
        if (hard_error) {
            board.state = BoardState::kError;
            board.error = snap.status;
        }
        return board;  // otherwise hidden: classic clock
    }
    if (snap.buses.empty()) return board;  // no bus in service: classic clock

    // snap.buses is sorted by secs, so the first live bus of a route is its next one.
    std::map<std::string, BusEntry> by_route;
    for (const BusArrival &b : snap.buses) {
        const double left = b.holding ? b.secs : b.secs - age;
        auto it = by_route.find(b.route);
        if (left < -kPassedGrace) {
            if (it == by_route.end()) by_route[b.route] = BusEntry{ b.route, 0, EntryStyle::kPassed };
            continue;
        }
        if (it != by_route.end() && it->second.style != EntryStyle::kPassed) continue;

        BusEntry e;
        e.route = b.route;
        e.mins = std::max(0, std::min(99, static_cast<int>(left / 60)));
        if ((b.stops >= 0 && b.stops <= kSoonStops) || left < kSoonSecs) {
            e.style = EntryStyle::kSoon;
        } else {
            e.style = age > kStaleAge ? EntryStyle::kStale : EntryStyle::kNormal;
        }
        by_route[b.route] = e;
    }

    for (const auto &kv : by_route) board.entries.push_back(kv.second);
    std::sort(board.entries.begin(), board.entries.end(),
              [](const BusEntry &a, const BusEntry &b) { return RouteLess(a.route, b.route); });
    if (board.entries.size() > kMaxEntries) board.entries.resize(kMaxEntries);
    board.state = BoardState::kData;
    return board;
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

void BusFeed::SetEnabled(bool on) {
    std::lock_guard<std::mutex> l(mu_);
    if (enabled_ == on) return;
    enabled_ = on;
    fprintf(stderr, "[bus] polling %s\n", on ? "resumed" : "paused (display mode off)");
    cv_.notify_all();
}

BusSnapshot BusFeed::Snapshot() {
    std::lock_guard<std::mutex> l(mu_);
    return snap_;
}

bool BusFeed::WaitTurn() {
    std::unique_lock<std::mutex> l(mu_);
    for (;;) {
        if (stop_) return false;
        if (enabled_) {
            const double wait = next_at_ - MonotonicSeconds();
            if (wait <= 0) return true;
            cv_.wait_for(l, std::chrono::duration<double>(wait));
        } else {
            cv_.wait(l);
        }
    }
}

void BusFeed::Run() {
    int failures = 0;
    int day = -1;
    int calls_today = 0;
    BusStatus logged_status = BusStatus::kWaiting;
    size_t logged_count = static_cast<size_t>(-1);

    while (WaitTurn()) {
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
            next_at_ = MonotonicSeconds() + next;
        }

        if (snap.status != logged_status || snap.buses.size() != logged_count) {
            fprintf(stderr, "[bus] %s, %zu bus(es), %d call(s) today, next in %.0fs\n",
                    StatusName(snap.status), snap.buses.size(), calls_today, next);
            logged_status = snap.status;
            logged_count = snap.buses.size();
        }
    }
}
