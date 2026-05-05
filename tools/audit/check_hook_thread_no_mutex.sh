#!/usr/bin/env bash
#
# Sprint 1 D7 — Phase B compliance gate.
#
# Verifies three guarantees at the source level:
#   1. The 3 hook-thread `lock_guard<recursive_mutex>` lines from D4 SPIKE
#      are still commented (regression marker — if any is uncommented,
#      Phase B's correctness contract is broken).
#   2. No uncommented `stateMutex_` reference inside any hook-callback entry
#      function body (`LowLevelKeyboardProc`, `WinEventProc`,
#      `LowLevelMouseProc`, `RawInputWndProc`).
#   3. All migrated atomic fields (D5 + D5.1 + D5.2 + D6) use `.load()` /
#      `.store()` — no plain assignment or read of these fields. The atomic
#      RCU `config_` field also obeys this rule.
#
# Run from repo root:
#   bash tools/audit/check_hook_thread_no_mutex.sh
#
# Exit code 0 = pass, non-zero = fail.
#
# Notes:
# * This is a grep-based heuristic, not a full call-graph analysis. It catches
#   the regression patterns that matter (someone adds a `stateMutex_` lock to
#   a hook callback, or reverts an atomic field to plain assignment), at the
#   cost of accepting some over-approximation in comments / format strings.
# * If a check produces a false positive, prefer tightening this script over
#   weakening the source-level invariant.
#
# Reference: docs/plans/sprint-1-single-owner-refactor.md §B D7.

set -e

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo_root"

CPP="src/app/system/HookEngine.cpp"
errors=0

if [ ! -f "$CPP" ]; then
    echo "ERROR: $CPP not found (run from repo root)"
    exit 2
fi

echo "=== Sprint 1 D7 audit: $(basename "$CPP") ==="
echo

# ────────────────────────────────────────────────────────────────────────
# Check 1: D4 SPIKE comment integrity
# ────────────────────────────────────────────────────────────────────────
# The 3 hook-thread lock_guard<recursive_mutex> lines at HookEngine.cpp
# (originally lines 647 / 677 / 705 — line numbers drift with edits) are
# expected to remain commented. If anyone uncomments them, Phase B's
# atomic-only contract on the hook hot path is silently broken.

echo "Check 1: D4 SPIKE comment integrity (≥3 commented lock_guard lines)"
spike_commented=$(grep -cE "^\s*//\s*std::lock_guard<std::recursive_mutex>" "$CPP")
if [ "$spike_commented" -lt 3 ]; then
    echo "  FAIL: expected ≥3 commented lock_guard<recursive_mutex> lines, found $spike_commented"
    grep -nE "^\s*//\s*std::lock_guard<std::recursive_mutex>" "$CPP" || true
    errors=$((errors + 1))
else
    echo "  OK: $spike_commented commented lock_guard line(s) preserved"
fi

# ────────────────────────────────────────────────────────────────────────
# Check 2: No uncommented stateMutex_ in hook callback entry function bodies
# ────────────────────────────────────────────────────────────────────────
# Plan §B D7 scope: LL keyboard / LL mouse / WinEvent — the callbacks that
# run on hookThread_ (LL keyboard / mouse / RawInput WM_INPUT). WinEventProc
# is included for plan continuity even though current architecture installs
# it from main thread (WINEVENT_OUTOFCONTEXT runs on installer thread); the
# audit catches the case where a future refactor moves it back onto the
# hook thread without removing the lock.
#
# Sprint 1 D10 retired `FocusPollTimerProc` (the 200 ms `SetTimer` poll for
# CJK layout + foreground PID). Its body now lives in `OnTickPoll`, driven
# from `MainThreadWorker`'s tick branch. Worker-thread access to stateMutex_
# is fine — Rule #11 only forbids it on the LL hook thread.

HOOK_ENTRIES=(
    "LowLevelKeyboardProc"
    "WinEventProc"
    "LowLevelMouseProc"
    "RawInputWndProc"
)

echo
echo "Check 2: stateMutex_ not reachable from hook callback entries"
for fn in "${HOOK_ENTRIES[@]}"; do
    # Extract function body using awk: from the line containing "::fn(" through
    # the matching closing brace at column 0. This is approximate (assumes
    # consistent indentation — closing brace at column 0 — which the codebase
    # follows). Grep -v filters comment lines.
    body=$(awk -v fn="$fn" '
        $0 ~ ("HookEngine::" fn "[[:space:]]*\\(") { in_fn = 1 }
        in_fn { print }
        in_fn && /^\}/ { in_fn = 0 }
    ' "$CPP")
    if [ -z "$body" ]; then
        # Function not found — could be a static helper or moved. Skip with note.
        echo "  SKIP: $fn (not found in $CPP)"
        continue
    fi
    # Count uncommented stateMutex_ references in the body.
    refs=$(echo "$body" | grep -vE "^\s*//" | grep -c "stateMutex_" || true)
    if [ "$refs" -gt 0 ]; then
        echo "  FAIL: $fn body contains $refs uncommented stateMutex_ reference(s)"
        echo "$body" | grep -vE "^\s*//" | grep -nE "stateMutex_" || true
        errors=$((errors + 1))
    else
        echo "  OK: $fn — 0 uncommented stateMutex_ references"
    fi
done

# ────────────────────────────────────────────────────────────────────────
# Check 3: Migrated atomic fields use .load() / .store() exclusively
# ────────────────────────────────────────────────────────────────────────
# Fields migrated across D5 / D5.1 / D5.2 / D6. Any plain access (assignment
# or read) on these fields outside of an explicit `.load(` / `.store(` call
# violates Rule #11.3 and is flagged.
#
# False-positive sources we explicitly tolerate:
#   * Lines starting with `//` (full-line C++ comments)
#   * Lines starting with `///` (Doxygen-style)
#   * Lines containing only field name in a printf format string would also
#     match — we accept that and recommend tightening this filter only if
#     it triggers in practice.

ATOMIC_BOOLS="vietnameseMode_|isTsfApp_|isExcludedApp_|isConsoleApp_|isElectronApp_|skipEmptyChar_|needBaitChar_|useClipboardPaste_|useEditMsgPath_|isOutlookApp_|macroEnabled_|macroInEnglish_|autoCaps_|autoCapsMacro_|tempOffMacroByEsc_|tempOffByAlt_"
ATOMIC_DWORD="excludedPid_"
ATOMIC_ENUM="currentMethod_"
ATOMIC_RCU="config_"

echo
echo "Check 3: atomic fields use .load()/.store() (no plain assignment or read)"

# Helper: count plain accesses for a field pattern.
# A "plain access" is any reference NOT followed by `.load(` or `.store(`,
# excluding C++ comments and the field's own declaration line.
audit_field_group() {
    local label="$1"
    local pattern="$2"
    local extra_exclude="$3"  # optional extra grep -vE pattern

    # Build the exclusion regex. Lines we don't count as violations:
    #   - .load( or .store( (the migrated atomic call sites)
    #   - C++ // comment lines (whole-line comments)
    #   - field declaration ("std::atomic<...>")
    #   - extra exclude (e.g. configEvent_ for the config_ check)
    local exclude="\\.(load|store)\\s*\\(|^\\s*[0-9]+:\\s*//|std::atomic"
    if [ -n "$extra_exclude" ]; then
        exclude="$exclude|$extra_exclude"
    fi

    local matches
    matches=$(grep -nE "\b($pattern)\b" "$CPP" | grep -vE "$exclude" || true)
    local count
    count=$(echo -n "$matches" | grep -c '^' || true)
    if [ "$count" -gt 0 ]; then
        echo "  FAIL [$label]: $count plain access(es)"
        echo "$matches" | head -10 | sed 's/^/    /'
        errors=$((errors + 1))
    else
        echo "  OK   [$label]"
    fi
}

audit_field_group "atomic<bool> primitives"  "$ATOMIC_BOOLS"  ""
audit_field_group "atomic<DWORD>"            "$ATOMIC_DWORD"  ""
audit_field_group "atomic<InputMethod>"      "$ATOMIC_ENUM"   ""
# config_ check excludes configEvent_ / configReloadCallback_ / config_t typedefs
audit_field_group "atomic<shared_ptr<TypingConfig>>" "$ATOMIC_RCU" "configEvent_|configReloadCallback_|TypingConfig"

# ────────────────────────────────────────────────────────────────────────
# Result
# ────────────────────────────────────────────────────────────────────────
echo
if [ "$errors" -gt 0 ]; then
    echo "FAIL: $errors check(s) failed — Phase B compliance regressed"
    exit 1
fi
echo "PASS: all D7 audit checks passed — Phase B compliance maintained"
exit 0
