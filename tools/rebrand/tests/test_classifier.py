import unittest
from pathlib import Path
from tools.rebrand.lib.classifier import classify, Category, ClassifierContext


class TestClassifier(unittest.TestCase):
    def setUp(self):
        self.ctx = ClassifierContext(
            file_renames={"src/app/NexusKey.rc": "src/app/VKey.rc"},
            namespace_keep_regex=[r"\bNextKey::", r"\bNEXTKEY_LOG\s*\("],
            brand_to_binary_regex=[r"--target\s+(NextKey|NexusKey)\w*"],
        )

    def test_excluded_path_returns_historical(self):
        cat = classify(Path("docs/plans/old.md"), 1, "NexusKey", "NexusKey",
                       in_guid_window=False, ctx=self.ctx, was_excluded=True)
        self.assertEqual(cat, Category.HISTORICAL_KEEP)

    def test_namespace_keep_in_cpp(self):
        cat = classify(Path("src/foo.cpp"), 1,
                       "auto x = NextKey::TSF::Foo();", "NextKey::",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.NAMESPACE_KEEP)

    def test_guid_window(self):
        cat = classify(Path("src/tsf/Globals.cpp"), 19,
                       "DEFINE_GUID(CLSID_TextService,", "NexusKey",
                       in_guid_window=True, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.PERSISTENT_ID)

    def test_runtime_ipc_string(self):
        line = 'L"Local\\\\NexusKeySharedState"'
        cat = classify(Path("src/core/ipc/SharedStateManager.cpp"), 14,
                       line, "NexusKey",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.RUNTIME_IPC)

    def test_binary_name_in_cpp(self):
        line = 'L"NexusKey.exe"'
        cat = classify(Path("src/app/system/UpdateInstaller.cpp"), 346,
                       line, "NexusKey.exe",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.BINARY_NAME)

    def test_cmake_is_binary_name(self):
        cat = classify(Path("CMakeLists.txt"), 51,
                       "add_library(NextKeyEngine STATIC ...)", "NextKeyEngine",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.BINARY_NAME)

    def test_md_default_brand_string(self):
        cat = classify(Path("README.md"), 1, "# NexusKey", "NexusKey",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.BRAND_STRING)

    def test_md_with_exe_recategorized(self):
        cat = classify(Path("docs/TODO.md"), 362,
                       "taskkill /F /IM NexusKeyWatchdog.exe",
                       "NexusKeyWatchdog.exe",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.BINARY_NAME)

    def test_md_with_target_recategorized(self):
        cat = classify(Path("CLAUDE.md"), 30,
                       "cmake --build build --target NextKeyApp", "NextKeyApp",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.BINARY_NAME)

    def test_yml_artifact(self):
        cat = classify(Path(".github/workflows/build.yml"), 90,
                       "Compress-Archive -DestinationPath NexusKey.zip",
                       "NexusKey.zip",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.BINARY_NAME)

    def test_yml_placeholder_text(self):
        cat = classify(Path(".github/ISSUE_TEMPLATE/feature_request.yml"), 11,
                       "placeholder: Bạn muốn NexusKey có thêm tính năng gì?",
                       "NexusKey",
                       in_guid_window=False, ctx=self.ctx, was_excluded=False)
        self.assertEqual(cat, Category.BRAND_STRING)
