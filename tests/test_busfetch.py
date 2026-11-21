"""Tests for busfetch.py using real API responses saved on 2026-11-21.

Run from the project directory:  python3 -I tests/test_busfetch.py  (or: make check)

The fixtures are responses for the 한밭대학교 stop toward 대전역동광장
(TAGO nodeId DJB8002920 / 대전시 BusStopID 8002920). daejeon_mixed.xml was
captured at 15:12:58 KST, daejeon_many.xml / tago_*.json at about 14:05:40.
*_badkey are the real gateway answers to an invalid key. No service key is
stored in them.
"""

import contextlib
import io
import os
import sys
import tempfile
import unittest
import urllib.request
from unittest import mock

sys.dont_write_bytecode = True
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import busfetch  # noqa: E402

FIXTURES = os.path.join(ROOT, "tests", "fixtures")


def hms(text):
    return busfetch.parse_hms(text)


def parse_file(provider, name, now="14:05:40"):
    out = io.StringIO()
    busfetch.run(["busfetch.py", "--parse-file", provider, os.path.join(FIXTURES, name),
                  "--now", now], out)
    return out.getvalue()


def read(name):
    with open(os.path.join(FIXTURES, name), encoding="utf-8") as f:
        return f.read()


class FixtureTest(unittest.TestCase):
    def test_daejeon_mixed_running_and_waiting(self):
        # 102 leaves 수통골 at 15:19: 6 min 2 s + 2 stops x 60 s = 482 s.
        self.assertEqual(parse_file("daejeon", "daejeon_mixed.xml", "15:12:58"),
                         "OK 6\nLABEL BUS\nVIA daejeon\n"
                         "BUS 102 482 2 WAIT 120\nBUS 113 520 4 RUN\nBUS 104 902 2 WAIT 120\n"
                         "BUS 212 1226 10 RUN\nBUS 117 1562 5 WAIT 300\nBUS 11 3542 2 WAIT 120\n")

    def test_daejeon_many_far_departure_is_dropped(self):
        # 11 leaves at 16:10, more than 99 minutes away.
        self.assertEqual(parse_file("daejeon", "daejeon_many.xml"),
                         "OK 5\nLABEL BUS\nVIA daejeon\n"
                         "BUS 102 320 2 WAIT 120\nBUS 113 359 3 RUN\nBUS 104 980 2 WAIT 120\n"
                         "BUS 117 1040 5 WAIT 300\nBUS 212 2147 24 RUN\n")

    def test_tago_heuristic(self):
        # 113 578: "05:78" is not a time -> seconds. 212 2250/25 stops is a plausible
        # drive and 22:50 is far away -> seconds. 102/104: > 4 min per stop -> HHMM.
        self.assertEqual(parse_file("tago", "tago_many.json"),
                         "OK 4\nLABEL BUS\nVIA tago\n"
                         "BUS 102 320 2 WAIT 120\nBUS 113 578 4 RUN\nBUS 104 980 2 WAIT 120\n"
                         "BUS 212 2250 25 RUN\n")

    def test_tago_single_item_is_an_object(self):
        self.assertEqual(parse_file("tago", "tago_single.json"),
                         "OK 1\nLABEL BUS\nVIA tago\nBUS 102 320 2 WAIT 120\n")

    def test_empty_answers(self):
        self.assertEqual(parse_file("tago", "tago_empty.json"), "OK 0\nLABEL BUS\nVIA tago\n")
        self.assertEqual(parse_file("daejeon", "daejeon_empty.xml"),
                         "OK 0\nLABEL BUS\nVIA daejeon\n")

    def test_bad_key(self):
        self.assertEqual(parse_file("tago", "tago_badkey.json"), "ERR AUTH 30\nLABEL BUS\n")
        self.assertEqual(parse_file("daejeon", "daejeon_badkey.xml"), "ERR AUTH 30\nLABEL BUS\n")


def dj(*items):
    rows = "".join(
        "<itemList><ROUTE_NO>%s</ROUTE_NO><MSG_TP>%s</MSG_TP><EXTIME_MIN>%s</EXTIME_MIN>"
        "<EXTIME_SEC>%s</EXTIME_SEC><STATUS_POS>%s</STATUS_POS></itemList>" % it for it in items)
    return ("<?xml version=\"1.0\"?><ServiceResult><msgHeader><headerCd>0</headerCd>"
            "</msgHeader><msgBody>%s</msgBody></ServiceResult>" % rows)


class WaitingBusTest(unittest.TestCase):
    def test_midnight(self):
        self.assertEqual(busfetch.waiting_bus("102", 10, 2, hms("23:50"), 60),
                         ("102", 20 * 60 + 120, 2, "WAIT", 120))     # 00:10 tomorrow
        # 23:55 was 10 minutes ago: still "about to leave", only the drive is left.
        self.assertEqual(busfetch.waiting_bus("102", 2355, 2, hms("00:05"), 60),
                         ("102", 120, 2, "WAIT", 120))

    def test_departure_passed(self):
        self.assertEqual(busfetch.waiting_bus("102", 1509, 2, hms("15:10:30"), 60)[1], 120)
        self.assertIsNone(busfetch.waiting_bus("102", 1449, 2, hms("15:10:30"), 60))

    def test_invalid_hhmm_and_unknown_stops(self):
        self.assertIsNone(busfetch.waiting_bus("102", 2460, 2, hms("12:00"), 60))
        self.assertIsNone(busfetch.waiting_bus("102", 1275, 2, hms("12:00"), 60))
        self.assertEqual(busfetch.waiting_bus("102", 1210, -1, hms("12:00"), 60)[1:], (600, -1, "WAIT", 0))

    def test_stop_secs_setting(self):
        self.assertEqual(busfetch.waiting_bus("102", 1210, 2, hms("12:00"), 90)[1], 600 + 180)

    def test_unknown_msg_tp(self):
        notes = set()
        buses = busfetch.parse_daejeon(dj(("102", "04", "5", "300", "3"),
                                          ("104", "02", "1", "1230", "2"),
                                          ("113", "09", "1", "9999", "2")),
                                       hms("12:00"), 60, notes)
        self.assertEqual(buses, [("102", 300, 3, "RUN", 0), ("104", 1920, 2, "WAIT", 120)])
        self.assertEqual(notes, {"msg_tp=04", "msg_tp=02", "msg_tp=09"})
        out = io.StringIO()
        busfetch.emit(out, busfetch.select(buses), via="daejeon", notes=notes)
        self.assertIn("NOTE msg_tp=04\n", out.getvalue())

    def test_tago_cases(self):
        def tago(v, n, now):
            return busfetch.parse_tago(
                '{"response":{"header":{"resultCode":"00"},"body":{"items":{"item":'
                '{"routeno":102,"arrtime":%d,"arrprevstationcnt":%d}}}}}' % (v, n), hms(now))
        self.assertEqual(tago(1512, 5, "15:00")[0][3], "WAIT")   # 302 s per stop
        self.assertEqual(tago(815, 5, "08:00"), [])              # ambiguous: blank
        self.assertEqual(tago(29, 1, "00:20"), [("102", 29, 1, "RUN", 0)])

    def test_routes(self):
        self.assertEqual(busfetch.clean_route("급행1"), "E1")
        self.assertEqual(busfetch.clean_route("마을 1"), "M1")
        self.assertEqual(busfetch.clean_route(" 102 "), "102")
        self.assertEqual(busfetch.clean_route("12345"), "1234")
        self.assertEqual(busfetch.clean_route("b-1"), "B-1")
        self.assertEqual(busfetch.clean_route("순환"), "?")

    def test_at_most_twelve_buses(self):
        buses = [(str(100 + i), 1000 - i, 3, "RUN", 0) for i in range(20)]
        selected = busfetch.select(buses)
        self.assertEqual(len(selected), 12)
        self.assertEqual(selected[0], ("119", 981, 3, "RUN", 0))


class SafetyTest(unittest.TestCase):
    def test_https_only_opener(self):
        handlers = {type(h) for h in busfetch.https_opener().handlers}
        for bad in (urllib.request.HTTPRedirectHandler, urllib.request.ProxyHandler,
                    urllib.request.HTTPHandler, urllib.request.FTPHandler,
                    urllib.request.FileHandler):
            self.assertNotIn(bad, handlers)
        self.assertIn(urllib.request.HTTPSHandler, handlers)
        with self.assertRaises(busfetch.FetchError) as cm:
            busfetch.http_get("http://apis.data.go.kr/x", {"serviceKey": "k"}, 1)
        self.assertEqual(cm.exception.kind, "NET")

    def test_large_and_doctype_responses(self):
        class Fake:
            def __init__(self, body):
                self.body = body
            def __enter__(self):
                return self
            def __exit__(self, *a):
                return False
            def read(self, n):
                return self.body[:n]
        opener = mock.Mock()
        opener.open.return_value = Fake(b"x" * (busfetch.MAX_RESPONSE + 10))
        with mock.patch.object(busfetch, "https_opener", return_value=opener):
            with self.assertRaises(busfetch.FetchError) as cm:
                busfetch.http_get(busfetch.DAEJEON_URL, {}, 1)
        self.assertEqual((cm.exception.kind, cm.exception.detail), ("PARSE", "too_large"))
        with self.assertRaises(busfetch.FetchError) as cm:
            busfetch.parse_daejeon('<?xml version="1.0"?><!DOCTYPE x [<!ENTITY a "b">]><x/>')
        self.assertEqual(cm.exception.detail, "doctype")
        with self.assertRaises(busfetch.FetchError) as cm:
            busfetch.parse_tago("<html>gateway timeout</html>")
        self.assertEqual(cm.exception.kind, "PARSE")

    def test_conf_numbers(self):
        for value, want in (("abc", 8.0), ("nan", 8.0), ("inf", 8.0), ("0", 1.0), ("3", 3.0)):
            self.assertEqual(busfetch.conf_number({"TIMEOUT": value}, "TIMEOUT", 1.0, 8.0, float),
                             want, value)
        self.assertEqual(busfetch.conf_number({"WAIT_STOP_SECS": "999"}, "WAIT_STOP_SECS", 0, 300), 300)

    def test_errors_are_one_safe_line(self):
        out = io.StringIO()
        busfetch.emit(out, [], busfetch.FetchError("NET", "bad thing\nserviceKey=abc"))
        self.assertEqual(out.getvalue(), "ERR NET bad_thing_serviceKey=abc\nLABEL BUS\n")

    def test_crash_prints_only_the_exception_name(self):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(busfetch, "run", side_effect=RuntimeError("secret key abc")), \
                mock.patch.object(sys, "stdout", out), contextlib.redirect_stderr(err):
            busfetch.main()
        self.assertEqual(out.getvalue(), "ERR PARSE RuntimeError\nLABEL BUS\n")
        self.assertEqual(err.getvalue(), "")

    def test_refuses_root(self):
        out = io.StringIO()
        with mock.patch.object(busfetch.os, "geteuid", return_value=0):
            busfetch.run(["busfetch.py"], out)
        self.assertEqual(out.getvalue(), "ERR CONF refuse_root\nLABEL BUS\n")

    def test_failed_providers_print_nothing_to_stderr(self):
        d = tempfile.mkdtemp()
        self.addCleanup(lambda: (os.remove(os.path.join(d, "bus.conf")), os.rmdir(d)))
        with open(os.path.join(d, "bus.conf"), "w") as f:
            f.write("SERVICE_KEY=abc\n")
        out, err = io.StringIO(), io.StringIO()
        boom = mock.Mock(side_effect=busfetch.FetchError("NET", "URLError"))
        with mock.patch.dict(busfetch.PROVIDERS, {"daejeon": (boom, None), "tago": (boom, None)}), \
                mock.patch.object(busfetch, "CONF_PATH", os.path.join(d, "bus.conf")), \
                contextlib.redirect_stderr(err):
            busfetch.run(["busfetch.py"], out)
        self.assertEqual(out.getvalue(), "ERR NET URLError\nLABEL BUS\n")
        self.assertEqual(err.getvalue(), "")
        self.assertEqual(boom.call_count, 2)


ROUTE_FIXTURES = {"30300037": "102", "30300039": "104", "30300005": "11",
                  "30300045": "113", "30300108": "117", "30300127": "212"}


class RouteInfoTest(unittest.TestCase):
    """노선정보조회 (headways for the next-bus estimate), saved 2026-11-21 17:43."""

    def test_parse_route_info(self):
        self.assertEqual(busfetch.parse_route_info(read("daejeon_route_102.xml")),
                         ("102", (8, 8, 10), (2235, 2235, 2235)))
        self.assertEqual(busfetch.parse_route_info(read("daejeon_route_104.xml")),
                         ("104", (23, 23, 24), (2230, 2231, 2231)))      # TURN_END differs
        self.assertEqual(busfetch.parse_route_info(read("daejeon_route_212.xml"))[2],
                         (2300, 2300, 2300))                              # TURN_END > ORIGIN_END
        self.assertEqual(busfetch.parse_route_info(read("daejeon_route_11.xml"))[1], (134, 134, 134))

    def test_invalid_values_are_zero(self):
        xml = ('<?xml version="1.0"?><ServiceResult><msgHeader><headerCd>0</headerCd></msgHeader>'
               '<msgBody><itemList><ROUTE_NO>102</ROUTE_NO><ALLO_INTERVAL>abc</ALLO_INTERVAL>'
               '<ALLO_INTERVAL_SAT>0</ALLO_INTERVAL_SAT><ALLO_INTERVAL_SUN>9999</ALLO_INTERVAL_SUN>'
               '<ORIGIN_END>2575</ORIGIN_END><TURN_END_SAT>0030</TURN_END_SAT>'
               '<ORIGIN_END_SAT>2300</ORIGIN_END_SAT></itemList></msgBody></ServiceResult>')
        self.assertEqual(busfetch.parse_route_info(xml), ("102", (0, 0, 0), (0, 30, 0)))

    def test_route_codes_from_the_arrival_answer(self):
        extra = {}
        busfetch.parse_daejeon(read("daejeon_1743.xml"), hms("17:43:10"), 60, set(), extra)
        self.assertEqual(extra["route_cds"], ROUTE_FIXTURES)
        extra = {}
        busfetch.parse_tago(read("tago_many.json"), hms("14:05:40"), 60, set(), extra)
        self.assertEqual(extra["route_cds"]["30300037"], "102")

    def test_route_files_in_parse_mode(self):
        out = io.StringIO()
        busfetch.run(["busfetch.py", "--parse-file", "daejeon",
                      os.path.join(FIXTURES, "daejeon_1743.xml"), "--now", "17:43:10",
                      "--route-file", os.path.join(FIXTURES, "daejeon_route_102.xml"),
                      "--route-file", os.path.join(FIXTURES, "daejeon_badkey.xml")], out)
        text = out.getvalue()
        self.assertTrue(text.startswith("OK 6\n"))
        self.assertTrue(text.endswith("ROUTE 102 8 8 10 2235 2235 2235\nNOTE routes=AUTH\n"))


class LiveRunTest(unittest.TestCase):
    """busfetch.run() with the HTTP layer replaced by the saved responses."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.conf = os.path.join(self.dir, "bus.conf")
        with open(self.conf, "w") as f:
            f.write("SERVICE_KEY=abc\n")
        self.calls = []
        self.route_reply = lambda cd: read("daejeon_route_%s.xml" % ROUTE_FIXTURES[cd])

    def tearDown(self):
        os.remove(self.conf)
        os.rmdir(self.dir)

    def fake_get(self, url, params, timeout):
        self.calls.append((url, params.get("busRouteId"), timeout))
        if url == busfetch.DAEJEON_URL:
            return read("daejeon_1743.xml")
        if url == busfetch.ROUTE_INFO_URL:
            return self.route_reply(params["busRouteId"])
        raise AssertionError(url)

    def run_live(self, *args):
        out = io.StringIO()
        with mock.patch.object(busfetch, "http_get", side_effect=self.fake_get), \
                mock.patch.object(busfetch, "CONF_PATH", self.conf), \
                mock.patch.object(busfetch, "now_kst_sod", return_value=hms("17:43:10")):
            busfetch.run(["busfetch.py"] + list(args), out)
        return out.getvalue()

    def test_without_routes_one_call(self):
        text = self.run_live()
        self.assertEqual(len(self.calls), 1)
        self.assertNotIn("ROUTE", text)

    def test_with_routes(self):
        text = self.run_live("--routes")
        self.assertTrue(text.startswith("OK 6\n"))
        self.assertEqual(len(self.calls), 7)
        self.assertTrue(all(t <= busfetch.ROUTE_CALL_TIMEOUT for _, cd, t in self.calls if cd))
        self.assertIn("\nROUTE 102 8 8 10 2235 2235 2235\n", text)
        self.assertEqual(text.count("\nROUTE "), 6)
        self.assertLess(text.index("BUS 212"), text.index("ROUTE"))   # arrivals come first

    def test_route_auth_failure_keeps_the_arrivals(self):
        self.route_reply = lambda cd: (_ for _ in ()).throw(busfetch.FetchError("AUTH", "30"))
        text = self.run_live("--routes")
        self.assertEqual(len(self.calls), 2)                      # stops after the first AUTH
        self.assertTrue(text.startswith("OK 6\n"))
        self.assertTrue(text.endswith("NOTE routes=AUTH\n"))

    def test_time_budget_gives_partial_results(self):
        clock = [0.0]
        def slow_get(url, params, timeout):
            clock[0] += 3.0
            return self.fake_get(url, params, timeout)
        with mock.patch.object(busfetch.time, "monotonic", side_effect=lambda: clock[0]), \
                mock.patch.object(busfetch, "http_get", side_effect=slow_get):
            notes = set()
            routes = busfetch.fetch_routes({"SERVICE_KEY": "abc"}, ROUTE_FIXTURES, notes, 0.0)
        self.assertEqual(len(routes), 4)                          # 0, 3, 6, 9 s; budget 12 s
        self.assertEqual(notes, {"routes=TIMEOUT"})


class ConfTest(unittest.TestCase):
    def write_conf(self, data):
        fd, path = tempfile.mkstemp()
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        self.addCleanup(os.remove, path)
        return path

    def test_missing_file(self):
        with self.assertRaises(busfetch.FetchError) as cm:
            busfetch.load_conf("/nonexistent/bus.conf")
        self.assertEqual(cm.exception.kind, "CONF")

    def test_empty_key(self):
        with self.assertRaises(busfetch.FetchError) as cm:
            busfetch.load_conf(self.write_conf(b"# comment\nSERVICE_KEY=\n"))
        self.assertEqual(cm.exception.detail, "no_SERVICE_KEY")

    def test_values_defaults_and_broken_bytes(self):
        conf = busfetch.load_conf(self.write_conf(
            b'SERVICE_KEY="abc"\r\nproviders = tago\n\xff\xfe junk\nROUTES=102\n'))
        self.assertEqual(conf["SERVICE_KEY"], "abc")
        self.assertEqual(conf["PROVIDERS"], "tago")
        self.assertEqual(conf["NODE_ID"], "DJB8002920")

    def test_encoding_key_is_decoded_once(self):
        self.assertEqual(busfetch.service_key({"SERVICE_KEY": "a%2Bb%3D%3D"}), "a+b==")
        self.assertEqual(busfetch.service_key({"SERVICE_KEY": "a+b=="}), "a+b==")


if __name__ == "__main__":
    unittest.main(verbosity=1)
