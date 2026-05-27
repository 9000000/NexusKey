# VKey Refactor Status — Living Inventory

> **Last refresh:** 2026-05-25 (Wave 3 arch-perf branch closed, 4 owners shipped, post-curation TODO)
> **Branch state:** `feat/architecture-review-v3.1` HEAD `b8b083e` — 16 commits ahead of Main since 2026-05-22 (Wave 1+2+3 + dialog Import helper). Awaiting Windows verify + Main merge.
> **Scope:** All architectural / cleanup refactor work. Excludes user-facing features (Sciter UI, TSF Phase 2/3, keymap files) — those track separately in `docs/TODO.md`.
> **Sequencing rule (anh 2026-05-07):** HookEngine refactor backlog BEFORE TypingEngine. Backlog architecturally closed in 2026-05-08 (H1-H7 + T1-T6 + T2.1). Then Sprint 3 post-2026-05-08 file growth (+862 LOC) triggered the 2026-05-22 de-god probe (Section H), which PAUSED after Phase 1. Wave 1-3 (2026-05-23 → 2026-05-25) re-opened the carve via a different lens — concern-based owner extraction rather than method-decomposition — and shipped 4 owners + RCU lock-free reload + atomic state migration.

---

## Codebase vital signs (snapshot 2026-05-25, branch `feat/architecture-review-v3.1`)

| Metric | Value |
|---|---|
| Total production LOC (`src/`) | 51,796 (+13,931 vs 2026-05-07 baseline 37,865 — most growth in Sciter/Classic UI, NOT engine layer) |
| App layer LOC | 33,793 (65%) |
| Engine layer LOC | 13,698 |
| TSF layer LOC | 4,133 |
| **HookEngine.cpp** | **3,483 LOC** (peak 4,634 pre-Wave-1 → −25% via 4 owner extractions) |
| **TypingEngine.cpp** | 2,055 LOC (peak from W7.5 feature-pipeline framework + 5 engine rules) |
| Other top files | `src/app/resources.cpp` 4305 (generated), `ClassicSettingsDialog.cpp` 1627, `SettingsDialog.cpp` 1506, `ConfigManager.cpp` 1222 |
| `src/app/system/` owner headers | 27 files (4 new from Wave 3: HookLifecycle / FocusOwner / OutputDispatcher / CommitState) |
| GTest count | **2039 / 2044 PASS** Linux (5 failing: `ToneMidSmartAccentTest` family — pending feature work, NOT regression from refactor — TelexEngineTest.cpp uncommitted in working tree) |
| Chaos pass rate | 53-54 / 55 PASS (chrome 1.2 + 1.3 documented env false-fails, not VKey regressions) |
| TODO/FIXME density | 10 markers in src/ (very low — post 2026-05-25 `679e8a7` curation) |
| Layering violations | None (engine/config has zero Win32 deps; tests run Linux-only via macro guards) |
| Dependency state | Sciter 6.0.3.17, toml++ 3.4.0, GoogleTest local-tagged |

---

## Section A — Refactor work DONE

### A.1 — Historic (chronological, Main branch)

| Sprint / Plan | Merge | Date | Scope |
|---|---|---|---|
| Phase 0a TestRunner | #114 `56a09d2` | 2026-05-04 | Chaos harness, edit_distance verdict, JUnit/CSV reporters |
| **Sprint 1** Single-Owner Hook | #115 `c34497a` | 2026-05-05 | Atomic flags (D5), RCU `shared_ptr<TypingConfig>` (D6), audit CI (D7), MainThreadWorker (D8-D10), `recursive_mutex` drop (D11), chaos baselines (D12), RichEditD2DPT routing |
| **Sprint 2 T3** IOutputInjector | #120 `61639b9` (+ #121, #123) | 2026-05-05 | D0-D6: Win32SendInput / RichEditEm / SplitDispatch injectors, factory, ChannelTraits cleanup, SettleBudget (D5), HookEngine 3593 → ~3300 LOC |
| FSM codegen tool | #132 `4a52399` | 2026-05-06 | Standalone Python NFA→DFA→Hopcroft tool — salvage on Main, FSM rewrite cancelled |
| EscapeState + QuickConsonantState | `cc3ac21` | 2026-04-11 | TelexEngine field consolidation |
| TypingEngine unification | `e5b21fc` | 2026-04-16 | TelexEngine → TypingEngine; VNI through one engine; `InputMethod::Combined` |
| TSF DLL hybrid update | `c1e9ce2` | 2026-04-22 | SharedState ABI gate + deferred DLL swap |
| **Sprint 3 Path G G-1..G-4** | #134-#138 | 2026-05-07 | Phonotactics class, DI, TypingAction enum, unified ProcessModifier dispatch, customKeyMap engine hook |
| **Post-Path-G cleanup** (H2 + H7 + T1 + T4) | #139 `64fb80f` | 2026-05-07 | Delete `CheckConfigEvent`, strip D4/T3 comments, `SpellChecker → PhonotacticsValidator`, verify Hot-path Fix 3. H4 wontfix (logging adapter, not dup). |
| **H3** atomic `cachedFocusedHwnd_` | #140 `6ebc603` | 2026-05-07 | Closes Pre-T3 Minor 1: `HWND` → `std::atomic<HWND>` relaxed. Tuple race documented benign. |
| **H5** Macro extract → pure functions | #141 `a141548` | 2026-05-07 | `TryExpandMacro` 217 → 50 LOC orchestrator + 3 free helpers in `core/MacroCase.{h,cpp}`. +38 gtests. Chaos 55/55. |
| **H1a** `HandleCommitUndo` extract | #143 `e820876` | 2026-05-07 | Step 2d FSM (187 LOC) → private method + `KeyOutcome` enum. ProcessKeyDown 561 → 380. |
| **H1b** `RunTopGuards` extract | #144 `3c06499` | 2026-05-07 | Steps 0/0b/1/1b/1c (54 LOC) → private method. ProcessKeyDown 380 → 327. |
| **H1c** `HandlePreDispatch` + `DispatchKeyAction` | #145 `659b910` | 2026-05-07 | Steps 3/3a-3d (107 LOC) + 4b-10 (158 LOC). ProcessKeyDown 327 → **79 LOC** orchestrator (-86% cumulative). |
| **T5** Tone replacement recovery | #146 `1ade8dc` | 2026-05-07 | `IsToneStopCodaMismatch()` + 3 gate-relaxations. +9 gtests. |
| **T2** Phonotactics onset agreement | #147 `d723e03` | 2026-05-08 | c/k, g/gh, ng/ngh enforcement (qu exempt). +16 gtests. |
| **T3** Phonotactics N1/N2/N3 groups | #149 `e908621` | 2026-05-08 | `ClassifyCoda` + `ClassifyVowelGroup` + `IsCodaCompatibleWithVowelGroup`. +11 gtests. |
| **T2.1** Vietnamese-rule consolidation sprint (D1-D4) | #150-#153 | 2026-05-08 | `IsFrontBaseVowel` lift, `kVCPairRules` lift (retire N-group on Path 2), `IPhonologyRules` plugin contract, `PhonologyRulePackId` enum + factory hook |
| **H6a** Watchdog | #154 `d37d0e1` | 2026-05-09 | `HeartbeatPublisher` + `VKeyWatchdog.exe`. HookSelfHealer reverted 2026-05-17 (RIDEV_INPUTSINK bypass broke Classic dialog typing). |
| **H6b** SPSC ring | — | 2026-05-09 | Closed wontfix (`docs/plans/2026-05-09-hook-engine-ring-buffer-kill.md`). Sync model 14-17ms p99 has 280ms headroom; async would regress UX baseline. |

### A.2 — Arch-perf branch (`feat/architecture-review-v3.1`, 2026-05-22 → 2026-05-25)

Branch context: Sprint 3 post-2026-05-08 file growth (+862 LOC, mostly ConfigApply Phase 3a-3f RCU + 5 commit-undo bug fixes) triggered selective probe + de-god analysis. After Phase 1 PAUSE verdict (Section H), pivoted to **concern-based owner extraction** instead of method-decomposition. Result: 4 owners shipped + lock-free hotkey/config + worker-thread doctrine fixes.

| PR | Commit | Date | Scope |
|---|---|---|---|
| **Phase 0** Atomic enum migration | `76511b1` + `8acef2e` | 2026-05-22 | `currentCodeTable_` / `globalCodeTable_` / `globalInputMethod_` → atomic. Formal UB eliminated on cross-thread enum reads. |
| **Phase 1** ConfigSnapshotBuilder lift | `84a2b90` | 2026-05-22 | `RebuildSnapshotFromToml` body → `ConfigSnapshotBuilder::BuildFromToml` pure free fn. PAUSE-verdict per Section H gate. |
| **Wave 1** Hotkey RCU + cross-thread dispatch | `cb9db69` + `e464b29` | 2026-05-23 | `HotkeyManager` slots split SlotBinding (RCU'd) / SlotState (LL-thread). LL callback lock-free + PostThreadMessage to hook thread. +5 review fixes (atomic mem-order, nodiscard, assert). |
| **Wave 2** Config RCU + lock-free reload | `8f45283` + `704eb49` | 2026-05-23 | Drop 5 cached bool fields from HookEngine, hot path reads via `config_.load()`. Atomize 5 SharedState cache scalars. TomlFileCache (mtime + RCU). CAS on `lastEpoch_` to prevent lost-update under concurrent slow-path. |
| **Wave 3 PR 3.1** HookLifecycle | `21c6afc` + `330781e` + `6fed143` | 2026-05-23/24 | Thread + LL hooks + mailbox + handshake CV → dedicated owner. HookEngine 4634 → 4432. Hotfixes: u8path C++20 deprecation, missed `mailbox_` rewrite, Wave 2 cfg shadow. |
| **Wave 3 PR 3.2** FocusOwner | `cb2bcb7` | 2026-05-24 | 14 focus-state fields + WinEvent hooks + ClassifyFocusedWindow + IsWebView2App + IsTrayOrTaskbarWindow + AppProfile cache → dedicated owner. HookEngine 4432 → 3868. ConfigContext seam keeps owner decoupled from HookLifecycle runtime state. |
| **Wave 3 PR 3.3** OutputDispatcher | `a109994` + `ef1fb5d` | 2026-05-24 | `injector_` atomic + dispatch helpers → dedicated owner. Hot path through `dispatcher_.GetInjector()`. Non-const FocusOwner& hotfix for RefreshFocusCache callable. |
| **Wave 3 PR 3.4** CommitState | `5323850` | 2026-05-24 | `commitStack_`, `commitUndoState_`, `pendingTriggerCount_`, `leadingTriggersForCurrentWord_`, `commitReadyTime_` → `CommitState` owner. HookEngine accessors via `commitState_.IsReady()` / `SetReady()` etc. |
| **Wave 3 PR 3.5** Cleanup — header diet | `6e8bf67` | 2026-05-24 | Drop 4 includes (`<condition_variable>`, `<thread>`, `<unordered_map>`, `<unordered_set>`) + 2 dead aliases (`using CommitUndoState`, `kMaxCommitStack`). Byte-identical body. |
| **PROJECT_MAP sync** | `d9afb0b` | 2026-05-24 | Doc-only — PROJECT_MAP.md aligned to Wave 3 post-merge reality |
| **Wave 3 PR 3.6** Worker-thread doctrine | `ec97b2a` + `ea1aefb` | 2026-05-24 | 3 race fixes via worker-thread invariant enforcement + std::function race + stale comment self-review |
| **Wave 3 PR 3.7** Hotkey tag-dispatch | `e7c3f32` | 2026-05-24 | Tag-based dispatch contract enforcement |
| **Wave 3 PR 3.8** Toggle hotkey live bus | `cb953be` + `2d18027` | 2026-05-24 | Toggle hotkey reads from SharedState live bus (not stale config snapshot) + self-review |
| **TODO curation** (post-Wave 3) | `679e8a7` | 2026-05-25 | DNA philosophy gate applied: closed 2 resolved + dropped 7 fail-philosophy entries + moved 1 to docs/plans. TODO 1224 → 1006 lines. |
| **Dialog cleanup** | `b8b083e` | 2026-05-25 | Extract `ParseConfigLines` helper for 8 dialog Import sites |

---

## Section B — Refactor work CANCELLED / DEFERRED

### B-1 Sprint 3 FSM rewrite (CANCELLED, local branch lingers)

| Field | Value |
|---|---|
| Branch | `sprint-3/fsm-engine` (local only, never pushed) |
| Last commit | `b7d0202` (handoff note) |
| Status | Brainstorm 2026-05-07 confirmed ≥19 MB DFA table → cancelled. Path G replaces. |
| **Action** | Delete local branch `git branch -D sprint-3/fsm-engine` to avoid future confusion. Standalone codegen tool on Main via PR #132 — keep that. |

### B-2 Items dropped via DNA philosophy gate (2026-05-25 `679e8a7`)

Anh applied "Nhanh / gọn / nhẹ / mượt / mở rộng" + "KHÔNG code phân mảnh" gate to TODO.md backlog. Items dropped as **aesthetic refactor without DNA win**:

| Dropped item | Why fail philosophy |
|---|---|
| VkToMacroChar syscalls per commit trigger | "Premature optimization without driver" — entry self-acknowledged |
| ScopedForegroundRestore RAII helper | Only 3 sites, no observed drift |
| Slot removal API + kInvalidSlotId sentinel | Premature abstraction, remove API doesn't exist |
| Action-string constants (DialogActions namespace) | Typo risk low for 4 dialogs × 5 actions |
| i18n import-confirm MessageBox | Single Vietnamese string, no localization roadmap |
| Test harness `--host-class` matrix | Marginal value, QA hasn't asked |
| TSF-apps toggle async register/unregister | Rare setup action (1×/user), has UAC consent + MessageBox feedback |
| Sub-dialog Instant Apply FindWindowW helper | Control-flow boilerplate (not data SOT), 15 sites decoupled — same risk as touching 15 files for aesthetic gain |

**Action:** These items are explicitly **NOT** to be re-opened without new driver (user signal, performance data, observed bug). Re-add only if philosophy gate flips green.

### B-3 W8+ body migration (DEFERRED — anti-pattern detected)

After Wave 3 closed at 3483 LOC, an external critique raised "HookEngine still implements 4 executor interfaces (IBackwardEdit + ICommitUndo + IEscRestoreRaw + IMacroExecutor) — feature bodies should migrate into Feature classes". Re-test against design philosophy:

| Test | Verdict |
|---|---|
| "Owner thật" — feature has independent state? | ❌ `commitStack_`/`inputHistory_`/`rawMacroBuffer_` are engine composition state, NOT feature state |
| "KHÔNG phân mảnh" | ❌ Move body needs either 5+ callback into HookEngine (inverse-god) OR migrate state cascade into ResetComposition/ClearWordState/etc. |
| "Pick END shape, ship atomically" | ❌ Either ship path leads to further refactor — undo or cascade |
| Hot-path budget impact | Neutral (~22ns virtual+callback per dispatch) |

**Decision:** SKIP. Body migration ≠ kiến trúc thật khi state không tách được — chỉ là cosmetic relocation. Same analysis applies to TypingEngine rules (ToneRule/ModifierRule body inside TypingEngine — engProt_/states_/rawInput_/escape_ are engine internals).

**Re-open trigger:** Only if a Feature gains independent state (e.g. a hypothetical UndoStack persistence layer separate from engine composition).

---

## Section C — HookEngine refactor backlog (ALL DONE)

| ID | Item | Status |
|---|---|---|
| ~~H1~~ | ~~ProcessKeyDown 561-LOC god-method decompose~~ — H1a/b/c shipped 2026-05-07 | ✅ PR #143-145 |
| ~~H2~~ | ~~Delete dead `CheckConfigEvent`~~ | ✅ PR #139 |
| ~~H3~~ | ~~`LowLevelMouseProc` race on `cachedFocusedHwnd_`~~ | ✅ PR #140 |
| ~~H4~~ | ~~Dual-route `TrackedSendInput` consolidate~~ — REJECTED 2026-05-07: it's a logging adapter, not dup | ❌ Wontfix |
| ~~H5~~ | ~~Macro extract to pure functions~~ | ✅ PR #141 |
| ~~H6a~~ | ~~Watchdog/self-healing~~ | ✅ PR #154 (HookSelfHealer reverted 2026-05-17) |
| ~~H6b~~ | ~~SPSC ring buffer Hook→Engine~~ | ❌ Wontfix 2026-05-09 |
| ~~H7~~ | ~~Strip Sprint 2 D4/T3 history comments~~ | ✅ PR #139 |
| ~~H8~~ | ~~Sprint 1 deferred (WaitOnAddress, ETW, fast-path foreground)~~ — Wave 3 PR 3.6 worker-thread doctrine + RCU + tag-dispatch addressed the practical concerns. Remaining items (ETW tracing) await Sprint 4 if observability ever becomes a need. | ✅ Effectively closed |
| **H9 (new)** | **Wave 3 owner extraction** — 4 owners shipped: HookLifecycle, FocusOwner, OutputDispatcher, CommitState. HookEngine 4634 → 3483 LOC (−25%). | ✅ Wave 3 PR 3.1-3.5 |

---

## Section D — TypingEngine refactor backlog (ALL DONE)

| ID | Item | Status |
|---|---|---|
| ~~T1~~ | ~~`SpellChecker → PhonotacticsValidator` rename~~ | ✅ PR #139 |
| ~~T2~~ | ~~Phonotactics onset agreement~~ | ✅ PR #147 |
| ~~T2.1~~ | ~~Vietnamese-rule consolidation sprint D1-D4~~ | ✅ PR #150-#153 |
| ~~T3~~ | ~~Phonotactics N1/N2/N3 vowel-coda~~ | ✅ PR #149 (retired in T2.1 D2) |
| ~~T4~~ | ~~Verify Hot-path Fix 3 ComposeAll buffer reuse~~ | ✅ Verified 2026-05-07 |
| ~~T5~~ | ~~Bug `cafcs → các`~~ | ✅ PR #146 |
| ~~T6~~ | ~~Bug Issue #117 `Lỗi → Lôĩ`~~ | ✅ Auto-resolved by H1 |

**Note:** 5 failing tests in `ToneMidSmartAccentTest` family are **pending feature work** (smart-accent + tone interaction), NOT refactor regression. Tests live in working-tree-modified `tests/TelexEngineTest.cpp` (uncommitted on branch).

---

## Section E — Dead code confirmed

Refreshed 2026-05-25 post-Wave-3 (most prior entries already cleaned via PR #139 / Sprint 2 D4 / ChannelTraits cleanup / Wave 3 PR 3.5).

**Remaining cosmetic (low priority):**

| Item | Location | Note |
|---|---|---|
| 3 stale comments referencing `HookEngine::ClassifyFocusedWindow` | `src/app/output/OutputInjectorFactory.{h,cpp}`, `RichEditEmReplaceSelInjector.cpp:51` | Method moved to `FocusOwner::Classify` in PR 3.2. Comments only — non-functional. Sweep optional. |
| Local branch `sprint-3/fsm-engine` | `.git/` | Cancelled FSM rewrite. `git branch -D sprint-3/fsm-engine` when ready. |

**File reconcile (verified clean post-PR 3.5):**
- All `.cpp` on disk referenced in CMakeLists.txt (both VKeyApp + VKeyLite targets)
- CMakeLists references no missing files
- Zero empty folders in `src/` and `tests/`

---

## Section F — Sequencing rule (historic, 2026-05-07)

**Rule:** Complete HookEngine refactor (Section C, H1-H6 except H8 roadmap) BEFORE TypingEngine items (Section D, T1-T4).

**Status:** Rule was followed. HookEngine + TypingEngine both architecturally closed by 2026-05-08. Wave 1-3 (2026-05-23 onward) was a fresh wave NOT bound by this rule — it was triggered by post-2026-05-08 file growth (Section H probe).

**Future application:** When new refactor wave arises (e.g. observability layer, plugin host), apply same sequencing — riskiest-file-first, single focused effort, bundle quick wins from sibling layers only when grouped.

---

## Section G — Recommended next steps (post-Wave-3, 2026-05-25)

### Immediate (anh next session)
1. **Windows verify** — build + chaos 11×5 on chrome/notepad/notepadpp/discord/gpt. Expected 53/55 PASS (chrome 1.2 omnibox + 1.3 ESC-bs-aa-b env false-fails). Any new fail = Wave 3 regression.
2. **Merge `feat/architecture-review-v3.1` → Main** when Windows green. Wave 1-3 = atomic merge (no further work scheduled on branch).
3. **Capture Wave 3 final state** in memory: index entry pointing to this REFACTOR_STATUS + the W7 retro doc (`docs/plans/2026-05-23-feature-pipeline-w7-retro.md`).

### Short-term (optional cleanup)
4. **3 stale comments sweep** in `src/app/output/` — 1-commit cosmetic if anh wants a clean snapshot at merge time. Skip if not.
5. **5 failing ToneMidSmartAccentTest** — pending feature work (smart-accent + tone interaction logic). Schedule as a separate non-refactor task.
6. **Local branch cleanup** — `git branch -D sprint-3/fsm-engine` if exists.

### Roadmap (no active work)
- **W8 body migration** — DEFERRED per Section B-3 anti-pattern test.
- **Observability layer (ETW / Perf histogram extension)** — out of scope unless user-visible perf bug arises.
- **TSF Phase 2/3 readonly context revive** — design doc at `docs/plans/2026-04-19-tsf-readonly-phase2-3-deferred.md`. No active driver.

### Re-open triggers (record them so future sessions can self-check)
- HookEngine.cpp ≥ 5500 LOC AND growth concentrated in single concern (>500 LOC region) → re-evaluate carve
- Specific bug class repeatedly hits ConfigApply / focus / commit / output layer → re-evaluate owner ownership
- Cross-platform tooling requirement → re-evaluate Linux-portability of config/engine layers

---

## Section H — De-god probe outcome (2026-05-22, historic) + 2026-05-25 update

**Context (2026-05-22):** Post-Sprint-3 file growth surfaced HookEngine.cpp +862 LOC (24%) between 2026-05-07 architectural-close and 2026-05-22, concentrated in ConfigApply Phase 3a-3f RCU migration (~+500 LOC) and 5 commit-undo bug fixes (~+300 LOC). Closure was method-level decompose (H1a/b/c, H5), not file-level — growth signal disputed "complete" verdict for ConfigApply specifically.

**Probe scope:** Two-phase selective probe per `docs/plans/2026-05-22-hookengine-degod-probe.md`:

| Phase | Item | Status | Commits |
|---|---|---|---|
| 0 | Atomic enum migration | **SHIPPED** | `76511b1` + `8acef2e` |
| 1 | ConfigSnapshotBuilder lift | **SHIPPED** | `84a2b90` |
| 2 | GATE — score Phase 1 | **PAUSE verdict** | — |

**Phase 1 GATE scorecard (5 criteria):** 3 PASS + 1 PARTIAL + 1 NEAR-MISS → PAUSE per plan's decision matrix.

**Reasoning at the time:**
1. Linux-testability premise wrong (ConfigManager uses Win32 WinStrings.h)
2. LOC reduction marginal (-34 / 4429 = 0.8%)
3. No latent bug caught
4. Anh philosophy "không phân mảnh trừ khi mang lại hiệu quả"

**What Section H said NOT to do (2026-05-22):**
- ❌ Phase 3 QuickSync diff lift
- ❌ Full ConfigApplier class extraction
- ❌ Focus/AppProfile split — Win32-heavy, low test value
- ❌ CompositionController class extraction

**2026-05-25 UPDATE — Wave 1-3 outcome reframes the verdict:**

The "❌" verdicts above were correct for **Phase 1's specific test premise** (Linux-testability + method-decompose). But Wave 1-3 (2026-05-23 → 2026-05-25) used a **different lens** — concern-based owner extraction — and shipped Focus + CommitState successfully:

| Section H verdict (2026-05-22) | Wave 3 reality (2026-05-25) | Outcome |
|---|---|---|
| ❌ Focus/AppProfile split — Win32-heavy, low test value | ✅ PR 3.2 FocusOwner — clean concern boundary, 53/55 chaos PASS, no Linux test required because Focus is Win32-by-nature | Section H was right that Linux testability won't unlock; was wrong that the split itself wasn't worth it |
| ❌ CompositionController class extraction | ✅ PR 3.4 CommitState — owner of commit-undo state lifecycle | Section H underestimated the value of *commit-state* ownership specifically (a narrower scope than "CompositionController") |
| ❌ Full ConfigApplier class extraction | ✅ Wave 2 — different mechanism: dropped 5 cached fields entirely (no extraction needed), atomized 5 SharedState scalars, CAS-protected slow path | Section H's "ApplyConfig has too many callbacks" was right; Wave 2 solved it by removing the need (RCU + atomic), not by extraction |
| ❌ Phase 3 QuickSync diff lift | Not done (still Win32-bound) | Section H verdict stands |

**Updated philosophy from Wave 3 retro:**

> Method-decompose probe (H1a/b/c) optimizes for **call-graph depth** — extract big methods to private helpers.
> Owner extraction (Wave 3) optimizes for **state ownership** — group related state + behavior under a single owner with one concern.
> They are orthogonal lenses. A "PAUSE on method-decompose" doesn't preclude owner extraction.
> The TEST that matters: does the new owner have **a real independent concern**? Section B-3 anti-pattern check applies.

**HookEngine.cpp trajectory:**
- 4634 LOC (pre-Wave 1, 2026-05-22)
- 4395 LOC (post-Phase 0+1, 2026-05-22, Section H PAUSE verdict)
- 4432 LOC (post-PR 3.1 HookLifecycle, 2026-05-23)
- 3868 LOC (post-PR 3.2 FocusOwner, 2026-05-24)
- 3483 LOC (post-PR 3.5 Cleanup, 2026-05-24)

**Re-open trigger conditions (unchanged):**
- HookEngine.cpp ≥ 5500 LOC AND new growth concentrated in a single concern area (>500 LOC in one region)
- Specific bug class repeatedly hits a layer where tests would have caught it
- Milestone requires Linux-portable config layer (e.g. cross-platform tooling)

Otherwise: HookEngine architecturally closed at 3483 LOC post-Wave-3. Stop further cosmetic decompose. **Section B-3 anti-pattern test applies to any new "should we extract X" proposal.**

---

## Section I — Wave 1-3 design lessons captured

Three cross-cutting lessons surfaced during Wave 1-3 that future refactor work must honor:

### I.1 — Cross-platform pitfalls (PR 3.1+ recurring)
1. **Linux build doesn't catch MSVC C4996.** `std::filesystem::u8path` C++20-deprecated. Conditional construction required:
   ```cpp
   #ifdef _WIN32
       const std::filesystem::path fsPath(Utf8ToWide(utf8Path));
   #else
       const std::filesystem::path fsPath(utf8Path);
   #endif
   ```
2. **Clang doesn't catch MSVC C4456 shadow.** Adding `const auto cfg = config_.load(...)` at function top → grep inner `auto cfg = ...` re-loads + delete.
3. **Grep filter pitfall on member-rename audits.** `mailbox_\.` (with dot) missed `DrainScope scope(mailbox_)`. Use `\bmailbox_\b` for member-rename audits.

### I.2 — Owner extraction litmus test
Section B-3 codified: owner extraction passes only when the new class has **a real independent concern**, not just a name. Tests to apply:
- Does the proposed owner have **state** that lives independently (not just engine internals)?
- Does the proposed owner have a **lifecycle** distinct from the host (e.g. Win32 handles, separate thread)?
- Can the host **read from the owner via getters** without 3+ callbacks back?

PR 3.1-3.4 all passed. W8 body migration fails all 3 → SKIP.

### I.3 — DNA philosophy gate (2026-05-25 TODO curation)
Anh codified the gate: "Nhanh / gọn / nhẹ / mượt / mở rộng" + "KHÔNG code phân mảnh". Apply to any new refactor TODO before adding to backlog:
- "Aesthetic refactor without DNA win" → drop
- "Premature optimization without driver" → drop
- "Premature abstraction without user" → drop
- "Single-call-site helper" → drop unless data SOT consolidation

Items that **pass** the gate get added; items that **fail** are explicitly listed in Section B-2 so they don't resurface.

---

## How to update this doc

This is a living inventory — update as items ship or scope changes:

- **Item shipped:** move from C/D to A with merge SHA + date.
- **New refactor item discovered:** add to C (HookEngine) or D (TypingEngine) with effort estimate. Apply Section I.2 owner litmus test + Section I.3 DNA gate BEFORE adding — if either fails, don't add (log in B-2 instead).
- **Sequencing decision changed:** update Section F.
- **Vital signs drift:** refresh top table at next major checkpoint.

When this doc is stale (>1 month since last refresh), re-run the survey via:
```
Agent (Explore, very thorough): codebase health survey + refactor inventory cross-reference vs latest Main
```
