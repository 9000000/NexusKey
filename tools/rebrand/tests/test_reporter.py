import unittest
from tools.rebrand.lib.reporter import render_report


class TestReporter(unittest.TestCase):
    def test_summary_table(self):
        plan = {
            "scanned_at": "2026-05-14T00:00:00+00:00",
            "old_brand": "NexusKey",
            "new_brand": "VKey",
            "guids": {
                "old_clsid_text_service": "{old}",
                "new_clsid_text_service": "{new}",
                "old_profile_guid": "{old2}",
                "new_profile_guid": "{new2}",
            },
            "edits": [
                {"category": "BRAND_STRING", "file": "README.md", "line": 1,
                 "col": 2, "before": "# NexusKey", "after": "# VKey",
                 "match": "NexusKey", "replacement": "VKey"},
            ],
            "renames": [{"from": "a.rc", "to": "b.rc"}],
            "stats": {"BRAND_STRING": 1},
        }
        md = render_report(plan)
        self.assertIn("Rebrand Scan Report", md)
        self.assertIn("BRAND_STRING", md)
        self.assertIn("README.md:1", md)
        self.assertIn("UNCLASSIFIED", md)

    def test_unclassified_marked_blocking(self):
        plan = {
            "scanned_at": "2026-05-14T00:00:00+00:00",
            "old_brand": "NexusKey", "new_brand": "VKey",
            "guids": {"old_clsid_text_service": "{o}",
                      "new_clsid_text_service": "TBD-uuidgen",
                      "old_profile_guid": "{o2}",
                      "new_profile_guid": "TBD-uuidgen"},
            "edits": [], "renames": [],
            "stats": {"UNCLASSIFIED": 2},
        }
        md = render_report(plan)
        self.assertIn("BLOCKING", md)
        self.assertIn("TBD-uuidgen", md)
