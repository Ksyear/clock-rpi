#!/usr/bin/env python3
"""Bus arrival fetcher for the Raspberry Pi LED Matrix Clock.

Copyright (C) 2026 Ksyear
Licensed under the GNU General Public License v2.0 (see COPYING).

The clock program runs this script about every 90 seconds (from its working
directory, as the unprivileged `daemon` user, fed on stdin as
`python3 -I - < ./busfetch.py` because daemon cannot open the absolute path
/home/clock/...) and reads a tiny line-based protocol from stdout:

    OK <n>                       n = number of BUS lines that follow (0 = no bus)
    ERR <KIND> <detail>          KIND = CONF | AUTH | LIMIT | NET | PARSE | API
    LABEL BUS                    kept for compatibility
    BUS <route> <secs> <stops>   one line per bus (every route at the stop),
                                 sorted by secs, at most 12

Route names are reduced to ASCII (the LED fonts have no Hangul): 급행1 -> E1,
마을1 -> M1, at most 4 characters. Which routes are shown is decided by the
clock's display mode (`./clockmode`), not here.

Only the Python standard library is used, so nothing has to be installed on the Pi.
The service key is read from ./bus.conf (never commit that file).

Usage:
    python3 -I busfetch.py                           # live request (needs bus.conf)
    python3 -I busfetch.py --parse-file tago FILE    # parse a saved response
    python3 -I busfetch.py --parse-file daejeon FILE
"""

import json
import sys
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

CONF_PATH = "bus.conf"

DEFAULTS = {
    "SERVICE_KEY": "",
    # Providers are tried in order; the next one is used only if the previous one fails.
    # 대전시 BIS is the source system. TAGO mirrors it with a delay and was seen
    # missing buses that were about to arrive, so it is only the fallback.
    "PROVIDERS": "daejeon,tago",
    # 한밭대학교 정류장, 대전역동광장 방향 (102번 기준)
    "CITY_CODE": "25",           # TAGO 도시코드: 대전광역시/계룡시
    "NODE_ID": "DJB8002920",     # TAGO 정류소 ID
    "BUS_STOP_ID": "8002920",    # 대전시 BIS 정류소 ID (같은 정류장)
    "TIMEOUT": "8",
}

TAGO_URL = ("https://apis.data.go.kr/1613000/ArvlInfoInqireService/"
            "getSttnAcctoArvlPrearngeInfoList")
DAEJEON_URL = "https://apis.data.go.kr/6300000/arrive/getArrInfoByStopID"

MAX_BUSES = 12
ROUTE_PREFIXES = (("급행", "E"), ("마을", "M"))

# data.go.kr gateway error codes (OpenAPI_ServiceResponse / returnReasonCode).
AUTH_CODES = {"20", "30", "31", "32", "33"}
LIMIT_CODES = {"22"}


class FetchError(Exception):
    def __init__(self, kind, detail):
        super().__init__(kind, detail)
        self.kind = kind
        self.detail = detail


def load_conf(path):
    conf = dict(DEFAULTS)
    try:
        with open(path, encoding="utf-8") as f:
            for raw in f:
                line = raw.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                conf[key.strip().upper()] = value.strip().strip('"').strip("'")
    except OSError as e:
        raise FetchError("CONF", "unreadable_" + type(e).__name__)
    if not conf["SERVICE_KEY"]:
        raise FetchError("CONF", "no_SERVICE_KEY")
    return conf


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
            root = ET.fromstring(stripped)
            code = (root.findtext(".//returnReasonCode") or "").strip()
            msg = (root.findtext(".//errMsg") or "").strip()
        else:
            return None
    except (ValueError, KeyError, TypeError, ET.ParseError):
        return None
    if code in AUTH_CODES or "SERVICE_KEY" in msg:
        return FetchError("AUTH", code or "key")
    if code in LIMIT_CODES or "LIMITED" in msg:
        return FetchError("LIMIT", code or "limit")
    return FetchError("API", "gw" + (code or "unknown"))


def parse_tago(text):
    err = gateway_error(text)
    if err:
        raise err
    try:
        resp = json.loads(text)["response"]
        code = str(resp["header"]["resultCode"])
        if code != "00":
            raise FetchError("API", "tago" + code)
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
            secs = to_int(it.get("arrtime"))
            stops = to_int(it.get("arrprevstationcnt"))
            if route and secs >= 0:
                buses.append((route, secs, stops))
        return buses
    except FetchError:
        raise
    except (ValueError, KeyError, TypeError, AttributeError):
        raise FetchError("PARSE", "tago")


def parse_daejeon(text):
    err = gateway_error(text)
    if err:
        raise err
    try:
        root = ET.fromstring(text.lstrip())
    except ET.ParseError:
        raise FetchError("PARSE", "daejeon")
    code = (root.findtext("./msgHeader/headerCd") or "").strip()
    if code == "4":          # 결과가 없습니다
        return []
    if code != "0":
        raise FetchError("API", "dj" + (code or "unknown"))
    buses = []
    for it in root.iter("itemList"):
        route = (it.findtext("ROUTE_NO") or "").strip()
        secs = to_int(it.findtext("EXTIME_SEC"))
        stops = to_int(it.findtext("STATUS_POS"))
        if route and secs >= 0:
            buses.append((route, secs, stops))
    return buses


def http_get(url, params, timeout):
    full = url + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(full, headers={"User-Agent": "clock-rpi"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        body = ""
        try:
            body = e.read().decode("utf-8", "replace")
        except OSError:
            pass
        err = gateway_error(body)
        if err:
            raise err
        if e.code in (401, 403):
            raise FetchError("AUTH", "http%d" % e.code)
        if e.code == 429:
            raise FetchError("LIMIT", "http429")
        raise FetchError("NET", "http%d" % e.code)
    except (urllib.error.URLError, OSError) as e:
        raise FetchError("NET", type(e).__name__)


def service_key(conf):
    key = conf["SERVICE_KEY"]
    # Accept both the "Encoding" and "Decoding" key forms; urlencode() below
    # must see the decoded form so the key is encoded exactly once.
    if "%" in key:
        key = urllib.parse.unquote(key)
    return key


def fetch_tago(conf):
    params = {
        "serviceKey": service_key(conf),
        "cityCode": conf["CITY_CODE"],
        "nodeId": conf["NODE_ID"],
        "numOfRows": "50",
        "pageNo": "1",
        "_type": "json",
    }
    return parse_tago(http_get(TAGO_URL, params, float(conf["TIMEOUT"])))


def fetch_daejeon(conf):
    params = {
        "serviceKey": service_key(conf),
        "BusStopID": conf["BUS_STOP_ID"],
    }
    return parse_daejeon(http_get(DAEJEON_URL, params, float(conf["TIMEOUT"])))


PROVIDERS = {"tago": (fetch_tago, parse_tago), "daejeon": (fetch_daejeon, parse_daejeon)}


def select(buses):
    """Every route at the stop, soonest first, at most MAX_BUSES lines."""
    cleaned = [(clean_route(route), secs, stops) for route, secs, stops in buses]
    return sorted(cleaned, key=lambda b: b[1])[:MAX_BUSES]


def emit(out, buses, error=None):
    if error is not None:
        out.write("ERR %s %s\n" % (error.kind, error.detail.replace(" ", "_") or "-"))
    else:
        out.write("OK %d\n" % len(buses))
    out.write("LABEL BUS\n")
    if error is None:
        for route, secs, stops in buses:
            out.write("BUS %s %d %d\n" % (route, secs, stops))


def run(argv, out):
    if len(argv) == 4 and argv[1] == "--parse-file":
        provider, path = argv[2], argv[3]
        if provider not in PROVIDERS:
            out.write("ERR CONF unknown_provider\nLABEL BUS\n")
            return 2
        try:
            with open(path, encoding="utf-8") as f:
                buses = PROVIDERS[provider][1](f.read())
            emit(out, select(buses))
        except FetchError as e:
            emit(out, [], e)
        return 0

    try:
        conf = load_conf(CONF_PATH)
    except FetchError as e:
        emit(out, [], e)
        return 0

    last_error = FetchError("CONF", "no_PROVIDERS")
    for name in [p.strip().lower() for p in conf["PROVIDERS"].split(",") if p.strip()]:
        if name not in PROVIDERS:
            last_error = FetchError("CONF", "unknown_provider")
            continue
        try:
            emit(out, select(PROVIDERS[name][0](conf)))
            return 0
        except FetchError as e:
            sys.stderr.write("busfetch: %s failed: %s %s\n" % (name, e.kind, e.detail))
            last_error = e
    emit(out, [], last_error)
    return 0


if __name__ == "__main__":
    sys.exit(run(sys.argv, sys.stdout))
