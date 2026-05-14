#!/usr/bin/env python3
"""Scan repo, classify hits, emit REBRAND_REPORT.md + rebrand_plan.json."""
from __future__ import annotations
import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import List

# Make package importable when invoked as a script
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from tools.rebrand.lib.config import load_rules, Config
from tools.rebrand.lib.scope import iter_files
from tools.rebrand.lib.matcher import find_matches
from tools.rebrand.lib.classifier import classify, Category, ClassifierContext


@dataclass
class Edit:
    category: str
    file: str
    line: int
    col: int
    before: str
    after: str
    match: str
    replacement: str


def _guid_window_lines(content: str) -> set[int]:
    """Lines within 5 lines after a DEFINE_GUID(...) of CLSID_TextService or
    GUID_Profile are GUID windows."""
    out: set[int] = set()
    triggers = re.compile(
        r"DEFINE_GUID\s*\(\s*(CLSID_TextService|GUID_Profile)\b"
    )
    for i, line in enumerate(content.splitlines(), start=1):
        if triggers.search(line):
            for j in range(i, i + 6):
                out.add(j)
    return out


def scan(root: Path, cfg: Config) -> tuple[List[Edit], List[dict], Counter]:
    edits: List[Edit] = []
    renames = [{"from": k, "to": v} for k, v in cfg.file_renames.items()]
    stats: Counter = Counter()
    ctx = ClassifierContext(
        file_renames=cfg.file_renames,
        namespace_keep_regex=cfg.namespace_keep_regex,
        brand_to_binary_regex=cfg.brand_to_binary_regex,
    )

    for rel in iter_files(root, cfg.scope_include, cfg.scope_exclude):
        path = root / rel
        try:
            content = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        guid_lines = _guid_window_lines(content) if path.suffix in (".cpp", ".h") else set()
        for line_no, line in enumerate(content.splitlines(), start=1):
            for m in find_matches(line, cfg.replacements):
                cat = classify(
                    rel_path=rel,
                    line_no=line_no,
                    line=line,
                    match=m.match,
                    in_guid_window=line_no in guid_lines,
                    ctx=ctx,
                    was_excluded=False,
                )
                stats[cat.value] += 1
                if cat in (Category.NAMESPACE_KEEP, Category.HISTORICAL_KEEP):
                    continue
                after = line[:m.col] + m.replacement + line[m.col + len(m.match):]
                edits.append(Edit(
                    category=cat.value,
                    file=str(rel).replace("\\", "/"),
                    line=line_no,
                    col=m.col,
                    before=line,
                    after=after,
                    match=m.match,
                    replacement=m.replacement,
                ))
    edits.sort(key=lambda e: (e.file, e.line, e.col))
    return edits, renames, stats


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--root", default=".", help="Repo root (default: cwd)")
    p.add_argument("--rules", default="tools/rebrand/rules.toml")
    p.add_argument("--out", default="tools/rebrand/out")
    args = p.parse_args()

    root = Path(args.root).resolve()
    cfg = load_rules(Path(args.rules))
    edits, renames, stats = scan(root, cfg)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    plan = {
        "schema_version": 1,
        "scanned_at": datetime.now(timezone.utc).isoformat(),
        "old_brand": cfg.old_brand,
        "new_brand": cfg.new_brand,
        "guids": {
            "old_clsid_text_service": cfg.old_clsid,
            "new_clsid_text_service": cfg.new_clsid,
            "old_profile_guid": cfg.old_profile_guid,
            "new_profile_guid": cfg.new_profile_guid,
        },
        "edits": [asdict(e) for e in edits],
        "renames": renames,
        "stats": dict(stats),
    }
    (out_dir / "rebrand_plan.json").write_text(
        json.dumps(plan, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    from tools.rebrand.lib.reporter import render_report
    (out_dir / "REBRAND_REPORT.md").write_text(render_report(plan), encoding="utf-8")

    unclassified = stats.get(Category.UNCLASSIFIED.value, 0)
    if unclassified > 0:
        print(f"FAIL: {unclassified} UNCLASSIFIED hits — see plan.json", file=sys.stderr)
        sys.exit(1)
    print(f"OK: {sum(stats.values())} hits, "
          f"{len(edits)} edits + {len(renames)} renames staged")


if __name__ == "__main__":
    main()
