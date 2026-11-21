"""Tests for busfetch.py using real API responses saved on 2026-11-21.

Run from the project directory:  python3 -I tests/test_busfetch.py  (or: make check)

The fixtures are responses for the 한밭대학교 stop toward 대전역동광장
(TAGO nodeId DJB8002920 / 대전시 BusStopID 8002920). *_badkey are the real
gateway answers to an invalid key. No service key is stored in them.
"""

import io
import os
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import busfetch  # noqa: E402

FIXTURES = os.path.join(ROOT, "tests", "fixtures")


def parse_file(provider, name):
    out = io.StringIO()
    busfetch.run(["busfetch.py", "--parse-file", provider, os.path.join(FIXTURES, name)], out)
    return out.getvalue()


class ParseTest(unittest.TestCase):
    def test_tago_every_route_soonest_first(self):
        self.assertEqual(parse_file("tago", "tago_many.json"),
                         "OK 5\nLABEL BUS\n"
                         "BUS 113 578 4\nBUS 102 1409 2\nBUS 104 1420 2\n"
                         "BUS 11 1610 2\nBUS 212 2250 25\n")

    def test_tago_single_item_is_an_object(self):
        self.assertEqual(parse_file("tago", "tago_single.json"),
                         "OK 1\nLABEL BUS\nBUS 102 1409 2\n")

    def test_tago_empty_items_is_an_empty_string(self):
        self.assertEqual(parse_file("tago", "tago_empty.json"), "OK 0\nLABEL BUS\n")

    def test_tago_bad_key(self):
        self.assertEqual(parse_file("tago", "tago_badkey.json"), "ERR AUTH 30\nLABEL BUS\n")

    def test_daejeon_every_route_soonest_first(self):
        self.assertEqual(parse_file("daejeon", "daejeon_many.xml"),
                         "OK 6\nLABEL BUS\n"
                         "BUS 113 359 3\nBUS 102 1409 2\nBUS 117 1418 5\n"
                         "BUS 104 1420 2\nBUS 11 1610 2\nBUS 212 2147 24\n")

    def test_daejeon_no_result(self):
        self.assertEqual(parse_file("daejeon", "daejeon_empty.xml"), "OK 0\nLABEL BUS\n")

    def test_daejeon_bad_key(self):
        self.assertEqual(parse_file("daejeon", "daejeon_badkey.xml"),
                         "ERR AUTH 30\nLABEL BUS\n")

    def test_garbage_is_a_parse_error(self):
        with self.assertRaises(busfetch.FetchError) as cm:
            busfetch.parse_tago("<html>gateway timeout</html>")
        self.assertEqual(cm.exception.kind, "PARSE")

    def test_at_most_twelve_buses(self):
        buses = [(str(100 + i), 1000 - i, 3) for i in range(20)]
        selected = busfetch.select(buses)
        self.assertEqual(len(selected), 12)
        self.assertEqual(selected[0], ("119", 981, 3))

    def test_clean_route(self):
        self.assertEqual(busfetch.clean_route("급행1"), "E1")
        self.assertEqual(busfetch.clean_route("마을 1"), "M1")
        self.assertEqual(busfetch.clean_route(" 102 "), "102")
        self.assertEqual(busfetch.clean_route("12345"), "1234")
        self.assertEqual(busfetch.clean_route("b-1"), "B-1")
        self.assertEqual(busfetch.clean_route("순환"), "?")


class ConfTest(unittest.TestCase):
    def write_conf(self, text):
        fd, path = tempfile.mkstemp()
        with os.fdopen(fd, "w") as f:
            f.write(text)
        self.addCleanup(os.remove, path)
        return path

    def test_missing_file(self):
        with self.assertRaises(busfetch.FetchError) as cm:
            busfetch.load_conf("/nonexistent/bus.conf")
        self.assertEqual(cm.exception.kind, "CONF")

    def test_empty_key(self):
        with self.assertRaises(busfetch.FetchError) as cm:
            busfetch.load_conf(self.write_conf("# comment\nSERVICE_KEY=\n"))
        self.assertEqual(cm.exception.detail, "no_SERVICE_KEY")

    def test_values_and_defaults(self):
        conf = busfetch.load_conf(self.write_conf('SERVICE_KEY="abc"\nproviders = tago\n'))
        self.assertEqual(conf["SERVICE_KEY"], "abc")
        self.assertEqual(conf["PROVIDERS"], "tago")
        self.assertEqual(conf["NODE_ID"], "DJB8002920")

    def test_old_routes_key_is_ignored(self):
        # bus.conf from the first version may still have ROUTES=102.
        conf = busfetch.load_conf(self.write_conf("SERVICE_KEY=abc\nROUTES=102\n"))
        with open(os.path.join(FIXTURES, "daejeon_many.xml"), encoding="utf-8") as f:
            self.assertEqual(len(busfetch.select(busfetch.parse_daejeon(f.read()))), 6)
        self.assertEqual(conf["SERVICE_KEY"], "abc")

    def test_encoding_key_is_decoded_once(self):
        self.assertEqual(busfetch.service_key({"SERVICE_KEY": "a%2Bb%3D%3D"}), "a+b==")
        self.assertEqual(busfetch.service_key({"SERVICE_KEY": "a+b=="}), "a+b==")


if __name__ == "__main__":
    unittest.main(verbosity=1)
