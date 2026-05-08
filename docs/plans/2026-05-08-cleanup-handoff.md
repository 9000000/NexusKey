# Post-v3 Cleanup Handoff — 2026-05-08

Quick handoff for whoever picks up after this session. Token budget hit; stopping clean.

## What landed (5 PRs merged on Main)

| PR | Branch | What |
|---|---|---|
| #154 | `feature/v3-hook-self-healer-watchdog` | v3.0.0 — Hook self-healer + watchdog (opt-in default OFF, tray toggle, single-instance mutex). Smoke 1/2/3 PASS. |
| #155 | `chore/cleanup-post-v3` | Drop dead `IsElectronByMarker` block (37 LOC), unused `Psapi.h`/`psapi.lib` in watchdog, unused `<memory>` in HookSelfHealer, retire stale `T2.1 D4` TODO label. |
| #156 | `chore/outlook-anh-em-revert-and-mouse-race` | Revert `isOutlookApp_` passthrough gate (Outlook AutoCorrect, not us — user-confirmed). Mark Pre-T3 Minor 1 (mouse race) as already-landed via Sprint 3 H3 atomic migration. |
| #157 | `chore/batch1-cleanup` | Multi-monitor centering helper (`NextKey::GetCenteredPos` in `helpers/AppHelpers.h`), 9 dialogs migrated. TSF_LOG → single OutputDebugStringW call. TrackedSendInput consolidation: drop HookEngine member, route 6 callers through `Output::Internal::TrackedSendInput`. |

## Branch open + uncommitted state

**Branch ready to push: `chore/batch2-shouldautocap-extract`** (not pushed yet — let teammate review first).

Two commits:
1. `refactor(tsf): extract ShouldAutoCap rule into Linux-portable header` — `core/AutoCapDecision.h::ComputeShouldAutoCap(buf, len)` + 8 tests in `tests/AutoCapDecisionTest.cpp`. Linux 1555 → 1563 PASS.
2. `docs(todo): mark Batch 2 items as landed/audited` — TODO.md cleanup for ApplyAutoCapsMacro / TryExpandMacro tests / `\n` escape audit / ShouldAutoCap.

**Stash kept** from earlier in session: `stash@{0}: WIP: sustained.toml corpus tweak — outside v3 scope`. Anh decides separately.

## What's NEXT in the cleanup plan

The session's batch plan was 4 batches. Status:

### ✅ Batch 1 — DONE (PR #157)
- Multi-monitor centering helper + 9 dialog migration
- TSF_LOG atomic single call
- TrackedSendInput consolidation

### 🟡 Batch 2 — Mostly DONE
- ✅ ApplyAutoCapsMacro extract → already landed via `Macro::Plan` (was stale TODO)
- ✅ TryExpandMacro match tests → already landed via `MacroCaseTest.cpp` (459 LOC)
- ✅ `\n` escape audit → no action needed (current behavior correct, see TODO.md note)
- ✅ ShouldAutoCap extract → just landed on `chore/batch2-shouldautocap-extract`

**Push + PR + merge `chore/batch2-shouldautocap-extract` to wrap Batch 2.** Suggested command:
```bash
git push -u origin chore/batch2-shouldautocap-extract
gh pr create --base Main --head chore/batch2-shouldautocap-extract \
  --title "refactor(tsf): extract ShouldAutoCap rule + Linux tests" \
  --body "Pure auto-cap decision moved into core/AutoCapDecision.h (8 tests). TODO.md cleanup for already-landed Batch 2 items."
gh pr merge --merge --admin --delete-branch
```

### ⏳ Batch 3 — Pending decisions
- **M2 — `kFoo` vs `UPPER_SNAKE` constants naming convention.** Needs 3-collaborator decision per `docs/TODO.md`. Recommendation: formalize the k-prefix in Rule 9.1 (it's the de facto pattern). Either way, picks one and renames or updates the rule.

### 🔵 Batch 4 — DEFERRED (no driver)
- **Phonotactics Path 1 → IPhonologyRules migration.** TODO label retired in PR #155. Reopen only when a dialectal rule pack is needed.

## Items still open in `docs/TODO.md` after this session

Reading top-down:

| Section | Status notes |
|---|---|
| 🟡 v3 Watchdog Smoke 4 + 5 + 6 | Defer until logout/login or AV scan window. Code shipped. |
| 🔴 Vietnamese-rule consolidation phonology plugin | **STALE** post-T2.1 sprint close. Most of what this entry asked for landed in Main commits `48d25b1`/`59fd807`/`3f86c40`/`1bfffb7`. Recommend rewriting this entry to reflect what's done vs what remains (Path 1 migration only). |
| 🟡 Items deferred from cleanup PR (Pre-T3 Minor 1, M2, M3, typing bug `cafcs`) | Minor 1 + M3 LANDED. M2 still pending decision. `cafcs` typing bug ostensibly fixed by T5 (`211f2e8`) — verify. |
| Typing bug `cafcs → các` | Verify post-T5 fix on Windows. |
| Outlook "Anh em" Fix | RESOLVED via PR #156 revert. TODO entry can be marked landed. |
| Macro Case-Matching follow-ups | All 5 sub-items still open (`TryExpandMacro` syscalls, P3/P4 composition asymmetry, escape gap, release-note). Lower priority. |
| Auto-caps + TSF Apps Feedback | Edge revive bugs ("user said skip"), Windows Search Vietnamese, ShouldAutoCap extract LANDED. |
| Hotkey Refactor deferred | All low-priority polish items. |
| TSF Readonly Context Phase 2/3 | "tạm chưa có ai dùng nhiều, để đó vậy" per user. |
| Earlier review sections | Mostly fixed, residual items low-priority. |

## Branch hygiene

After Batch 2 PR merges, local branches list:

| Branch | Status |
|---|---|
| `Main` | tracking `origin/Main` |
| `feat/save-feat` | NOT merged ("stuck at classic UI") — leave |
| `feature/typing-engine-unification` | merged into Main, safe to delete |
| `pr-151` | merged into Main, safe to delete |
| `sprint-3/fsm-engine` | NOT merged ("Sprint 3 paused, Path G replaces") — leave |

User asked Claude not to auto-delete (per session memory). Suggested cleanup if teammate wants:
```bash
git branch -d feature/typing-engine-unification pr-151
```

## Decisions made in this session (durable)

1. **Watchdog opt-in (default OFF)** — User design choice. Tray toggle is single source of truth. No migration code (branch was unreleased). Codified in `feedback_design_philosophy.md` memory.
2. **Outlook revert** — User confirmed "Anh em" symptom is Outlook AutoCorrect, not the IME path. Removed `isOutlookApp_` passthrough gate; kept `needBaitChar_` for the BS U+202F quirk (orthogonal, real).
3. **Pre-T3 Minor 1 (mouse race)** — Already addressed via Sprint 3 H3 atomic migration (`58d8f88`). Companion `cachedFocusedClass_` tuple race documented as benign in `HookEngine.h:474-479`. No further action.
4. **Option A (kill-twice-stop watchdog) — REVERTED** — 60s threshold never fires (heartbeat timeout 90s > threshold). Tray toggle covers user disable.

## How to resume

Continue on Main. Next obvious task (in priority order):

1. **Push + merge `chore/batch2-shouldautocap-extract`** — already-tested, low risk.
2. **Decide M2 (k-prefix vs UPPER_SNAKE).** Update Rule 9.1 in `docs/CODING_RULES/9-naming-conventions.md` to formalize whichever pattern wins. Either way, ~30 min of work.
3. **Verify cafcs typing bug** post-T5 fix on Windows. If still broken, follow `nexuskey-typing-bugs` skill.
4. **Smoke 4/5/6** for v3 watchdog when next logout/login window opens.
5. **TODO.md tidy-up** — rewrite the "🔴 Vietnamese-rule consolidation" top entry to reflect post-T2.1 reality (most done, Path 1 migration is the only remainder).

Linux GTest baseline: 1563 / 1563 PASS as of session end.
