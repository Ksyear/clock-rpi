#!/usr/bin/env python3
"""Bus arrival fetcher for the Raspberry Pi LED Matrix Clock.

Copyright (C) 2026 Ksyear
Licensed under the GNU General Public License v2.0 (see COPYING).

The clock program runs this script about every 90 seconds (from its working
directory, as the unprivileged `daemon` user, fed on stdin as
`python3 -I - < ./busfetch.py` because daemon cannot open the absolute path
/home/clock/...) and reads a tiny line-based protocol from stdout:

    OK <n>                         n = number of BUS lines that follow (0 = no bus)
    ERR <KIND> <detail>            KIND = CONF | AUTH | LIMIT | NET | PARSE | API
    LABEL BUS                      kept for compatibility
    VIA <provider>                 which API answered (daejeon | tago)
    NOTE <text>                    something worth a journal line (unknown MSG_TP)
    BUS <route> <secs> <stops> RUN            running: <secs> until arrival
    BUS <route> <secs> <stops> WAIT <min>     waiting at its terminal: estimated
                                              <secs> until arrival, never less
                                              than <min> (the drive to this stop)
    ROUTE <route> <hw_wk> <hw_sat> <hw_sun> <last_wk> <last_sat> <last_sun>
                                   only with --routes, after the BUS lines (not
                                   counted in OK n): headway in minutes and the
                                   last departure HHMM per weekday/Saturday/Sunday
                                   from 노선정보조회, 0 = unknown. The clock uses
                                   it to estimate the NEXT bus of a route.

Buses waiting at their terminal (대전시 MSG_TP=07) do not report seconds: their
EXTIME_SEC is the scheduled departure time as HHMM (1519 = 15:19). They are
converted to "seconds until departure + STATUS_POS x WAIT_STOP_SECS". TAGO
mirrors the same numbers without MSG_TP, so it is told apart heuristically.

Route names are reduced to ASCII (the LED fonts have no Hangul): 급행1 -> E1,
마을1 -> M1, at most 4 characters. At most 12 buses, soonest first.

Only the Python standard library is used. The service key is read from
./bus.conf (never commit that file) and only ever sent over HTTPS.

Usage:
    python3 -I busfetch.py [--routes]                         # live (needs bus.conf)
    python3 -I busfetch.py --parse-file daejeon FILE [--now HH:MM:SS] [--route-file F]...
    python3 -I busfetch.py --parse-file tago FILE [--now HH:MM:SS]
"""

import json
import math
import os
import re
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

CONF_PATH = "bus.conf"

DEFAULTS = {
    "SERVICE_KEY": "",
    # Providers are tried in order; the next one is used only if the previous one fails.
    # 대전시 BIS is the source system. TAGO mirrors it with a delay, was seen missing
    # buses that were about to arrive and has no MSG_TP, so it is only the fallback.
    "PROVIDERS": "daejeon,tago",
    # 한밭대학교 정류장, 대전역동광장 방향 (102번 기준)
    "CITY_CODE": "25",           # TAGO 도시코드: 대전광역시/계룡시
    "NODE_ID": "DJB8002920",     # TAGO 정류소 ID
    "BUS_STOP_ID": "8002920",    # 대전시 BIS 정류소 ID (같은 정류장)
    "TIMEOUT": "8",              # seconds per request, 1..8
    # Estimated drive per stop for a bus still at its terminal. 102 needed
    # 52-82 s per stop from 수통골; the low end is used because a bus that comes
    # earlier than shown is the one you miss. 0..300.
    "WAIT_STOP_SECS": "60",
}

TAGO_URL = ("https://apis.data.go.kr/1613000/ArvlInfoInqireService/"
            "getSttnAcctoArvlPrearngeInfoList")
DAEJEON_URL = "https://apis.data.go.kr/6300000/arrive/getArrInfoByStopID"
ROUTE_INFO_URL = "https://apis.data.go.kr/6300000/busRouteInfo/getRouteInfo"
ROUTES_BUDGET = 12.0             # seconds for all route-info calls together
ROUTES_DEADLINE = 17.0           # ... and never later than this after start (timeout 20)
ROUTE_CALL_TIMEOUT = 3.0

MAX_BUSES = 12
MAX_RESPONSE = 256 * 1024
MAX_CONF = 64 * 1024
MAX_ARRIVAL = 99 * 60 + 59       # the screens show at most 99 minutes
DEPARTED_GRACE = 10 * 60         # "departure time passed" still counts as about to leave
KST_OFFSET = 9 * 3600            # timetables are KST, whatever the Pi's time zone is
ROUTE_PREFIXES = (("급행", "E"), ("마을", "M"))

# data.go.kr gateway error codes (OpenAPI_ServiceResponse / returnReasonCode).
AUTH_CODES = {"20", "30", "31", "32", "33"}
LIMIT_CODES = {"22"}


class FetchError(Exception):
    def __init__(self, kind, detail):
        super().__init__(kind, detail)
        self.kind = kind
        self.detail = detail


def safe_word(s, limit=40):
    """Protocol-safe token: no spaces/newlines/odd characters, bounded length."""
    s = re.sub(r"[^A-Za-z0-9_.=-]", "_", str(s))[:limit]
    return s or "-"


def load_conf(path):
    conf = dict(DEFAULTS)
    try:
        with open(path, "rb") as f:
            data = f.read(MAX_CONF)
    except OSError as e:
        raise FetchError("CONF", "unreadable_" + type(e).__name__)
    for raw in data.decode("utf-8", "replace").split("\n"):
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        conf[key.strip().upper()] = value.strip().strip('"').strip("'")
    if not conf["SERVICE_KEY"]:
        raise FetchError("CONF", "no_SERVICE_KEY")
    return conf


def conf_number(conf, key, lo, hi, cast=int):
    """Number from bus.conf, clamped to [lo, hi]; the default if not a number."""
    try:
        v = cast(conf.get(key, DEFAULTS[key]))
        if isinstance(v, float) and not math.isfinite(v):
            raise ValueError
    except (TypeError, ValueError):
        v = cast(DEFAULTS[key])
    return max(lo, min(hi, v))


def clean_route(name):
    """ASCII route label for the LED fonts: 급행1 -> E1, 마을1 -> M1, max 4 chars."""
    s = "".join(str(name).split())
    for korean, ascii_prefix in ROUTE_PREFIXES:
        s = s.replace(korean, ascii_prefix)
    s = "".join(ch for ch in s.upper() if ch.isascii() and (ch.isalnum() or ch == "-"))
    return s[:4] or "?"


def to_int(value, default=-1):
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return default


def now_kst_sod():
    """Seconds since midnight in Korea, independent of the Pi's TZ setting."""
    return int(time.time() + KST_OFFSET) % 86400


def parse_hms(text):
    m = re.fullmatch(r"(\d{1,2}):(\d{2})(?::(\d{2}))?", text)
    if not m or int(m.group(1)) > 23 or int(m.group(2)) > 59 or int(m.group(3) or 0) > 59:
        raise ValueError("bad time: %r" % text)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3) or 0)


def hhmm_seconds(value):
    """Seconds of day for an HHMM number (1519 -> 15:19), or None if invalid."""
    if value < 0:
        return None
    hh, mm = divmod(value, 100)
    if hh > 23 or mm > 59:
        return None
    return hh * 3600 + mm * 60


def until(dep_sod, now_sod):
    """Signed seconds from now to a time of day, within -12 h .. +12 h (midnight safe)."""
    d = (dep_sod - now_sod) % 86400
    return d - 86400 if d > 43200 else d


def waiting_bus(route, hhmm, stops, now_sod, stop_secs):
    """A bus at its terminal leaving at HHMM -> (route, secs, stops, 'WAIT', min) or None."""
    dep = hhmm_seconds(hhmm)
    if dep is None:
        return None
    d = until(dep, now_sod)
    if d < -DEPARTED_GRACE:
        return None                     # stale entry: it should have left long ago
    drive = max(0, stops) * stop_secs
    total = max(0, d) + drive
    if total > MAX_ARRIVAL:
        return None                     # far in the future (e.g. tomorrow's first bus)
    return (route, total, stops, "WAIT", drive)


def gateway_error(text):
    """Return a FetchError if `text` is a data.go.kr gateway error (JSON or XML)."""
    code, msg = "", ""
    stripped = text.lstrip()
    try:
        if stripped.startswith("{"):
            header = json.loads(stripped)["OpenAPI_ServiceResponse"]["cmmMsgHeader"]
            code = str(header.get("returnReasonCode", ""))
            msg = str(header.get("errMsg", ""))
        elif stripped.startswith("<") and "OpenAPI_ServiceResponse" in stripped:
            root = parse_xml(stripped)
            code = (root.findtext(".//returnReasonCode") or "").strip()
            msg = (root.findtext(".//errMsg") or "").strip()
        else:
            return None
    except (ValueError, KeyError, TypeError, AttributeError, FetchError, ET.ParseError):
        return None
    if code in AUTH_CODES or "SERVICE_KEY" in msg:
        return FetchError("AUTH", code or "key")
    if code in LIMIT_CODES or "LIMITED" in msg:
        return FetchError("LIMIT", code or "limit")
    return FetchError("API", "gw" + (code or "unknown"))


def parse_xml(text):
    # The API never sends a DTD; refusing one rules out entity-expansion tricks.
    upper = text[:4096].upper()
    if "<!DOCTYPE" in upper or "<!ENTITY" in upper:
        raise FetchError("PARSE", "doctype")
    try:
        return ET.fromstring(text.lstrip())
    except ET.ParseError:
        raise FetchError("PARSE", "xml")


def parse_tago(text, now_sod=None, stop_secs=60, notes=None, extra=None):
    err = gateway_error(text)
    if err:
        raise err
    now_sod = now_kst_sod() if now_sod is None else now_sod
    try:
        resp = json.loads(text)["response"]
        code = str(resp["header"]["resultCode"])
        if code != "00":
            raise FetchError("API", "tago" + safe_word(code, 8))
        items = resp["body"].get("items")
        # TAGO quirk: 0 results -> items == "", 1 result -> item is an object.
        if not items:
            return []
        item = items.get("item", [])
        if isinstance(item, dict):
            item = [item]
        buses = []
        for it in item:
            route = str(it.get("routeno", "")).strip()
            if extra is not None:   # TAGO routeid DJB30300037 = 대전시 ROUTE_CD 30300037
                cd = str(it.get("routeid", ""))
                if cd.startswith("DJB") and cd[3:].isdigit() and route:
                    extra.setdefault("route_cds", {})[cd[3:]] = route
            v = to_int(it.get("arrtime"))
            stops = to_int(it.get("arrprevstationcnt"))
            if not route or v < 0:
                continue
            # No MSG_TP here, so waiting buses (HHMM) are told apart by plausibility.
            dep = hhmm_seconds(v)
            if v < 500 or dep is None:
                buses.append((route, v, stops, "RUN", 0))       # surely seconds
            elif v / max(stops, 1) > 240:
                # > 4 min per stop is not a driving estimate: a departure time.
                bus = waiting_bus(route, v, stops, now_sod, stop_secs)
                if bus:
                    buses.append(bus)
            elif -DEPARTED_GRACE <= until(dep, now_sod) <= 3 * 3600:
                pass   # could be either; a blank beats a number that is 10 min off
            else:
                buses.append((route, v, stops, "RUN", 0))
        return buses
    except FetchError:
        raise
    except (ValueError, KeyError, TypeError, AttributeError):
        raise FetchError("PARSE", "tago")


def parse_daejeon(text, now_sod=None, stop_secs=60, notes=None, extra=None):
    err = gateway_error(text)
    if err:
        raise err
    now_sod = now_kst_sod() if now_sod is None else now_sod
    root = parse_xml(text)
    code = (root.findtext("./msgHeader/headerCd") or "").strip()
    if code == "4":          # 결과가 없습니다
        return []
    if code != "0":
        raise FetchError("API", "dj" + safe_word(code or "unknown", 8))
    buses = []
    for it in root.iter("itemList"):
        route = (it.findtext("ROUTE_NO") or "").strip()
        if extra is not None:
            cd = (it.findtext("ROUTE_CD") or "").strip()
            if cd.isdigit() and len(cd) <= 12 and route:
                extra.setdefault("route_cds", {})[cd] = route
        msg_tp = (it.findtext("MSG_TP") or "").strip()
        secs = to_int(it.findtext("EXTIME_SEC"))
        mins = to_int(it.findtext("EXTIME_MIN"))
        stops = to_int(it.findtext("STATUS_POS"))
        if not route or secs < 0:
            continue
        if msg_tp in ("03", "06"):           # running / about to arrive: real seconds
            buses.append((route, secs, stops, "RUN", 0))
        elif msg_tp == "07":                 # at its terminal: EXTIME_SEC is HHMM
            bus = waiting_bus(route, secs, stops, now_sod, stop_secs)
            if bus:
                buses.append(bus)
        else:
            if notes is not None:
                notes.add("msg_tp=" + safe_word(msg_tp or "none", 8))
            if mins >= 0 and abs(mins - secs / 60) <= 1.5:
                buses.append((route, secs, stops, "RUN", 0))
            else:
                bus = waiting_bus(route, secs, stops, now_sod, stop_secs)
                if bus:
                    buses.append(bus)
    return buses


def parse_route_info(text):
    """getRouteInfo -> (route, (hw_wk, hw_sat, hw_sun), (last_wk, last_sat, last_sun)).

    Headways in minutes, last departures as HHMM (the later of ORIGIN_END and
    TURN_END, so it also covers stops on the way back); 0 = unknown/invalid."""
    err = gateway_error(text)
    if err:
        raise err
    root = parse_xml(text)
    code = (root.findtext("./msgHeader/headerCd") or "").strip()
    if code != "0":
        raise FetchError("API", "rt" + safe_word(code or "unknown", 8))
    it = root.find(".//itemList")
    if it is None:
        raise FetchError("PARSE", "route_info")
    route = clean_route(it.findtext("ROUTE_NO") or "")

    def headway(tag):
        v = to_int(it.findtext(tag))
        return v if 1 <= v <= 600 else 0

    def last(suffix):
        best = 0
        for tag in ("ORIGIN_END", "TURN_END"):
            v = to_int(it.findtext(tag + suffix))
            if hhmm_seconds(v) is None:
                continue
            # after-midnight times (00:30) are later than 23:00
            if best == 0 or (v + 2400 if v < 400 else v) > (best + 2400 if best < 400 else best):
                best = v
        return best

    return (route,
            tuple(headway(t) for t in ("ALLO_INTERVAL", "ALLO_INTERVAL_SAT", "ALLO_INTERVAL_SUN")),
            tuple(last(sfx) for sfx in ("", "_SAT", "_SUN")))


def fetch_routes(conf, codes, notes, started):
    """Route info for every ROUTE_CD seen, within a time budget; partial is fine."""
    routes = []
    deadline = min(time.monotonic() + ROUTES_BUDGET, started + ROUTES_DEADLINE)
    for cd in sorted(codes):
        left = deadline - time.monotonic()
        if left < 0.5:
            notes.add("routes=TIMEOUT")
            break
        try:
            text = http_get(ROUTE_INFO_URL, {"serviceKey": service_key(conf), "busRouteId": cd},
                            min(ROUTE_CALL_TIMEOUT, left))
            routes.append(parse_route_info(text))
        except FetchError as e:
            notes.add("routes=" + e.kind)
            if e.kind in ("AUTH", "LIMIT", "CONF"):
                break           # the others would fail the same way
    return routes


def emit_routes(out, routes, notes=()):
    for route, hw, last in routes:
        out.write("ROUTE %s %d %d %d %d %d %d\n" % ((route,) + tuple(hw) + tuple(last)))
    for note in sorted(notes):
        out.write("NOTE %s\n" % safe_word(note))


def https_opener():
    """HTTPS only: no redirects (a redirect to http:// would leak the key), no
    proxies from the environment, no other URL schemes. TLS verification on."""
    opener = urllib.request.OpenerDirector()
    opener.add_handler(urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    opener.add_handler(urllib.request.HTTPDefaultErrorHandler())
    opener.add_handler(urllib.request.HTTPErrorProcessor())
    opener.add_handler(urllib.request.UnknownHandler())
    return opener


def http_get(url, params, timeout):
    if not url.startswith("https://"):
        raise FetchError("NET", "not_https")
    full = url + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(full, headers={"User-Agent": "clock-rpi"})
    try:
        with https_opener().open(req, timeout=timeout) as r:
            body = r.read(MAX_RESPONSE + 1)
    except urllib.error.HTTPError as e:
        try:
            body = e.read(MAX_RESPONSE).decode("utf-8", "replace")
        except OSError:
            body = ""
        err = gateway_error(body)
        if err:
            raise err
        if e.code in (401, 403):
            raise FetchError("AUTH", "http%d" % e.code)
        if e.code == 429:
            raise FetchError("LIMIT", "http429")
        raise FetchError("NET", "http%d" % e.code)
    except (urllib.error.URLError, OSError, ValueError) as e:
        raise FetchError("NET", type(e).__name__)
    if len(body) > MAX_RESPONSE:
        raise FetchError("PARSE", "too_large")
    return body.decode("utf-8", "replace")


def service_key(conf):
    key = conf["SERVICE_KEY"]
    # Accept both the "Encoding" and "Decoding" key forms; urlencode() below
    # must see the decoded form so the key is encoded exactly once.
    if "%" in key:
        key = urllib.parse.unquote(key)
    return key


def fetch_tago(conf, notes, extra=None):
    params = {
        "serviceKey": service_key(conf),
        "cityCode": conf["CITY_CODE"],
        "nodeId": conf["NODE_ID"],
        "numOfRows": "50",
        "pageNo": "1",
        "_type": "json",
    }
    text = http_get(TAGO_URL, params, conf_number(conf, "TIMEOUT", 1.0, 8.0, float))
    return parse_tago(text, None, conf_number(conf, "WAIT_STOP_SECS", 0, 300), notes, extra)


def fetch_daejeon(conf, notes, extra=None):
    params = {
        "serviceKey": service_key(conf),
        "BusStopID": conf["BUS_STOP_ID"],
    }
    text = http_get(DAEJEON_URL, params, conf_number(conf, "TIMEOUT", 1.0, 8.0, float))
    return parse_daejeon(text, None, conf_number(conf, "WAIT_STOP_SECS", 0, 300), notes, extra)


PROVIDERS = {"tago": (fetch_tago, parse_tago), "daejeon": (fetch_daejeon, parse_daejeon)}


def select(buses):
    """Every route at the stop, soonest first, at most MAX_BUSES lines."""
    cleaned = [(clean_route(b[0]),) + tuple(b[1:]) for b in buses]
    return sorted(cleaned, key=lambda b: b[1])[:MAX_BUSES]


def emit(out, buses, error=None, via=None, notes=()):
    if error is not None:
        out.write("ERR %s %s\n" % (safe_word(error.kind, 8), safe_word(error.detail)))
        out.write("LABEL BUS\n")
        return
    out.write("OK %d\n" % len(buses))
    out.write("LABEL BUS\n")
    if via:
        out.write("VIA %s\n" % safe_word(via, 12))
    for note in sorted(notes):
        out.write("NOTE %s\n" % safe_word(note))
    for route, secs, stops, kind, min_secs in buses:
        if kind == "WAIT":
            out.write("BUS %s %d %d WAIT %d\n" % (route, secs, stops, min_secs))
        else:
            out.write("BUS %s %d %d RUN\n" % (route, secs, stops))


def run(argv, out):
    started = time.monotonic()
    if len(argv) >= 4 and argv[1] == "--parse-file":
        provider, path = argv[2], argv[3]
        if provider not in PROVIDERS:
            out.write("ERR CONF unknown_provider\nLABEL BUS\n")
            return 2
        now_sod, route_files, rest = None, [], argv[4:]
        while rest:
            if rest[0] == "--now" and len(rest) >= 2:
                now_sod = parse_hms(rest[1])
            elif rest[0] == "--route-file" and len(rest) >= 2:
                route_files.append(rest[1])
            else:
                raise ValueError("unknown argument")
            rest = rest[2:]
        notes = set()
        try:
            with open(path, encoding="utf-8") as f:
                buses = PROVIDERS[provider][1](f.read(), now_sod, 60, notes)
            emit(out, select(buses), via=provider, notes=notes)
        except FetchError as e:
            emit(out, [], e)
            return 0
        routes, route_notes = [], set()
        for rf in route_files:
            try:
                with open(rf, encoding="utf-8") as f:
                    routes.append(parse_route_info(f.read()))
            except FetchError as e:
                route_notes.add("routes=" + e.kind)
        emit_routes(out, routes, route_notes)
        return 0

    # The clock drops root before running this; refuse to handle the key as root.
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        emit(out, [], FetchError("CONF", "refuse_root"))
        return 0

    try:
        conf = load_conf(CONF_PATH)
    except FetchError as e:
        emit(out, [], e)
        return 0

    with_routes = "--routes" in argv[1:]
    last_error = FetchError("CONF", "no_PROVIDERS")
    for name in [p.strip().lower() for p in conf["PROVIDERS"].split(",") if p.strip()]:
        if name not in PROVIDERS:
            last_error = FetchError("CONF", "unknown_provider")
            continue
        notes, extra = set(), {}
        try:
            buses = PROVIDERS[name][0](conf, notes, extra)
        except FetchError as e:
            last_error = e   # the clock logs the final result; no per-attempt lines
            continue
        emit(out, select(buses), via=name, notes=notes)
        if with_routes and extra.get("route_cds"):
            # The arrivals are out first: if the route calls run into the clock's
            # timeout, it still gets them.
            out.flush()
            route_notes = set()
            emit_routes(out, fetch_routes(conf, extra["route_cds"], route_notes, started),
                        route_notes)
        return 0
    emit(out, [], last_error)
    return 0


def main():
    try:
        return run(sys.argv, sys.stdout)
    except Exception as e:  # never print a traceback (it could quote request details)
        sys.stdout.write("ERR PARSE %s\nLABEL BUS\n" % safe_word(type(e).__name__))
        return 0


if __name__ == "__main__":
    sys.exit(main())
