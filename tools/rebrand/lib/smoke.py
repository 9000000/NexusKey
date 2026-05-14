"""Detect gtest target in CMakeLists.txt and run a Linux smoke build."""
from __future__ import annotations
import re
import subprocess
from pathlib import Path


_TEST_TARGET_RE = re.compile(
    r"add_executable\(\s*((?:NextKey|VKey)Tests)\b"
)


def detect_test_target(cmake_file: Path) -> str:
    text = cmake_file.read_text(encoding="utf-8", errors="replace")
    m = _TEST_TARGET_RE.search(text)
    if not m:
        raise RuntimeError(
            f"Could not detect gtest target name in {cmake_file}. "
            f"Expected add_executable(NextKeyTests | VKeyTests ...)."
        )
    return m.group(1)


def run_smoke(root: Path, build_dir: Path = Path("build-linux")) -> None:
    target = detect_test_target(root / "CMakeLists.txt")
    subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", target],
        cwd=root, check=True,
    )
    subprocess.run(
        [str(build_dir / "tests" / target)],
        cwd=root, check=True,
    )
