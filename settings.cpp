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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

std::string Trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) b++;
    while (e > b && isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

std::string Upper(std::string s) {
    for (char &ch : s) ch = toupper(static_cast<unsigned char>(ch));
    return s;
}

bool ParseMode(const std::string &v, DisplayMode *m) {
    const std::string s = Upper(v);
    if (s == "0" || s == "OFF")       *m = DisplayMode::kOff;
    else if (s == "1" || s == "ALTERNATE") *m = DisplayMode::kAlternate;
    else if (s == "2" || s == "ROTATE")    *m = DisplayMode::kRotate;
    else if (s == "3" || s == "FIXED")     *m = DisplayMode::kFixed;
    else return false;
    return true;
}

bool ValidRoute(const std::string &s) {
    if (s.empty() || s.size() > 4) return false;
    for (unsigned char ch : s) {
        if (!(isdigit(ch) || (ch >= 'A' && ch <= 'Z') || ch == '-')) return false;
    }
    return true;
}

// Parses an integer; returns `def` if not a number, otherwise clamps to [lo, hi].
int ParseInt(const std::string &v, int def, int lo, int hi) {
    if (v.empty()) return def;
    char *end = NULL;
    const long n = strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0') return def;
    return static_cast<int>(std::max<long>(lo, std::min<long>(hi, n)));
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
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Upper(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));
        if (key == "MODE") {
            if (!ParseMode(value, &s.mode)) s.mode = def.mode;
        } else if (key == "ROUTE") {
            const std::string r = Upper(value);
            s.route = ValidRoute(r) ? r : def.route;
        } else if (key == "ALT_CLOCK_SECS") {
            s.alt_clock_secs = ParseInt(value, def.alt_clock_secs, 5, 300);
        } else if (key == "ALT_BUS_SECS") {
            s.alt_bus_secs = ParseInt(value, def.alt_bus_secs, 2, 30);
        } else if (key == "ROTATE_SECS") {
            s.rotate_secs = ParseInt(value, def.rotate_secs, 2, 30);
        }
    }
    return s;
}

std::string DumpDisplaySettings(const DisplaySettings &s) {
    char buf[128];
    snprintf(buf, sizeof(buf), "mode=%s route=%s alt_clock=%d alt_bus=%d rotate=%d",
             ModeName(s.mode), s.route.c_str(), s.alt_clock_secs, s.alt_bus_secs,
             s.rotate_secs);
    return buf;
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

    DisplaySettings next;  // defaults when the file is missing or unreadable
    if (exists) {
        std::ifstream f(path_.c_str());
        if (f) {
            std::stringstream text;
            text << f.rdbuf();
            next = ParseDisplaySettings(text.str());
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
