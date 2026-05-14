import unittest
from pathlib import Path
import tempfile
from tools.rebrand.lib.config import load_rules, ConfigError


class TestConfig(unittest.TestCase):
    def _write(self, content: str) -> Path:
        f = tempfile.NamedTemporaryFile("w", suffix=".toml", delete=False)
        f.write(content)
        f.close()
        return Path(f.name)

    def test_minimal_valid_rules(self):
        toml = """
[meta]
old_brand = "NexusKey"
new_brand = "VKey"
keep_namespace = "NextKey"
keep_log_macro = "NEXTKEY_LOG"

[scope]
include = ["**/*.md"]
exclude = []

[replacements.brand_string]
"NexusKey" = "VKey"

[replacements.binary_name]

[replacements.runtime_ipc]

[replacements.persistent_id]

[replacements.release_artifact]

[persistent_id.guids]
old_clsid_text_service = "old"
new_clsid_text_service = "{12345678-1234-1234-1234-123456789abc}"
old_profile_guid = "old"
new_profile_guid = "{12345678-1234-1234-1234-123456789abd}"

[exclude_patterns.namespace_keep]
regex = ['\\bNextKey::']

[recategorize]
brand_to_binary_regex = []

[file_renames]
"src/old.rc" = "src/new.rc"
"""
        cfg = load_rules(self._write(toml))
        self.assertEqual(cfg.old_brand, "NexusKey")
        self.assertEqual(cfg.new_brand, "VKey")
        self.assertIn("NexusKey", cfg.replacements)
        self.assertEqual(cfg.replacements["NexusKey"], "VKey")
        self.assertIn("**/*.md", cfg.scope_include)

    def test_missing_meta_fails(self):
        with self.assertRaises(ConfigError):
            load_rules(self._write("[scope]\ninclude=[]\nexclude=[]\n"))

    def test_unfilled_guid_fails_validate(self):
        toml = """
[meta]
old_brand="N"
new_brand="V"
keep_namespace="X"
keep_log_macro="Y"
[scope]
include=[]
exclude=[]
[replacements.brand_string]
[replacements.binary_name]
[replacements.runtime_ipc]
[replacements.persistent_id]
[replacements.release_artifact]
[persistent_id.guids]
old_clsid_text_service="o1"
new_clsid_text_service="TBD-uuidgen"
old_profile_guid="o2"
new_profile_guid="{12345678-1234-1234-1234-123456789abc}"
[exclude_patterns.namespace_keep]
regex=[]
[recategorize]
brand_to_binary_regex=[]
[file_renames]
"""
        cfg = load_rules(self._write(toml))
        with self.assertRaises(ConfigError):
            cfg.validate_for_apply()
