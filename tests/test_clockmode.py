"""Tests for the clockmode command.

Run from the project directory:  python3 -I tests/test_clockmode.py  (or: make check)

The C++ side (settings.cpp) must read display.conf exactly like clockmode does.
When ./preview exists, every case is also read back through `./preview --settings`.
"""

import os
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLOCKMODE = os.path.join(ROOT, "clockmode")
PREVIEW = os.path.join(ROOT, "preview")
DEFAULT_DUMP = ("mode=rotate route=102 alt_clock=15 alt_bus=8 rotate=10 max=15 min=3 "
                "quiet=01:00-05:00")


class ClockModeTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "display.conf")

    def tearDown(self):
        for name in os.listdir(self.dir):
            os.remove(os.path.join(self.dir, name))
        os.rmdir(self.dir)

    def run_cmd(self, *args, umask=None):
        def preexec():
            if umask is not None:
                os.umask(umask)
        return subprocess.run([sys.executable, "-I", CLOCKMODE, "--file", self.path] + list(args),
                              capture_output=True, text=True, preexec_fn=preexec)

    def dump(self):
        r = self.run_cmd("--dump")
        self.assertEqual(r.returncode, 0, r.stderr)
        value = r.stdout.strip()
        if os.path.exists(PREVIEW):  # the clock must read the file the same way
            c = subprocess.run([PREVIEW, "--settings", self.path], capture_output=True, text=True)
            self.assertEqual(c.stdout.strip(), value)
        return value

    def write(self, data):
        with open(self.path, "wb") as f:
            f.write(data if isinstance(data, bytes) else data.encode("utf-8"))

    def test_defaults_without_file(self):
        self.assertEqual(self.dump(), DEFAULT_DUMP)
        self.assertFalse(os.path.exists(self.path))
        self.assertIn("기본값", self.run_cmd().stdout)

    def test_every_mode_and_alias(self):
        for arg, name in (("0", "off"), ("off", "off"), ("1", "alternate"),
                          ("alternate", "alternate"), ("2", "rotate"), ("rotate", "rotate"),
                          ("3", "fixed"), ("fixed", "fixed")):
            r = self.run_cmd(arg)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue(self.dump().startswith("mode=%s " % name), arg)

    def test_values_are_kept(self):
        self.run_cmd("1", "--clock", "20", "--bus", "10")
        self.run_cmd("3", "--route", "104")
        self.run_cmd("--max", "20", "--rotate", "60", "--quiet", "23:30-5:30", "--min", "5")
        self.assertEqual(self.dump(), "mode=fixed route=104 alt_clock=20 alt_bus=10 rotate=60 "
                                      "max=20 min=5 quiet=23:30-05:30")
        self.run_cmd("--within", "25", "--min", "0")   # --within is the old name of --max
        self.assertIn(" max=25 min=0 ", self.dump())
        self.run_cmd("--quiet", "off")
        self.assertTrue(self.dump().endswith("quiet=off"))
        self.run_cmd("--defaults")
        self.assertEqual(self.dump(), DEFAULT_DUMP.replace("mode=rotate", "mode=fixed"))

    def test_out_of_range_leaves_the_file_alone(self):
        self.run_cmd("1")
        with open(self.path, "rb") as f:
            before = f.read()
        for args in (("--clock", "4"), ("--clock", "301"), ("--bus", "x"), ("--rotate", "1"),
                     ("--rotate", "61"), ("--within", "4"), ("--within", "100"),
                     ("--route", "12345"), ("--route", "1 2"), ("9",),
                     ("--quiet", "25:00-05:00"), ("--quiet", "05:00-05:00"),
                     ("--quiet", "0100-0500"), ("--quiet", "05:00-01:00"), ("--clock", "5_0"),
                     ("--min", "11"), ("--min", "-1"), ("--max", "5", "--min", "5")):
            r = self.run_cmd(*args)
            self.assertEqual(r.returncode, 2, args)
        with open(self.path, "rb") as f:
            self.assertEqual(f.read(), before)

    def test_file_is_readable_by_daemon_and_no_temp_left(self):
        self.run_cmd("2", umask=0o077)
        self.assertEqual(stat.S_IMODE(os.stat(self.path).st_mode), 0o644)
        self.assertEqual(os.listdir(self.dir), ["display.conf"])

    def test_hand_edited_and_broken_files(self):
        self.write("MODE = Alternate   # comment\nroute=b1\nALT_CLOCK_SECS=1000\nROTATE_SECS=abc\n"
                   "QUIET = 23:00-06:00 # night\n")
        self.assertEqual(self.dump(), "mode=alternate route=B1 alt_clock=300 alt_bus=8 rotate=10 "
                                      "max=15 min=3 quiet=23:00-06:00")
        self.write("MODE=7\nROUTE=too_long\nALT_BUS_SECS=1\nALT_CLOCK_SECS=5_0\nQUIET=garbage\n"
                   "ROTATE_MIN_MINS=99\n")
        self.assertEqual(self.dump(), "mode=rotate route=102 alt_clock=15 alt_bus=2 rotate=10 "
                                      "max=15 min=10 quiet=01:00-05:00")

    def test_bytes_that_used_to_crash_or_differ(self):
        cases = [
            b"MODE=1\n\xff\xfe broken utf-8\n",                 # crashed clockmode before
            b"MODE=1\rROUTE=104\n",                             # CR is not a line break
            b"MODE=1\x0bROUTE=104\n",                           # VT
            b"MODE=1\xc2\x85ROUTE=104\n",                       # U+0085
            b"MODE=1\x1cROUTE=104\n",                           # U+001C
            b"ROUTE=\xc2\xa0104\n",                             # NBSP is not a space
            b"MODE=\xc4\xb1\n",                                 # dotless i
            b"ROUTE=\xc5\xbf1\n",                               # long s
            b"ALT_CLOCK_SECS=2\x000\n",                         # NUL inside a number
            b"MODE=1\n" + b"#" * 5000 + b"\nMODE=3\n",          # past the 4 KB limit
        ]
        for data in cases:
            self.write(data)
            self.dump()  # parity with the clock is checked inside
            r = self.run_cmd()
            self.assertEqual(r.returncode, 0, (data, r.stderr))

    def test_status_text(self):
        self.run_cmd("1", "--clock", "20")
        out = self.run_cmd("--now", "14:00").stdout
        self.assertIn("현재 모드: 1 (alternate)", out)
        self.assertIn("시계 20초", out)
        self.assertIn("지금은 동작 중", out)
        self.assertIn("지금은 중지 시간입니다", self.run_cmd("--now", "03:00").stdout)


if __name__ == "__main__":
    unittest.main(verbosity=1)
