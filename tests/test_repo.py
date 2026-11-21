"""Repository hygiene: the service key must never end up in git.

Run from the project directory:  python3 -I tests/test_repo.py  (or: make check)
Skipped when git or the .git directory is not available (e.g. a tarball).
"""

import os
import re
import shutil
import subprocess
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# data.go.kr "일반 인증키": 64 hex characters (or its URL-encoded/base64 forms).
KEY_LIKE = re.compile(rb"(?<![0-9a-fA-F])[0-9a-f]{64}(?![0-9a-fA-F])")


def git(*args):
    return subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True)


@unittest.skipUnless(shutil.which("git") and os.path.isdir(os.path.join(ROOT, ".git")),
                     "git repository not available")
class RepoTest(unittest.TestCase):
    def test_secret_files_are_ignored_and_untracked(self):
        tracked = git("ls-files").stdout.decode().split("\n")
        for name in ("bus.conf", "display.conf", ".bus.conf.swp", "bus.conf.bak",
                     ".display.conf.abc123"):
            self.assertNotIn(name, tracked)
            self.assertEqual(git("check-ignore", "-q", name).returncode, 0, name)
        self.assertIn("bus.conf.example", tracked)
        self.assertNotEqual(git("check-ignore", "-q", "bus.conf.example").returncode, 0)

    def test_no_key_like_strings_in_tracked_files(self):
        for name in git("ls-files", "-z").stdout.split(b"\0"):
            path = os.path.join(ROOT, name.decode())
            if not name or not os.path.isfile(path) or name.startswith(b"lib/"):
                continue
            with open(path, "rb") as f:
                self.assertIsNone(KEY_LIKE.search(f.read()), name)


if __name__ == "__main__":
    unittest.main(verbosity=1)
