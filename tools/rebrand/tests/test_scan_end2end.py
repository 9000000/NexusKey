import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

FIXTURE = Path(__file__).parent / "fixtures" / "mini_repo"
SCAN = Path(__file__).parent.parent / "scan.py"


class TestScanEndToEnd(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        for item in FIXTURE.iterdir():
            if item.is_dir():
                shutil.copytree(item, self.tmp / item.name)
            else:
                shutil.copy(item, self.tmp / item.name)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_scan_produces_plan_json(self):
        result = subprocess.run(
            [sys.executable, str(SCAN),
             "--root", str(self.tmp),
             "--rules", str(self.tmp / "rules.toml"),
             "--out", str(self.tmp / "out")],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        plan = json.loads((self.tmp / "out" / "rebrand_plan.json").read_text())
        self.assertEqual(plan["old_brand"], "NexusKey")
        self.assertEqual(plan["new_brand"], "VKey")
        cats = {e["category"] for e in plan["edits"]}
        self.assertIn("BRAND_STRING", cats)
        self.assertIn("RUNTIME_IPC", cats)
        self.assertIn("BINARY_NAME", cats)
        for e in plan["edits"]:
            self.assertNotIn("docs/plans", e["file"])
        for e in plan["edits"]:
            self.assertNotIn("NextKey::", e["before"])

    def test_unclassified_blocks_emit(self):
        (self.tmp / "weird.xyz").write_text("NexusKey here")
        rules_path = self.tmp / "rules.toml"
        text = rules_path.read_text()
        text = text.replace(
            'include = ["**/*.md", "src/**/*.{cpp,h}", ".github/**/*.yml"]',
            'include = ["**/*.md", "src/**/*.{cpp,h}", ".github/**/*.yml", "*.xyz"]',
        )
        rules_path.write_text(text)
        result = subprocess.run(
            [sys.executable, str(SCAN),
             "--root", str(self.tmp),
             "--rules", str(rules_path),
             "--out", str(self.tmp / "out")],
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("UNCLASSIFIED", result.stderr + result.stdout)
