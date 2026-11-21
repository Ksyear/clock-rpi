/*
 * Raspberry Pi LED Matrix Clock - display mode settings (./display.conf)
 *
 * Copyright (C) 2026 Ksyear
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include "settings.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>

namespace {

const size_t kMaxFileBytes = 4096;  // same limit as clockmode

// ASCII-only helpers: bytes >= 0x80 are never spaces, letters or digits, so
// the result does not depend on the locale (clockmode works on bytes too).
bool IsSpace(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
bool IsDigit(unsigned char c) { return c >= '0' && c <= '9'; }
char Upper(unsigned char c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }

std::string Trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && IsSpace(s[b])) b++;
    while (e > b && IsSpace(s[e - 1])) e--;
    return s.substr(b, e - b);
}

std::string UpperStr(std::string s) {
    for (char &ch : s) ch = Upper(ch);
    return s;
}

bool ParseMode(const std::string &v, DisplayMode *m) {
    const std::string s = UpperStr(v);
    if (s == "0" || s == "OFF")            *m = DisplayMode::kOff;
    else if (s == "1" || s == "ALTERNATE") *m = DisplayMode::kAlternate;
    else if (s == "2" || s == "ROTATE")    *m = DisplayMode::kRotate;
    else if (s == "3" || s == "FIXED")     *m = DisplayMode::kFixed;
    else return false;
    return true;
}

bool ValidRoute(const std::string &s) {
    if (s.empty() || s.size() > 4) return false;
    for (unsigned char ch : s) {
        if (!(IsDigit(ch) || (ch >= 'A' && ch <= 'Z') || ch == '-')) return false;
    }
    return true;
}

// Integer matching ^[+-]?[0-9]+$; `def` if not a number, otherwise clamped.
int ParseInt(const std::string &v, int def, int lo, int hi) {
    size_t i = (!v.empty() && (v[0] == '+' || v[0] == '-')) ? 1 : 0;
    if (i >= v.size()) return def;
    long long n = 0;
    for (; i < v.size(); i++) {
        if (!IsDigit(v[i])) return def;  // also rejects an embedded NUL
        if (n < 1000000) n = n * 10 + (v[i] - '0');
    }
    if (v[0] == '-') n = -n;
    return static_cast<int>(std::max<long long>(lo, std::min<long long>(hi, n)));
}

// "H:MM" or "HH:MM" -> minutes of day, or -1.
int ParseClock(const std::string &s) {
    const size_t colon = s.find(':');
    if (colon == std::string::npos || colon < 1 || colon > 2 || s.size() != colon + 3) return -1;
    for (size_t i = 0; i < s.size(); i++) {
        if (i != colon && !IsDigit(s[i])) return -1;
    }
    const int h = atoi(s.substr(0, colon).c_str());
    const int m = atoi(s.substr(colon + 1).c_str());
    if (h > 23 || m > 59) return -1;
    return h * 60 + m;
}

// "HH:MM-HH:MM" (start != end, at most 12 h long) or "off".
bool ParseQuiet(const std::string &v, int *start, int *end) {
    if (UpperStr(v) == "OFF") {
        *start = *end = -1;
        return true;
    }
    const size_t dash = v.find('-');
    if (dash == std::string::npos) return false;
    const int a = ParseClock(v.substr(0, dash));
    const int b = ParseClock(v.substr(dash + 1));
    if (a < 0 || b < 0 || a == b) return false;
    if ((b - a + 1440) % 1440 > 720) return false;  // probably start/end swapped
    *start = a;
    *end = b;
    return true;
}

}  // namespace

const char *ModeName(DisplayMode m) {
    switch (m) {
    case DisplayMode::kOff:       return "off";
    case DisplayMode::kAlternate: return "alternate";
    case DisplayMode::kRotate:    return "rotate";
    case DisplayMode::kFixed:     return "fixed";
    }
    return "?";
}

DisplaySettings ParseDisplaySettings(const std::string &text) {
    DisplaySettings s;
    const DisplaySettings def;
    std::istringstream in(text.substr(0, kMaxFileBytes));  // like the file read below
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = UpperStr(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));
        if (key == "MODE") {
            if (!ParseMode(value, &s.mode)) s.mode = def.mode;
        } else if (key == "ROUTE") {
            const std::string r = UpperStr(value);
            s.route = ValidRoute(r) ? r : def.route;
        } else if (key == "ALT_CLOCK_SECS") {
            s.alt_clock_secs = ParseInt(value, def.alt_clock_secs, 5, 300);
        } else if (key == "ALT_BUS_SECS") {
            s.alt_bus_secs = ParseInt(value, def.alt_bus_secs, 2, 30);
        } else if (key == "ROTATE_SECS") {
            s.rotate_secs = ParseInt(value, def.rotate_secs, 2, 60);
        } else if (key == "ROTATE_WITHIN_MINS") {
            s.rotate_within = ParseInt(value, def.rotate_within, 5, 99);
        } else if (key == "ROTATE_MIN_MINS") {
            s.rotate_min = ParseInt(value, def.rotate_min, 0, 10);
        } else if (key == "QUIET") {
            if (!ParseQuiet(value, &s.quiet_start, &s.quiet_end)) {
                s.quiet_start = def.quiet_start;
                s.quiet_end = def.quiet_end;
            }
        }
    }
    return s;
}

std::string QuietText(const DisplaySettings &s) {
    if (s.quiet_start < 0) return "off";
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", s.quiet_start / 60, s.quiet_start % 60,
             s.quiet_end / 60, s.quiet_end % 60);
    return buf;
}

std::string DumpDisplaySettings(const DisplaySettings &s) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "mode=%s route=%s alt_clock=%d alt_bus=%d rotate=%d max=%d min=%d quiet=%s",
             ModeName(s.mode), s.route.c_str(), s.alt_clock_secs, s.alt_bus_secs,
             s.rotate_secs, s.rotate_within, s.rotate_min, QuietText(s).c_str());
    return buf;
}

bool InQuietHours(const DisplaySettings &s, int secs_of_day) {
    if (s.quiet_start < 0) return false;
    const int start = s.quiet_start * 60, end = s.quiet_end * 60;
    if (start < end) return secs_of_day >= start && secs_of_day < end;
    return secs_of_day >= start || secs_of_day < end;  // crosses midnight
}

SettingsFile::SettingsFile(const std::string &path) : path_(path) {}

bool SettingsFile::Poll(double now) {
    if (loaded_ && now - last_check_ < 1.0) return false;
    last_check_ = now;

    struct stat st;
    const bool exists = stat(path_.c_str(), &st) == 0;
    if (loaded_ && exists == exists_ &&
        (!exists || (st.st_ino == ino_ && st.st_size == size_ && st.st_mtime == mtime_))) {
        return false;
    }
    exists_ = exists;
    if (exists) {
        ino_ = st.st_ino;
        size_ = st.st_size;
        mtime_ = st.st_mtime;
    }

    // Defaults when the file is missing, unreadable or not a regular file.
    // O_NONBLOCK + the S_ISREG check: a FIFO or device here can't stall the clock.
    DisplaySettings next;
    if (exists) {
        const int fd = open(path_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            struct stat fst;
            if (fstat(fd, &fst) == 0 && S_ISREG(fst.st_mode)) {
                char buf[kMaxFileBytes];
                size_t got = 0;
                ssize_t n;
                while (got < sizeof(buf) && (n = read(fd, buf + got, sizeof(buf) - got)) > 0) {
                    got += static_cast<size_t>(n);
                }
                next = ParseDisplaySettings(std::string(buf, got));
            }
            close(fd);
        }
    }

    const bool changed = !loaded_ || DumpDisplaySettings(next) != DumpDisplaySettings(settings_);
    settings_ = next;
    loaded_ = true;
    if (changed) {
        fprintf(stderr, "[display] %s%s\n", DumpDisplaySettings(settings_).c_str(),
                exists ? "" : " (defaults, no display.conf)");
    }
    return changed;
}
