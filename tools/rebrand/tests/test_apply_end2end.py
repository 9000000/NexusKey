import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

APPLY = Path(__file__).parent.parent / "apply.py"


class TestApplyEndToEnd(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        subprocess.run(["git", "init", "-q", "-b", "main"], cwd=self.tmp, check=True)
        subprocess.run(["git", "config", "user.email", "t@t"], cwd=self.tmp, check=True)
        subprocess.run(["git", "config", "user.name", "T"], cwd=self.tmp, check=True)
        (self.tmp / "README.md").write_text("# NexusKey\n")
        subprocess.run(["git", "add", "."], cwd=self.tmp, check=True)
        subprocess.run(["git", "commit", "-q", "-m", "init"], cwd=self.tmp, check=True)

        self.plan = {
            "schema_version": 1,
            "scanned_at": "2026-05-14T00:00:00+00:00",
            "old_brand": "NexusKey", "new_brand": "VKey",
            "guids": {"old_clsid_text_service": "{o}",
                      "new_clsid_text_service": "{12345678-1234-1234-1234-123456789abc}",
                      "old_profile_guid": "{o2}",
                      "new_profile_guid": "{12345678-1234-1234-1234-123456789abd}"},
            "edits": [{"category": "BRAND_STRING", "file": "README.md",
                       "line": 1, "col": 2, "before": "# NexusKey",
                       "after": "# VKey", "match": "NexusKey",
                       "replacement": "VKey"}],
            "renames": [],
            "stats": {"BRAND_STRING": 1},
        }
        plan_dir = self.tmp / "tools" / "rebrand" / "out"
        plan_dir.mkdir(parents=True)
        (plan_dir / "rebrand_plan.json").write_text(json.dumps(self.plan))

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_dirty_tree_blocks(self):
        (self.tmp / "junk.txt").write_text("dirty")
        result = subprocess.run(
            [sys.executable, str(APPLY), "--category", "BRAND_STRING",
             "--root", str(self.tmp), "--skip-smoke"],
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clean", (result.stderr + result.stdout).lower())

    def test_apply_brand_string_commits(self):
        result = subprocess.run(
            [sys.executable, str(APPLY), "--category", "BRAND_STRING",
             "--root", str(self.tmp), "--skip-smoke"],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        readme = (self.tmp / "README.md").read_text()
        self.assertEqual(readme, "# VKey\n")
        log = subprocess.run(["git", "log", "--oneline"],
                              cwd=self.tmp, capture_output=True, text=True)
        self.assertIn("rebrand:", log.stdout.lower())

    def test_stale_plan_fails(self):
        (self.tmp / "README.md").write_text("# Different\n")
        subprocess.run(["git", "commit", "-aq", "-m", "drift"], cwd=self.tmp, check=True)
        result = subprocess.run(
            [sys.executable, str(APPLY), "--category", "BRAND_STRING",
             "--root", str(self.tmp), "--skip-smoke"],
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("stale", (result.stderr + result.stdout).lower())
