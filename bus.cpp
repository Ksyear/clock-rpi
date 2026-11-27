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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <thread>

namespace {

// Polling (calls to busfetch.py; each call is one API request, two if the
// fallback API is also tried — each API has its own 10,000/day limit).
const double kPollOk = 90;         // a bus is listed
const double kPollEmpty = 300;     // no bus in service
const double kPollAuthError = 600;
const double kPollLimitError = 1800;
const double kPollNoConfig = 30;   // waiting for ./bus.conf to appear
const int kDailyCap = 2000;        // ~2.5x normal use (~800/day), far below 10,000

// Display rules.
const double kStaleAge = 150;      // older data is dimmed
const double kMaxAge = 300;        // older data is not shown at all
const double kPassedGrace = 30;    // keep a bus this long after its estimated arrival
const int kSoonSecs = 60;
const int kSoonStops = 1;
const size_t kMaxEntries = 12;
const size_t kMaxRouteLen = 4;
const int kMaxSecs = 86400;
const int kMaxStops = 500;

// Next-bus estimate from the headway.
const int kMinHeadway = 3;          // minutes; outside 3..180 the value is ignored
const int kMaxHeadway = 180;
const int kLastGrace = 300;         // seconds after the last departure still allowed
const int kServiceDayStart = 4 * 3600;  // 00:00-03:59 belongs to the previous day

// Route table refresh (busfetch.py --routes).
const double kRoutesMaxAge = 6 * 3600;
const double kRoutesRetry = 1800;

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

// Keeps printable ASCII only (the BDF fonts have no Hangul) and at most `max` chars.
std::string CleanWord(const std::string &s, size_t max = kMaxRouteLen) {
    std::string out;
    for (unsigned char ch : s) {
        if (ch >= 0x21 && ch <= 0x7E && out.size() < max) out += ch;
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

// 0 weekday, 1 Saturday, 2 Sunday (public holidays are not known: weekday).
int DayIndex(int wday) { return wday == 0 ? 2 : (wday == 6 ? 1 : 0); }

// Seconds of the service day for an HHMM (after-midnight times count as 24:xx).
int ServiceSecs(int hhmm) {
    const int secs = (hhmm / 100) * 3600 + (hhmm % 100) * 60;
    return hhmm < 400 ? secs + 86400 : secs;
}

const RouteInfo *FindRoute(const std::vector<RouteInfo> &routes, const std::string &route) {
    for (const RouteInfo &r : routes) {
        if (r.route == route) return &r;
    }
    return NULL;
}

// Seconds left and style of one bus; false if it has passed (`left` is still set).
bool Evaluate(const BusArrival &b, double age, double *left, EntryStyle *style) {
    if (b.waiting) {
        // Timetable estimate: counts down to its departure, then stays at the
        // drive time until the bus really leaves (no SOON, never "passed").
        *left = std::max<double>(b.secs - age, b.min_secs);
        *style = EntryStyle::kWait;
        return true;
    }
    *left = b.secs - age;
    if (*left < -kPassedGrace) return false;
    if ((b.stops >= 0 && b.stops <= kSoonStops) || *left < kSoonSecs) {
        *style = EntryStyle::kSoon;
    } else {
        *style = age > kStaleAge ? EntryStyle::kStale : EntryStyle::kNormal;
    }
    return true;
}

int Mins(double left) { return std::max(0, std::min(99, static_cast<int>(left / 60))); }

std::string RoutesText(const std::vector<RouteInfo> &routes) {
    std::vector<RouteInfo> sorted = routes;
    std::sort(sorted.begin(), sorted.end(),
              [](const RouteInfo &a, const RouteInfo &b) { return RouteLess(a.route, b.route); });
    std::string out;
    for (const RouteInfo &r : sorted) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s%s=%d/%d/%d", out.empty() ? "" : " ", r.route.c_str(),
                 r.headway[0], r.headway[1], r.headway[2]);
        out += buf;
    }
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
    default:                  return std::to_string(e.mins) + "M";
    }
}

bool CatchableBus(const BusEntry &e, int min_mins, BusPick *out) {
    auto ok = [min_mins](int mins, EntryStyle style) {
        if (style == EntryStyle::kPassed) return false;
        if (min_mins <= 0) return true;
        return style != EntryStyle::kSoon && mins >= min_mins;
    };
    if (ok(e.mins, e.style)) {
        *out = BusPick{ e.mins, e.style, false, false };
        return true;
    }
    if (e.has_next && ok(e.next_mins, e.next_style)) {
        *out = BusPick{ e.next_mins, e.next_style, true, e.next_estimated };
        return true;
    }
    return false;
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
    long expected = -1;
    if (word == "OK") {
        ok = true;
        status = BusStatus::kOk;
        if (!(head >> expected) || expected < 0 || expected > 100) return false;
    } else if (word == "ERR") {
        std::string kind;
        head >> kind;
        ok = false;
        status = StatusFromKind(kind);
    } else {
        return false;
    }

    std::vector<BusArrival> buses;
    std::vector<RouteInfo> routes;
    std::string label = snap->label, via, notes;
    long bus_lines = 0;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        word.clear();
        in >> word;
        if (word == "LABEL") {
            in >> label;
        } else if (word == "VIA") {
            in >> via;
        } else if (word == "NOTE") {
            std::string note;
            in >> note;
            if (!notes.empty()) notes += ' ';
            notes += CleanWord(note, 40);
        } else if (word == "ROUTE") {
            RouteInfo r;
            std::string route;
            if (!(in >> route >> r.headway[0] >> r.headway[1] >> r.headway[2] >>
                  r.last[0] >> r.last[1] >> r.last[2])) {
                continue;
            }
            r.route = CleanWord(route);
            bool valid = !r.route.empty();
            for (int i = 0; i < 3; i++) {
                if (r.headway[i] < 0 || r.headway[i] > 600) r.headway[i] = 0;
                if (r.last[i] < 0 || r.last[i] > 2359 || r.last[i] % 100 > 59) r.last[i] = 0;
            }
            if (valid) routes.push_back(r);
        } else if (word == "BUS") {
            ++bus_lines;
            BusArrival b;
            std::string route, kind;
            if (!(in >> route >> b.secs >> b.stops)) continue;
            if (in >> kind && kind == "WAIT") {  // older helpers print no kind (= RUN)
                b.waiting = true;
                if (!(in >> b.min_secs)) b.min_secs = 0;
            }
            b.route = CleanWord(route);
            if (b.route.empty() || b.secs < 0 || b.secs > kMaxSecs ||
                b.stops < -1 || b.stops > kMaxStops) {
                continue;
            }
            b.min_secs = std::max(0, std::min(b.min_secs, b.secs));
            buses.push_back(b);
        }
    }
    if (ok && bus_lines != expected) return false;  // truncated or garbled output

    snap->status = status;
    snap->label = label.empty() ? "BUS" : CleanWord(label);
    if (ok) {
        std::sort(buses.begin(), buses.end(),
                  [](const BusArrival &a, const BusArrival &b) { return a.secs < b.secs; });
        snap->buses = buses;
        snap->has_data = true;
        snap->fetched_at = now;
        snap->via = CleanWord(via, 12);
        snap->notes = notes;
        for (const RouteInfo &r : routes) {  // merge: keep routes this answer did not cover
            bool replaced = false;
            for (RouteInfo &old : snap->routes) {
                if (old.route == r.route) {
                    old = r;
                    replaced = true;
                }
            }
            if (!replaced && snap->routes.size() < 32) snap->routes.push_back(r);
        }
        if (!routes.empty()) snap->routes_at = now;
    } else if (status == BusStatus::kNoConfig) {
        snap->has_data = false;
        snap->buses.clear();
    }
    // Other errors keep the previous good data; ComputeBusBoard ages it out.
    return true;
}

BusBoard ComputeBusBoard(const BusSnapshot &snap, double now, const struct tm &wall) {
    BusBoard board;
    const bool hard_error = snap.status == BusStatus::kAuthError ||
                            snap.status == BusStatus::kLimitError;
    const double age = now - snap.fetched_at;

    if (!snap.has_data || age > kMaxAge) {
        if (hard_error) {
            board.state = BoardState::kError;
            board.error = snap.status;
        } else if (!snap.has_data && snap.status != BusStatus::kNoConfig) {
            board.state = BoardState::kWaiting;  // starting up, or no answer yet
        }
        return board;  // otherwise hidden: nothing to show
    }
    if (snap.buses.empty()) return board;  // no bus in service

    // The service day: 00:00-03:59 still belongs to the previous day's timetable.
    int sod = wall.tm_hour * 3600 + wall.tm_min * 60 + wall.tm_sec;
    int wday = wall.tm_wday;
    if (sod < kServiceDayStart) {
        sod += 86400;
        wday = (wday + 6) % 7;
    }
    const int day = DayIndex(wday);

    // snap.buses is sorted by secs, so per route they come soonest first.
    std::map<std::string, std::vector<const BusArrival *>> by_route;
    for (const BusArrival &b : snap.buses) by_route[b.route].push_back(&b);

    for (const auto &kv : by_route) {
        BusEntry e;
        e.route = kv.first;
        bool have_first = false;
        double first_left = 0, last_left = 0;
        for (const BusArrival *b : kv.second) {
            double left;
            EntryStyle style;
            const bool live = Evaluate(*b, age, &left, &style);
            last_left = left;
            if (!live) continue;
            if (!have_first) {
                have_first = true;
                first_left = left;
                e.mins = Mins(left);
                e.style = style;
            } else if (!e.has_next) {  // a real second bus in the answer
                e.has_next = true;
                e.next_mins = Mins(left);
                e.next_style = style;
            }
        }
        if (!have_first) e.style = EntryStyle::kPassed;

        // Otherwise estimate the next bus from the headway, but not after the
        // route's last departure of the day.
        const RouteInfo *ri = FindRoute(snap.routes, e.route);
        const int headway = ri ? ri->headway[day] : 0;
        if (!e.has_next && headway >= kMinHeadway && headway <= kMaxHeadway) {
            const double next_left = (have_first ? first_left : last_left) + headway * 60.0;
            const int last = ri->last[day];
            const bool in_service = last <= 0 || sod + next_left <= ServiceSecs(last) + kLastGrace;
            if (next_left >= 60 && in_service) {
                e.has_next = true;
                e.next_mins = Mins(next_left);
                e.next_style = EntryStyle::kWait;
                e.next_estimated = true;
            }
        }
        board.entries.push_back(e);
    }

    std::sort(board.entries.begin(), board.entries.end(),
              [](const BusEntry &a, const BusEntry &b) { return RouteLess(a.route, b.route); });
    if (board.entries.size() > kMaxEntries) board.entries.resize(kMaxEntries);
    board.state = BoardState::kData;
    return board;
}

const char *PauseName(FeedPause p) {
    switch (p) {
    case FeedPause::kNone:          return "none";
    case FeedPause::kModeOff:       return "display mode off";
    case FeedPause::kClockUnsynced: return "waiting for the clock to sync";
    case FeedPause::kQuietHours:    return "quiet hours";
    }
    return "?";
}

FeedPause FeedPauseFor(const DisplaySettings &s, int secs_of_day, bool clock_ok) {
    if (s.mode == DisplayMode::kOff) return FeedPause::kModeOff;
    if (!clock_ok) return FeedPause::kClockUnsynced;
    // 30 s early: a helper lives at most 25 s, so none is running when the window opens.
    if (InQuietHours(s, secs_of_day) || InQuietHours(s, (secs_of_day + 30) % 86400)) {
        return FeedPause::kQuietHours;
    }
    return FeedPause::kNone;
}

BusFeed::BusFeed(const std::string &helper_cmd, const std::string &conf_path,
                 double start_delay, const std::string &routes_cmd)
    : helper_cmd_(helper_cmd), conf_path_(conf_path), start_delay_(start_delay),
      routes_cmd_(routes_cmd) {
    snap_.status = BusStatus::kWaiting;  // the screens show "..." until the first answer
}

void BusFeed::Start() {
    {
        std::lock_guard<std::mutex> l(mu_);
        next_at_ = MonotonicSeconds() + start_delay_;
    }
    std::thread(&BusFeed::Run, this).detach();
}

void BusFeed::Stop() {
    std::lock_guard<std::mutex> l(mu_);
    stop_ = true;
    cv_.notify_all();
}

void BusFeed::SetPause(FeedPause p, const std::string &why) {
    std::lock_guard<std::mutex> l(mu_);
    if (pause_ == p) return;
    if (p == FeedPause::kNone) {
        fprintf(stderr, "[bus] polling resumed, %d call(s) today\n", calls_today_);
    } else {
        fprintf(stderr, "[bus] polling paused (%s%s%s), %d call(s) today\n", PauseName(p),
                why.empty() ? "" : " ", why.c_str(), calls_today_);
    }
    pause_ = p;
    cv_.notify_all();
}

BusSnapshot BusFeed::Snapshot() {
    std::lock_guard<std::mutex> l(mu_);
    return snap_;
}

int BusFeed::Runs() {
    std::lock_guard<std::mutex> l(mu_);
    return runs_;
}

bool BusFeed::WaitTurn() {
    std::unique_lock<std::mutex> l(mu_);
    for (;;) {
        if (stop_) return false;
        if (pause_ == FeedPause::kNone) {
            const double wait = next_at_ - MonotonicSeconds();
            if (wait <= 0) return true;
            cv_.wait_for(l, std::chrono::duration<double>(wait));
        } else {
            cv_.wait(l);  // paused: no wake-ups, no helper, no requests
        }
    }
}

void BusFeed::Run() {
    // The library drops root to `daemon` before this thread starts. If that did
    // not happen (e.g. --led-no-drop-privs), never run network code as root.
    if (geteuid() == 0) {
        fprintf(stderr, "[bus] helper disabled: running as root "
                        "(start without --led-no-drop-privs)\n");
        return;
    }

    int failures = 0;
    int day = -1;
    bool cap_logged = false;
    std::string logged;
    std::string logged_routes;
    double routes_try = -1;  // MonotonicSeconds() of the last --routes run

    while (WaitTurn()) {
        BusSnapshot snap = Snapshot();  // only this thread writes snap_
        double next;
        std::string detail;

        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        if (lt.tm_yday != day) {
            day = lt.tm_yday;
            std::lock_guard<std::mutex> l(mu_);
            calls_today_ = 0;
            cap_logged = false;
        }

        if (access(conf_path_.c_str(), R_OK) != 0) {
            detail = std::string(" (") + conf_path_ + ": " + strerror(errno) + ")";
            snap.status = BusStatus::kNoConfig;
            snap.has_data = false;
            snap.buses.clear();
            next = kPollNoConfig;
        } else if (calls_today_ >= kDailyCap) {
            if (!cap_logged) {
                fprintf(stderr, "[bus] daily self-cap reached (%d runs), waiting for midnight\n",
                        kDailyCap);
                cap_logged = true;
            }
            snap.status = BusStatus::kLimitError;
            next = 60;  // re-check for the date change
        } else {
            {
                std::lock_guard<std::mutex> l(mu_);
                ++calls_today_;
                ++runs_;
            }
            // Headways (for the next-bus estimate) are fetched rarely: first run,
            // then when the table is 6 h old or lacks a route (every 30 min at most).
            const double t0 = MonotonicSeconds();
            bool missing = false;
            for (const BusArrival &b : snap.buses) {
                if (FindRoute(snap.routes, b.route) == NULL) missing = true;
            }
            const bool stale = snap.routes.empty() || t0 - snap.routes_at > kRoutesMaxAge;
            const bool use_routes = !routes_cmd_.empty() &&
                (routes_try < 0 || ((stale || missing) && t0 - routes_try >= kRoutesRetry));
            if (use_routes) routes_try = t0;
            const std::string out = RunCommand(use_routes ? routes_cmd_ : helper_cmd_);
            if (!ParseFetchOutput(out, MonotonicSeconds(), &snap)) {
                snap.status = BusStatus::kOtherError;
            }
            switch (snap.status) {
            case BusStatus::kOk:
                failures = 0;
                next = snap.buses.empty() ? kPollEmpty : kPollOk;
                break;
            case BusStatus::kNoConfig: {
                std::lock_guard<std::mutex> l(mu_);
                --calls_today_;  // no API request was made
                next = kPollNoConfig;
                break;
            }
            case BusStatus::kAuthError:   next = kPollAuthError; break;
            case BusStatus::kLimitError:  next = kPollLimitError; break;
            default:
                // 15 s, 30 s, 60 s, then the normal interval.
                next = std::min(kPollOk, 15.0 * (1 << std::min(failures, 3)));
                ++failures;
                break;
            }
        }

        int waiting = 0;
        for (const BusArrival &b : snap.buses) waiting += b.waiting ? 1 : 0;
        int calls;
        {
            std::lock_guard<std::mutex> l(mu_);
            if (stop_) return;
            snap_ = snap;
            next_at_ = MonotonicSeconds() + next;
            calls = calls_today_;
        }

        // One journal line per change of state, never per poll.
        char line[256];
        snprintf(line, sizeof(line), "[bus] %s%s%s, %zu bus(es) (%d waiting)%s%s%s",
                 StatusName(snap.status), snap.via.empty() ? "" : " via ", snap.via.c_str(),
                 snap.buses.size(), waiting, detail.c_str(),
                 snap.notes.empty() ? "" : ", note ", snap.notes.c_str());
        if (logged != line) {
            fprintf(stderr, "%s, %d call(s) today, next in %.0fs\n", line, calls, next);
            logged = line;
        }
        const std::string routes_text = RoutesText(snap.routes);
        if (routes_text != logged_routes) {
            fprintf(stderr, "[bus] routes (headway weekday/sat/sun): %s\n", routes_text.c_str());
            logged_routes = routes_text;
        }
    }
}
