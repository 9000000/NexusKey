import unittest
import tempfile
from pathlib import Path
from tools.rebrand.lib.scope import file_in_scope, iter_files


class TestScope(unittest.TestCase):
    def test_include_matches(self):
        self.assertTrue(file_in_scope(Path("src/a.cpp"),
                                       ["src/**/*.cpp"], []))

    def test_exclude_overrides_include(self):
        self.assertFalse(file_in_scope(Path("docs/plans/x.md"),
                                        ["**/*.md"], ["docs/plans/**"]))

    def test_brace_glob(self):
        self.assertTrue(file_in_scope(Path("src/a.rc"),
                                       ["src/**/*.{cpp,h,rc}"], []))
        self.assertFalse(file_in_scope(Path("src/a.txt"),
                                        ["src/**/*.{cpp,h,rc}"], []))

    def test_iter_files(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "src").mkdir()
            (root / "docs").mkdir()
            (root / "src" / "a.cpp").write_text("x")
            (root / "src" / "b.txt").write_text("x")
            (root / "docs" / "skip.md").write_text("x")
            files = sorted(iter_files(root, ["src/**/*.cpp"], []))
            self.assertEqual(files, [Path("src/a.cpp")])
