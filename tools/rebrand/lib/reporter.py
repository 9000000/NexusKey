"""Render REBRAND_REPORT.md from a plan dict."""
from __future__ import annotations
from collections import defaultdict


def render_report(plan: dict) -> str:
    stats = plan.get("stats", {})
    edits = plan.get("edits", [])
    renames = plan.get("renames", [])
    guids = plan.get("guids", {})

    files_per_cat = defaultdict(set)
    for e in edits:
        files_per_cat[e["category"]].add(e["file"])

    out = []
    out.append(f"# Rebrand Scan Report — {plan.get('scanned_at', '')}")
    out.append("")
    out.append(f"Old brand: `{plan.get('old_brand')}` → New brand: `{plan.get('new_brand')}`")
    out.append("")
    out.append("## Summary")
    out.append("")
    out.append(f"- Total edits queued: {len(edits)}")
    out.append(f"- File renames: {len(renames)}")
    out.append("")
    out.append("| Category | Hits | Files |")
    out.append("|---|---:|---:|")
    for cat in ("BRAND_STRING", "BINARY_NAME", "RUNTIME_IPC",
                "PERSISTENT_ID", "FILENAME", "NAMESPACE_KEEP",
                "HISTORICAL_KEEP", "UNCLASSIFIED"):
        n = stats.get(cat, 0)
        f = len(files_per_cat.get(cat, set()))
        out.append(f"| {cat} | {n} | {f} |")
    out.append("")

    out.append("## Validation gates")
    out.append("")
    unc = stats.get("UNCLASSIFIED", 0)
    guid_ok = all(
        guids.get(k, "TBD-uuidgen") not in ("TBD-uuidgen", "", None)
        for k in ("new_clsid_text_service", "new_profile_guid")
    )
    out.append(f"- [{'x' if unc == 0 else ' '}] UNCLASSIFIED == 0"
               f"{' ← BLOCKING' if unc else ''}")
    out.append(f"- [{'x' if guid_ok else ' '}] All persistent_id.guids filled"
               f"{' ← BLOCKING (TBD-uuidgen)' if not guid_ok else ''}")
    out.append("")

    out.append("## Renames")
    out.append("")
    for r in renames:
        out.append(f"- `{r['from']}` → `{r['to']}`")
    out.append("")

    out.append("## Sample edits per category (first 5)")
    out.append("")
    by_cat = defaultdict(list)
    for e in edits:
        by_cat[e["category"]].append(e)
    for cat in ("BRAND_STRING", "BINARY_NAME", "RUNTIME_IPC",
                "PERSISTENT_ID", "FILENAME"):
        if cat not in by_cat:
            continue
        out.append(f"### {cat}")
        out.append("")
        for e in by_cat[cat][:5]:
            out.append(f"- `{e['file']}:{e['line']}`  `{e['match']}` → `{e['replacement']}`")
        out.append("")

    return "\n".join(out) + "\n"
