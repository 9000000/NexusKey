import unittest
from tools.rebrand.lib.matcher import find_matches, Match


class TestMatcher(unittest.TestCase):
    def setUp(self):
        self.repl = {
            "NexusKey": "VKey",
            "NexusKey.exe": "VKey.exe",
            "NEXUSKEY": "VKEY",
            "nexuskey": "vkey",
        }

    def test_longest_first(self):
        hits = list(find_matches('NexusKey.exe', self.repl))
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0].match, "NexusKey.exe")
        self.assertEqual(hits[0].replacement, "VKey.exe")
        self.assertEqual(hits[0].col, 0)

    def test_case_sensitive(self):
        hits = list(find_matches('Foo NEXUSKEY bar', self.repl))
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0].match, "NEXUSKEY")

    def test_multiple_per_line(self):
        hits = list(find_matches('a NexusKey b NexusKey c', self.repl))
        self.assertEqual(len(hits), 2)
        self.assertEqual([h.col for h in hits], [2, 13])

    def test_no_overlap(self):
        # After matching NexusKey.exe at col 0, no second match starts inside it
        hits = list(find_matches('NexusKey.exe and NexusKey', self.repl))
        self.assertEqual(len(hits), 2)
        self.assertEqual(hits[0].match, "NexusKey.exe")
        self.assertEqual(hits[1].match, "NexusKey")

    def test_empty_key_does_not_hang(self):
        # Defensive: an accidental "" entry in rules.toml must not infinite-loop
        bad = {"": "x", "NexusKey": "VKey"}
        hits = list(find_matches("NexusKey here", bad))
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0].match, "NexusKey")
