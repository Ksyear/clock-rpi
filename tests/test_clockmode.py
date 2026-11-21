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
DEFAULT_DUMP = "mode=rotate route=102 alt_clock=15 alt_bus=5 rotate=3"


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

    def write(self, text):
        with open(self.path, "w", encoding="utf-8") as f:
            f.write(text)

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
        self.run_cmd("1", "--clock", "20", "--bus", "8")
        self.run_cmd("3", "--route", "104")
        self.assertEqual(self.dump(), "mode=fixed route=104 alt_clock=20 alt_bus=8 rotate=3")
        self.run_cmd("--rotate", "4")  # value only, mode unchanged
        self.assertEqual(self.dump(), "mode=fixed route=104 alt_clock=20 alt_bus=8 rotate=4")
        self.run_cmd("--defaults")
        self.assertEqual(self.dump(), "mode=fixed route=102 alt_clock=15 alt_bus=5 rotate=3")

    def test_out_of_range_leaves_the_file_alone(self):
        self.run_cmd("1")
        with open(self.path, "rb") as f:
            before = f.read()
        for args in (("--clock", "4"), ("--clock", "301"), ("--bus", "x"), ("--rotate", "1"),
                     ("--route", "12345"), ("--route", "1 2"), ("9",)):
            r = self.run_cmd(*args)
            self.assertEqual(r.returncode, 2, args)
        with open(self.path, "rb") as f:
            self.assertEqual(f.read(), before)

    def test_file_is_readable_by_daemon(self):
        self.run_cmd("2", umask=0o077)
        mode = stat.S_IMODE(os.stat(self.path).st_mode)
        self.assertEqual(mode, 0o644)
        self.assertFalse(os.path.exists(self.path + ".tmp"))

    def test_hand_edited_and_broken_files(self):
        self.write("MODE = Alternate   # comment\nroute=b1\nALT_CLOCK_SECS=1000\nROTATE_SECS=abc\n")
        self.assertEqual(self.dump(), "mode=alternate route=B1 alt_clock=300 alt_bus=5 rotate=3")
        self.write("MODE=7\nROUTE=too_long\nALT_BUS_SECS=1\nALT_CLOCK_SECS=5_0\n")
        self.assertEqual(self.dump(), "mode=rotate route=102 alt_clock=15 alt_bus=2 rotate=3")
        self.write("\x00\xff garbage")
        self.assertEqual(self.dump(), DEFAULT_DUMP)

    def test_status_text(self):
        self.run_cmd("1", "--clock", "20")
        out = self.run_cmd().stdout
        self.assertIn("현재 모드: 1 (alternate)", out)
        self.assertIn("시계 20초", out)


if __name__ == "__main__":
    unittest.main(verbosity=1)
