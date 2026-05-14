# rebrand — NexusKey → VKey

Two-phase tool that rebrands the project across docs/binaries/runtime IDs.

## Usage

1. Fill `rules.toml` — generate new GUIDs (`uuidgen`), paste old GUIDs from
   `src/tsf/Globals.cpp`. Replace `TBD-uuidgen` placeholders.
2. Scan: `python3 tools/rebrand/scan.py`
   Outputs: `tools/rebrand/out/REBRAND_REPORT.md` + `out/rebrand_plan.json`.
   Exit non-zero if any UNCLASSIFIED hit — fix `rules.toml` and rerun.
3. Review `REBRAND_REPORT.md` — verify samples per category.
4. Apply (per category, one commit each):
   `python3 tools/rebrand/apply.py --category BRAND_STRING`
   `python3 tools/rebrand/apply.py --category RUNTIME_IPC`
   `python3 tools/rebrand/apply.py --category PERSISTENT_ID`
   `python3 tools/rebrand/apply.py --category BINARY_NAME_AND_FILENAME`
   Or all at once: `python3 tools/rebrand/apply.py --category ALL`.
5. Each apply runs Linux smoke test between stage and commit. Use
   `--skip-smoke` to bypass (you accept verification yourself).

## Tests

`python3 -m unittest discover tools/rebrand/tests`
