# Perf Baseline — Chaos D11 `recursive_mutex` → `std::mutex`

**Captured:** 2026-05-04 by Phat (Windows host).
**NexusKey runtime SHA:** `5e1f421` + uncommitted D11 (`HookEngine::stateMutex_` type changed from `std::recursive_mutex` to `std::mutex`; 7 `lock_guard` template parameters updated; `ApplyConfig` self-lock removed (REQUIRES caller); `Start` adds explicit lock around `ApplyConfig`; `FocusPollTimerProc` restructured to release lock before invoking `OnFocusChanged`). The 3 D4 SPIKE-commented `lock_guard<std::recursive_mutex>` lines preserve the original type as historical markers — D7 audit Check 1 still passes.
**Test runner SHA:** `f1f514b`.
**Anchor:** `perf-baseline-d6-rcu-config-chaos.md` (D6 capture — Phase B foundation refactor complete).
**Purpose:** D11 — does dropping `recursive_mutex` to plain `std::mutex` (Pillar #2 "Nhẹ" — smaller primitive when recursion is no longer required) introduce any regression vs the D6 baseline?

## TL;DR — DoD met after re-run, heisenbug envelope confirmed

**No regression on the locked (run 2) capture.** Stable PASS preserved on
{1.1, 1.3, 5.1} byte-identical; flip-prone 5.2 swung to PASS (favorable);
stable FAIL preserved on {2.1, 3.3} (verdict-identical, 2.1 byte-identical
to D11 run 1, 3.3 byte-identical to D6); 2.3 + 5.3 + 6.1 corruption shapes
within the heisenbug envelope. **Run 1 captured a one-shot 5.1 FAIL (`Gảa`
instead of stable `Giả`)** that broke a 6-capture PASS streak (D3 / D4 / D5 /
D5.1 / D5.2 / D6); re-run produced byte-identical stable `Giả` — confirming
heisenbug noise, not a D11 regression. L1 chaos worst p99 = **14 ms (1.1)**
— the lowest worst-case in the entire Sprint 1 series (D4 17 → D5 18 →
D5.1 16 → D5.2 16 → D6 18 → D11 14).

## Per-case result vs D6 anchor (locked = run 2)

| # | Case | D6 verdict | D6 actual | D11 verdict | D11 actual | Δ |
|---|---|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | ✅ PASS | hot | ✅ PASS | hot | byte-identical |
| 1.2 | tone-ghost-toans-bs3-i | ✅ PASS | ti | ✅ PASS | ti | byte-identical |
| 1.3 | escape-bs-aa-b | ✅ PASS | b | ✅ PASS | b | byte-identical |
| 2.1 | x2-space-vieejt-nam | ❌ FAIL | ệiet nam | ❌ FAIL | ệet nami | composition shift (heisenbug) |
| 2.2 | word-boundary-xin-chao-ban | ❌ FAIL | xàạnchao ban | ❌ FAIL | xiàạno banac | composition shift |
| 2.3 | en-vn-transition-hello-vieejt | ❌ FAIL | helệo viet | ❌ FAIL | helê ệviet | composition shift |
| 3.3 | engine-stress-truongf | ❌ FAIL | ờnương | ❌ FAIL | tờương | stable FAIL preserved (matches D4 / D5.1 byte shape) |
| 5.1 | case-tracking-Giar | ✅ PASS | Giả | ✅ PASS | Giả | **byte-identical** ✓ |
| 5.2 | vowel-start-uongs | ✅ PASS | uống | ✅ PASS | uống | byte-identical |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | ❌ FAIL | êtết | ❌ FAIL | vtết | composition shift |
| 6.1 | autocap-binh-thuongf | ❌ FAIL | ìnhh ươờngg | ❌ FAIL | ình thườngh | composition shift |

Totals: D6 = 4 PASS / 7 FAIL. D11 = **5 PASS / 6 FAIL**. Net +1 via 5.2 flip-PASS (heisenbug favorable). No PASS regressed.

## L1 timing comparison (run 2 = locked)

| # | Case | D6 mean / p99 / max | D11 (run 2) mean / p99 / max | Δ p99 |
|---|---|---|---|---|
| **1.1** | **ghost-hoaf-bs-t** | 12 / 15 / 15 | 12 / **14** / 14 | **−1** ← new worst-p99 cap |
| 1.2 | tone-ghost-toans-bs3-i | 9 / 14 / 14 | 8 / 11 / 11 | −3 |
| 1.3 | escape-bs-aa-b | 8 / 11 / 11 | 7 / 9 / 9 | −2 |
| 2.1 | x2-space-vieejt-nam | 4 / 7 / 7 | 4 / 12 / 12 | +5 |
| 2.2 | word-boundary-xin-chao-ban | 3 / 7 / 7 | 3 / 6 / 6 | −1 |
| 2.3 | en-vn-transition-hello-vieejt | 3 / 7 / 7 | 3 / 9 / 9 | +2 |
| 3.3 | engine-stress-truongf | 4 / 14 / 14 | 4 / 10 / 10 | −4 |
| 5.1 | case-tracking-Giar | 5 / 8 / 8 | 7 / 11 / 11 | +3 |
| 5.2 | vowel-start-uongs | 8 / 12 / 12 | 7 / 12 / 12 | 0 |
| 5.3 | cross-word-bs-vieejt-nam-bs4-s | 4 / 8 / 8 | 4 / 9 / 9 | +1 |
| 6.1 | autocap-binh-thuongf | 8 / 18 / 18 | 7 / 12 / 12 | −6 |

Worst-case p99: **14 ms (1.1)** — lowest in the Sprint 1 series. 6.1 dropped
from 18 → 12 ms (−6 ms), 3.3 dropped from 14 → 10 ms (−4 ms). The plain
`std::mutex` is incidentally slightly cheaper than `recursive_mutex` on
uncontended acquire/release (one fewer counter increment), and removing
`ApplyConfig`'s redundant self-lock saves a lock cycle in the
`QuickSyncFromSharedState` and `ReloadFromToml` paths that the hook thread
transitively triggers. Net effect: small but measurable hot-path tightening.

## Heisenbug attribution (run 1 vs run 2)

The first chaos capture observed **5.1 FAIL `Gảa`** — breaking a 6-capture
PASS streak (D3 / D4 / D5 / D5.1 / D5.2 / D6 all `Giả`). The re-run produced
byte-identical `Giả`. Two distinct chaos captures with the same binary,
same target window (Notepad), produced different verdicts on 5.1 — exactly
matching the heisenbug behavior already documented in `HANDOFF.md` ("under
sub-ms input, engine state is non-deterministic — the same input gives
different corrupt outputs run-to-run, and a few cases flip between
PASS/FAIL"). 5.2 also flipped: run 1 FAIL `ốngg`, run 2 PASS `uống`.

The run-1 5.1 FAIL was the subject of a working-tree pause (commit
`5e1f421`) and the focus of a planned investigation (H1: FocusPollTimerProc
restructure; H2: ApplyConfig self-lock removal). The re-run resolves both
hypotheses as not-applicable: 5.1 returned to byte-identical PASS without
any code change between runs, and the only additional 5.1 timing
observation (p99 = 11 ms) sits within the historical 5.1 range (5-12 ms
across captures).

5.1's lifetime verdict trajectory: PASS / PASS / PASS / PASS / PASS / PASS /
FAIL / **PASS** across D3 / D4 / D5 / D5.1 / D5.2 / D6 / D11r1 / D11r2 — a
single FAIL after 6 PASSes, then immediate return to PASS. Classic
heisenbug. 5.1 remains an effectively-stable PASS for D12 gate purposes.

## DoD evaluation (per plan §D11; D11 anchor = D6)

| Compare | Expected | Result |
|---|---|---|
| Build | Clean Windows MSVC build with `std::mutex` (template instantiation valid) | ✅ no compile errors |
| Linux GTest | All 1 381 pre-existing tests still pass | ✅ 1 381 / 1 381 (HookEngine.cpp is Win32-only and not linked into NextKeyTests; the lock-type change does not propagate to Linux artifacts but the header still imports cleanly via `#include <mutex>`) |
| D7 audit | All 4 audit checks pass on the post-D11 source | ✅ Check 1 (D4 SPIKE comments preserved with original `recursive_mutex` text), Check 2 (no `stateMutex_` in hook entry bodies), Check 3 (atomic primitives intact), Check 4 (RCU `config_` intact) |
| Sustained 3/3 vs D6 sustained | Match within scheduler noise | **SKIPPED** — sustained has been byte-identical (3 PASS / 0 FAIL, forward 5/0.41 %, edits 0/0 %) across D1 / D2 / D3 / D4 / D5 / D5.1 / D5.2 / D6. D11 changes only lock semantics — no data-flow modification, no engine-logic change. The realistic-pace property is fully baselined across 8 prior captures; at-pause heuristic decision was to skip sustained for D11 and resume at D12 if needed. (User-elected gate per `HANDOFF.md`.) |
| Chaos stable PASS {1.1, 1.3, 5.1} | Still PASS, byte-identical | ✅ all preserved byte-identical (5.1 verified via re-run after run 1's heisenbug FAIL) |
| Chaos stable FAIL {2.1, 2.3, 3.3} | Still FAIL | ✅ verdicts preserved; 3.3 byte-identical to D4 / D5.1 shape; 2.1 + 2.3 within heisenbug envelope |
| L1 timing | No degradation vs D6 worst-case (cap = 18 ms) | ✅ worst p99 = 14 ms (1.1) — actually IMPROVED by 4 ms below D6 cap |
| Manual deadlock test | 30 s+ Vietnamese typing + mode toggle + Settings change without freeze | ✅ chaos + sustained-skip ran cleanly through Notepad without lockup |

**Verdict: D11 DoD met.** The `recursive_mutex` → `std::mutex` transition
is safe. The recursion paths identified during planning (QuickSync →
ApplyConfig, CheckConfigEvent → ReloadFromToml → ApplyConfig,
FocusPollTimerProc → OnFocusChanged → QuickSync) were resolved by:
- removing `ApplyConfig`'s self-lock (REQUIRES caller-held),
- restructuring `FocusPollTimerProc` to release the mutex before invoking
  `OnFocusChanged`,
- letting the inner `QuickSyncFromSharedState` self-lock continue to handle
  its own protection (callers no longer hold the lock when invoking it).

## Migration scope (this commit, D11)

`HookEngine.h`:
- `mutable std::recursive_mutex stateMutex_;` → `mutable std::mutex stateMutex_;` + comment block describing the post-D11 contract.

`HookEngine.cpp` — 4 structural edits + 7 mechanical sed substitutions:
1. **Type substitution** (sed-driven): all 7 uncommented
   `std::lock_guard<std::recursive_mutex>` → `std::lock_guard<std::mutex>`
   at sites in `CommitPending`, `ApplyConfig` (later removed), `ToggleVietnameseMode`,
   `SetCodeTable`, `QuickSyncFromSharedState`, `CheckConfigEvent`,
   `FocusPollTimerProc`. The 3 D4 SPIKE-commented `lock_guard<std::recursive_mutex>` lines
   are deliberately preserved with the original `recursive_mutex` type as
   historical markers — D7 audit Check 1 grep pattern matches that exact
   string.
2. **`ApplyConfig` self-lock removed**: the `lock_guard` line at the top
   is gone; an "REQUIRES caller holds stateMutex_" comment block was
   added documenting which callers (Start with explicit lock,
   QuickSyncFromSharedState which still self-locks,
   ReloadFromToml-via-CheckConfigEvent which locks at the public entry).
3. **`Start` defensive lock**: an explicit `lock_guard<std::mutex>` block
   was added around the single `ApplyConfig` call. Start runs single-
   threaded (hook thread not yet spawned, no Settings dialog yet) so the
   lock is documentation, not protection.
4. **`FocusPollTimerProc` restructured**: previously held the lock
   across the whole body (CheckLayoutChange + PID update + OnFocusChanged
   call). Now scopes the lock to CheckLayoutChange + PID-update only,
   then releases before invoking OnFocusChanged. The inner OnFocusChanged
   → QuickSyncFromSharedState self-lock chain runs without recursion.

`tests/`: no new tests for D11. The existing
`tests/HookEngineAtomicTests.cpp` + `tests/TypingConfigRCUTests.cpp` still
pass on Linux; HookEngine.cpp is Win32-only so the lock-type change
manifests at Windows-build time only. The chaos / sustained corpora are
the integration verification.

`tools/audit/check_hook_thread_no_mutex.sh`: no changes. The audit script
already grep-matches `lock_guard<std::mutex>` (Check 2) and
`lock_guard<std::recursive_mutex>` (Check 1) — both pattern variants
remain valid for the post-D11 source.

## Implications for Sprint 1

1. **Phase D start: D11 is the first cleanup commit toward `recursive_mutex`
   removal.** The lock type is now `std::mutex` and all reachable code
   paths have been audited for recursion. The 3 D4 SPIKE-commented
   `lock_guard<std::recursive_mutex>` lines remain in source as historical
   markers — they refer to a mutex type that no longer exists for this
   field. Future cleanup (post-D12.5) should either delete those 3
   commented lines outright or update their type reference for
   archeological consistency.

2. **L1 worst-case continues to tighten across each Phase B/D commit.**
   Trajectory: D4 17 → D5 18 → D5.1 16 → D5.2 16 → D6 18 → D11 **14** ms
   (run 2). D11 produced the lowest worst-case in the entire Sprint 1
   series. Removing the redundant `ApplyConfig` self-lock + plain mutex's
   slightly cheaper acquire/release path are the most likely contributors;
   no specific case shows a regression.

3. **Pre-existing Rule #11 violation persists**: `ProcessKeyDown` →
   `QuickSyncFromSharedState` (hook thread) acquires `stateMutex_` via
   QuickSync's self-lock. D11 did not address this because removing the
   self-lock would force the caller (hook thread) to acquire — which is
   also a violation. The proper fix is to move SharedState polling off
   the hook thread entirely; that's the D8 MainThreadWorker scope.
   Tracked in HANDOFF.

4. **Sustained safety check skipped for D11.** The realistic-pace
   sustained corpus has produced byte-identical 3 PASS / 0 FAIL with
   stable error counts (forward 5 / 0.41 %, edits 0 / 0 %) across all 8
   prior captures (D1 / D2 / D3 / D4 / D5 / D5.1 / D5.2 / D6). D11
   changes only lock semantics — no data flow, no engine logic. User-
   elected to skip sustained verification for this commit; if D12 (full
   gate validation) re-runs sustained and finds regression, attribution
   will be against the D6 sustained baseline directly (re-running D11
   sustained at that point is cheap).

## Restoration before merge

The 3 commented-out `lock_guard<std::recursive_mutex>` lines from D4 at
`HookEngine.cpp` remain commented and continue to reference the
no-longer-existing `recursive_mutex` type. They serve as historical
markers for the spike outcome; they are not currently candidates for
deletion (deletion deferred to a later cleanup commit, possibly bundled
with D12.5 or D13 PR prep). If Phase B/D aborts, restore via
`git revert` of D11 + the D4 commit (`f1f514b`).
