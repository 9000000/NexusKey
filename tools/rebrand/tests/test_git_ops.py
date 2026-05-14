import subprocess
import tempfile
import unittest
from pathlib import Path
from tools.rebrand.lib.git_ops import is_clean, stage, commit, move
from tools.rebrand.lib.smoke import detect_test_target


class TestGitOps(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        subprocess.run(["git", "init", "-q", "-b", "main"], cwd=self.tmp, check=True)
        subprocess.run(["git", "config", "user.email", "t@t"], cwd=self.tmp, check=True)
        subprocess.run(["git", "config", "user.name", "T"], cwd=self.tmp, check=True)
        (self.tmp / "a.txt").write_text("hello")
        subprocess.run(["git", "add", "."], cwd=self.tmp, check=True)
        subprocess.run(["git", "commit", "-q", "-m", "init"], cwd=self.tmp, check=True)

    def test_is_clean(self):
        self.assertTrue(is_clean(self.tmp))
        (self.tmp / "a.txt").write_text("dirty")
        self.assertFalse(is_clean(self.tmp))

    def test_stage_and_commit(self):
        (self.tmp / "a.txt").write_text("changed")
        stage(self.tmp, [Path("a.txt")])
        commit(self.tmp, "test: change a")
        self.assertTrue(is_clean(self.tmp))

    def test_move(self):
        move(self.tmp, Path("a.txt"), Path("b.txt"))
        self.assertTrue((self.tmp / "b.txt").exists())
        self.assertFalse((self.tmp / "a.txt").exists())


class TestSmoke(unittest.TestCase):
    def test_detect_test_target_v_prefix(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "CMakeLists.txt"
            p.write_text("add_executable(VKeyTests EXCLUDE_FROM_ALL ...)\n")
            self.assertEqual(detect_test_target(p), "VKeyTests")

    def test_detect_test_target_legacy(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "CMakeLists.txt"
            p.write_text("add_executable(NextKeyTests EXCLUDE_FROM_ALL ...)\n")
            self.assertEqual(detect_test_target(p), "NextKeyTests")
