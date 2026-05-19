# TODO

> Active follow-ups only. Resolved/landed entries archived in `TODO-ARCHIVE.md`
> (full git history preserved via `git log -p docs/TODO.md`).

## ✅ RESOLVED: P0 — engine_ single-writer violation (2026-05-19, P3e `8e9b5fb` + P3f `775487d`)

The P0 race I flagged during the Phase 3 review and deferred here with
"Phase 3 RCU will close it" was incomplete — Phase 3c moved
ReloadFromToml off the hook thread but kept `CommitComposition` +
`engine_ = Create()` inline on the worker thread, racing against the
hook hot path's unprotected `engine_->Peek/Push/Count` reads.

`run-chaos.ps1 -InjectConfigReloadMs 50` surfaced this in two layers:

- **Layer 1 (UAF):** 11/55 failures — mid-word composition state loss,
  scrambled output (e.g. `uống` → `uôngs`, `trường` → `ưư`,
  `bình thường` → `thfươ ng`). **P3e fix:** collapse to one writer
  (hook thread) via the dormant `ApplyConfigOnHookThread` handler P2c
  had pre-wired into the kConfigApply mailbox bit. ReloadFromToml +
  QuickSync slow path publish the new config_ and post kConfigApply;
  the hook drain runs CommitComposition + engine swap +
  currentMethod_.store atomically. UAF closed → 6/55 fails left.

- **Layer 2 (mid-word reset):** P3e's drain ran ApplyConfigOnHookThread
  unconditionally on every kConfigApply bit, recreating `engine_` even
  when `engine_->Count() > 0` (uncommitted word). User-visible quirk:
  partial words committed visibly. **P3f fix:** drain latches the
  apply via `deferredConfigApply_` atomic and gates on
  `engine_->Count() == 0`. The apply lands on the keystroke following
  a natural word commit / backspace-empty / focus reset. Imperceptible
  at human typing pace. Stress chaos confirms 0/55 engine bugs (only
  the Chrome omnibox autocomplete flake remains — non-engine).

**Lesson:** "this race will be closed by Phase X" is not a safe
deferral if Phase X doesn't actually RCU the field. Validate the
deferred fix delivers what was promised; chaos under stress is the
behavioral gate. AND: even when the UAF is fixed, the behavioral
trade-off (mid-word reset on reload) is itself a visible quirk worth
fixing. Stress chaos surfaces both layers in one run.

## 🟢 Phase 5 — HookEngine class split: DEFERRED (decision 2026-05-19)

Design doc §Phase 5 marked the class split CONDITIONAL: "decide after
Phase 3 ships". With Phase 2-4 now landed on `feat/architecture-review-v3.1`,
the four service boundaries the reviewer proposed are visible:

| Boundary | Existing component | Could become |
|---|---|---|
| Command intake | HookCommandMailbox + DrainHookCommands + drain handlers | `HookCommandQueue` class |
| Config snapshot | ConfigSnapshot + RebuildSnapshotFromToml + pendingConfigReload_ | `ConfigSnapshotPublisher` class |
| Output dispatch | IOutputInjector + injector_ + injector strategies | already extracted (Sprint 2 T3) |
| State mutator | engine_ + composition fields + 8× VKEY_ASSERT_HOOK_THREAD methods | `HookStateMutator` class |

HookEngine.cpp is 4239 LOC post Phase 4 — large but not unmanageable.
Pre-Phase-2 was ~3500 LOC; net +700 LOC for mailbox infra + new method
bodies + comments. The seams are **conceptually clear** even without
the class boundary; readers can navigate via the section comments
("Phase 2a — Hook-thread command drain", "Phase 3d — single source of
truth for ConfigSnapshot rebuild", etc.).

**Defer rationale:**

1. Architecture review's correctness goals (Rule 11.2 + 11.3 compliance,
   single-writer invariant, RCU for variable-size config) are already
   met by Phase 2-4. Phase 5 is structural cleanup, not correctness.
2. Refactor cost: 2-3 days work + high regression risk (touching 8
   composition-state methods, the LL callback, the drain dispatch).
3. No production signal yet that the size is a debugging burden.
   Solo-dev workflow — anh navigates the file fine today.
4. ThreadIdProvider (separate TODO below) would come for free with the
   split's `ThreadContext`, but the test gap it would close is also
   indirectly covered by Windows chaos.

**Trigger to revisit:** any one of —
- Incident debugging a Phase 2-4-touched section takes > 1 day
- HookEngine.cpp grows past 5000 LOC from unrelated work
- Need to plug a 5th component (e.g. metrics) and lack a clean seam

## ✅ RESOLVED: `appSendMethodOverrides_` race — last plain map promoted to ConfigSnapshot (2026-05-19)

P3d had folded every other variable-size config map (excluded apps,
TSF apps, macro table, encoding overrides, input-method overrides)
into ConfigSnapshot but left `appSendMethodOverrides_` behind because
only main-thread `ClassifyFocusedWindow` reads it. Phase 2-3's thread
split exposed the gap: writer ran on the worker (`RebuildSnapshotFrom
Toml` did `.clear()` then entry-by-entry insert), reader on main, no
mutex → torn-read window of ~5 ms on every TOML reparse. Self-recovering
quirk (next focus re-classifies), but strict Rule 11.3 violation.

**Fix (option 1 from previous entry):** added `appSendMethodOverrides`
to `ConfigSnapshot` next to the encoding / input-method maps. Extended
`ConfigSnapshot::Build` to take + move the third override map.
`RebuildSnapshotFromToml` now builds all three override maps locally
during the single TOML pass and hands them to Build; the legacy
HookEngine member is gone. Reader at `ClassifyFocusedWindow:3228`
loads `configSnapshot_` once and reads `snap->appSendMethodOverrides`
— lock-free, immune to torn reads.

Tests: `ConfigSnapshotTest` (DefaultIsEmpty + StoreLoadRoundTrip +
Build round-trip + EmptyInputs) all extended to cover the new field;
`ConfigReloadBurstTest` gen-derived snapshot includes a parity-toggled
send-method entry per generation so the integrity check pins it to
source. 1835/1835 Linux gtest PASS. Notepad chaos baseline + stress
(`-InjectConfigReloadMs 50`) both 11/11 PASS.

**Why option 1 over option 2 (mutex guard):** option 2 would have
forced main-thread `Classify` to take a lock that the worker holds
for the 5 ms rebuild → potential blip in focus-change UX. Option 1
keeps the hot side lock-free and matches the rest of the snapshot.

## 🟢 ThreadIdProvider for `pendingConfigReload_` routing tests (review 2026-05-19)

Phase 3 added thread-aware routing in `QuickSyncFromSharedState`:
```cpp
if (hookThreadId_ != 0 && GetCurrentThreadId() == hookThreadId_) {
    pendingConfigReload_.store(true, release);
} else {
    ReloadFromToml();  // inline on worker
}
```

Phase 4 gtests don't cover this routing — they test the data-structure
RCU patterns (ConfigSnapshot, IOutputInjector swap) but not the
thread-id branching itself. Reason: HookEngine is Win32-only and not
linked into VKeyTests; Linux gtest has no way to drive the routing.

Design doc §Phase 4 mentioned the fix: a `ThreadIdProvider` interface
(production reads `GetCurrentThreadId()`, tests inject fake IDs) —
same pattern as Sprint 1 D5 abstractions. Not implemented in Phase 4.

Behavioural verification today: Windows chaos runs (54/55 PASS across
P3a-P3d + review cleanup) exercise the routing transparently. A
deterministic gtest would catch routing regressions earlier.

→ Defer to Phase 5 (HookEngine class split) — the split will likely
introduce a `ThreadContext` boundary anyway, at which point this
abstraction comes for free.

## 🟢 Phase 3 latency edge cases (Phase 4 chaos targets)

Phase 3 (`feat/config-rcu-snapshot`) shipped clean. Two latency edges
are inherent to the deferred-reload design — worth driving deterministic
coverage when Phase 4 replay harness lands.

### A. Macro-toggle on hook thread (≤200 ms staleness)

User toggles macroEnabled in Settings → continues typing in target app
without focus switch → macro behavior takes effect only after the next
worker tick (≤200 ms cadence). Pre-P3c was instant on next keystroke.
Trade-off: Rule 11.2 (TOML parse off hook) wins over the 200 ms latency.

Phase 4 chaos scenario candidate:
`--inject-macro-toggle-mid-typing` — open Settings, toggle macro, hold
focus, type 20 keystrokes within 200 ms, verify macro applies before
the 200 ms tick boundary.

### B. `pendingConfigReload_` lost on rapid Stop()

Edge case: user saves Settings → hook QuickSync sets pendingConfigReload_
→ app shutdown before worker tick drains. Config save to SharedState
is already persisted; TOML save deferred ≤30 s — restart loads correctly.
No data loss, just no in-session effect. Acceptable.

Phase 4 scenario: `--inject-config-save-then-immediate-exit` — bump
configGeneration, send WM_CLOSE within 50 ms, restart, verify config
loaded from TOML/SharedState matches the bump.

## 🟢 CODING_RULES & code drift residue (2026-05-19)

Mechanical refresh of CODING_RULES landed in `6171a82`. Two follow-up items
left out of scope (semantic / requires architecture work):

### A. Rule 11.3 forbidden-pattern example matches production code — **RESOLVED Phase 3** (`feat/config-rcu-snapshot`)

Rule 11.3 (`docs/CODING_RULES/11-hook-system-rules.md`) showed:
```
std::lock_guard _lock(stateMutex_);
ReloadFromToml();  // file I/O holding lock!
```
as a forbidden pattern that production code did anyway on the
QuickSyncFromSharedState slow path.

**Fix landed in P3c** (`574ef2a`): hook QuickSync now sets
`pendingConfigReload_` instead of running ReloadFromToml inline; worker
OnTickPoll drains the flag and runs Reload on the worker thread.
**P3d** (`ff0437b`) cleaned up by collapsing the four legacy Reload*
methods into one `RebuildSnapshotFromToml` helper. Rule 11.3 example
now matches reality.

### B. Inconsistent `LowLevelHooksTimeout` documentation in HookEngine comments

Three different timeout values appear in comments without reconciliation:
- `HookEngine.cpp:223` says "clamped to 1000ms"
- `HookEngine.cpp:3166` says "300ms LowLevelHooksTimeout"
- `HookEngine.cpp:3487` says "default 500 ms"

Win32 docs: default is 300ms, configurable via
`HKCU\Control Panel\Desktop\LowLevelHooksTimeout`. Comments should agree.
Code drift, not rule drift. Single cleanup PR; 5 minutes.

## 🟡 `power → pởe` ở spell-check OFF (2026-05-18)

### Triệu chứng

`power` typed in Telex (hoặc UserDefined w=HornOrInsertU) với `spell_check=false`
→ `pởe` (sai). Spell ON thì OK (`power` literal).

```
Telex spellON  "power": [p][po][pow][powe][power]   ✓
Telex spellOFF "power": [p][po][pơ][pơe][pởe]       ✗
UD    spellOFF "power": [p][po][pơ][pơe][pởe]       ✗ (config user thật)
```

### Root cause

`IsBlockedEnglishModifier` table (`pow`/`upw` prefix) chỉ active khi
`effectiveSpellCheck = config_.spellCheckEnabled && !allowEnglishBypass`.
Spell OFF → gate skipped → `o + w` apply Horn (P6) → `pơ` → tone `r` rơi
lên `ơ` → `pởe`.

`IsHardEnglishStart` không bắt `po` (po có thể là tiếng Việt: pờ, pố...),
nên TIER1 cluster check pass. Free-mark fail không trigger vì `e` đến sau
khi đã có ư/ơ.

### Fix candidates

1. **Move `IsBlockedEnglishModifier` ra khỏi `effectiveSpellCheck` gate** —
   prefix table luôn check bất kể spell. Risk: pre-existing tests dựa
   vào behavior này có thể fail.
2. **Mở rộng `IsHardEnglishStart`** với heuristic vowel-consonant patterns
   khi spell OFF. Risk: over-blocking tiếng Việt.
3. **Document limitation** — spell OFF = "Vietnamese-mode bias", users gõ
   English nên bật spell hoặc dùng escape key.

Cần probe + decision trước khi fix. Liên quan fix `customKeyMap design
alignment` (2026-05-18) — Section 2d của `HandleModifierAction` đã có
guard mirror 2a, nhưng `pow` không trigger bias HardEnglish ở spell OFF
→ guard không kích hoạt.

### Pointer

- `src/core/engine/EnglishProtection.h:451` — `IsBlockedEnglishModifier`
- `src/core/engine/TypingEngine.cpp:423,469` — gate sites in 2a/2d
- Probe scenarios: see session log 2026-05-18

---

## 🟡 Commit-undo replay sai sau auto-restore + gõ-lại + BS (2026-05-18)

### Triệu chứng (user report)

Sau khi commit từ tiếng Việt rồi gõ một từ bị auto-restore (engine guess sai
→ revert về raw), nếu user gõ thêm vài chars rồi BS xoá sạch, **engine
"kéo" từ tiếng Việt cũ vào composition**, dính với phím alpha tiếp theo →
phải BS tận từ cũ mới gõ tiếng Việt lại được.

User quote: *"tai sao chu lieu phai BS toi tu truoc do moi go tieng viet
lai duoc"*

### Repro chính xác (từ log 2026-05-18 09:43:35)

1. `t a i ␣` → commit `tài`, stack=[`tài`], state=`Ready` (HookEngine.cpp:1544)
2. `l i e e j e u` (7 phím, có 1 `e` thừa sau `j`) → engine state lệch
   thành `lieẹu` (dấu nặng nhảy sai vị trí). State qua `Ready→Idle` ở phím
   `l` (line 1196).
3. `␣` → `CommitComposition`: `Commit()` cho ra `lieẹu`, khác screen text
   `lieẹu` → **không khác** → wait, log nói `AutoRestore: 'lieẹu' → 'lieejeu'`,
   tức `Commit()` returns raw `lieejeu`. Không push stack (line 1923),
   `pushedToStack_=false`. State giữ nguyên `Idle`. Stack vẫn = [`tài`].
4. BS×7 xoá `lieejeu` trên màn hình. Engine count=0 nên BS **không** vào
   `HandleBackspace` (line 1495 cần count>0). Mọi BS chỉ raw passthrough.
   State vẫn `Idle`.
5. `i i e e j` (5 alpha) → engine build state `iieej` (passthrough,
   không transform). State `Idle`. Stack vẫn = [`tài`].
6. BS×5 → mỗi BS đi vào `HandleBackspace` nhánh `count > 0`. Lần cuối
   (`count=1→0`) rơi vào nhánh `count==0` của `HandleBackspace`
   (line 1874-1892):
   - `previousComposition_` empty → skip `SendBackspaces` block
   - `!commitStack_.empty()` (vẫn còn `tài`) → `SetCommitUndoReady()` →
     state=`Ready`
   - Log: `HandleBackspace: engine empty, stack has 3 entries → state 1`
7. BS lần nữa → `HandleCommitUndo` line 1024 match (Ready + VK_BACK + count=0)
   → state=`Primed`, BS passthrough xoá space → screen `tài`.
8. `l` → line 1115: state Primed + alpha + vnMode → `ReplayCommittedChars()`
   → engine nạp `tài` từ stack, append `l` → engine state `tàil`.
   Log: `commit-undo: replaying + alpha 'L' (stack_top='tài' stackSize=3)`.
9. User gõ tiếp `ieeuj` mong ra `liệu`, nhưng engine state là `tàilieeuj`
   passthrough (vì engine không thấy diphthong hợp lệ trong context `tàili...`).
   → Phải BS×7 về `tà`, space commit, rồi gõ `lieeju` mới ra `liệu`.

### Root cause

`HandleBackspace` nhánh `count > 0 → 0` (line 1888) rearm
`SetCommitUndoReady` mỗi khi engine vừa empty trong khi stack non-empty.
Code này **giả định** stack top vẫn là từ ngay trước cursor — đúng cho
trường hợp single-word commit, nhưng **sai** khi giữa lần push gần nhất và
hiện tại có một auto-restored commit (push bị skip ở line 1923).

Cursor lúc đó thực sự đang đứng ngay sau `tài␣` (vì user đã xoá hết phần
sau), nên về mặt vị trí thì replay đúng — nhưng về mặt **ý định người
dùng**, họ đã từ bỏ word đó và bắt đầu word mới, không muốn dính vào `tài`.

### Liên quan: case (6) — feature đáng lẽ có nhưng chưa work

Hành vi user kỳ vọng (anh nói: *"ủa con trỏ đã BS về từ tai rồi mà, thì
phải sửa được chứ"*): sau auto-restore, BS xoá hết từ bị restore + BS thêm
để xoá space → engine kéo từ tiếng Việt cũ ra cho user gõ tone sửa
(`tài` + `j` → `tại`).

**Hiện không work** vì sau auto-restore, mọi BS đều có engine count=0, không
vào `HandleBackspace`, không trigger `SetCommitUndoReady`. State luôn `Idle`,
không bao giờ qua `Ready→Primed`. Gõ alpha tiếp → start word mới.

Bug ở mục trên là *side-effect* của user accidental gõ thêm `iieej` rồi BS
sạch — chỉ đường count>0→0 mới tình cờ trigger replay.

### Fix design — 2 phương án

**Phương án 1 (chỉ fix bug, an toàn, ship nhanh):**

Thêm `bool stackTopAdjacent_ = false;` vào HookEngine state.

- `CommitComposition` line 1936 (sau push thành công): `stackTopAdjacent_ = true`
- `CommitComposition` line 1918 (khi `restored=true`): `stackTopAdjacent_ = false`
- `HandleBackspace` line 1888: đổi `if (!commitStack_.empty())` →
  `if (!commitStack_.empty() && stackTopAdjacent_)`
- `ReplayCommittedChars` sau khi pop stack:
  - Stack rỗng → `stackTopAdjacent_ = false`
  - Stack còn → `stackTopAdjacent_ = true` (entry tiếp theo trở thành adjacent)
- `ResetComposition` / `CancelCommitUndo`: `stackTopAdjacent_ = false`

Effect: case (6) vẫn không work (giữ status quo), bug được fix.

**Phương án 2 (fix bug + add feature 6, phức tạp hơn):**

Track distance từ cursor đến stack top — `int autoRestorePendingChars_`:

- `CommitComposition` khi `restored=true`: set =
  `committed.size() + leadingTriggersForCurrentWord_ + 1` (cho commit trigger).
  Hoặc đếm theo `previousEncodedWidths_` cho code page non-Unicode.
- Mỗi BS khi `count==0` và `autoRestorePendingChars_ > 0`: decrement.
  Khi về 0 → cursor sát space → `SetCommitUndoReady` → state=`Ready` →
  BS kế xoá space + Primed → alpha replay.
- Bất kỳ alpha/digit/macro key nào khi `autoRestorePendingChars_ > 0`: set = -1
  (invalidate, đã có user activity, không còn pure-BS-chain nữa).
- `HandleBackspace` nhánh count>0→0 (line 1888): chỉ rearm nếu
  `autoRestorePendingChars_ == 0` (đã consume xong) **hoặc** chưa có
  auto-restore nào (tức stack top thực sự adjacent từ đầu).
- `ResetComposition` / commit thường: reset về 0.

Effect: cả bug và feature (6) đều work.

### Constraints (rules đã đọc)

- Hook rules (docs/CODING_RULES/11): không alloc, không lock, atomic-only.
  Cả 2 fix chỉ thêm 1 int/bool state → OK.
- Refactoring checklist (docs/CODING_RULES/10): không liên quan invariant nào.
- HookEngine.cpp là Win32-only → không có Linux gtest cover được. Phải
  manual test trên Windows từng case 1-6 trong báo cáo dưới.

### Test plan (manual Windows)

Setup: enable Telex Vietnamese mode.

1. `tai␣` → ` ` (xoá space bằng BS) → `j` → kỳ vọng `tại` (single-word
   commit-undo, không vỡ)
2. `tai␣loi␣` → BS×6 → `i` → kỳ vọng `tải l + i` (multi-word chain)
3. `tai␣s` → kỳ vọng `tái` (tone modifier exempt — Sprint 2 D1)
4. `tai␣␣` → BS BS BS `j` → kỳ vọng `tại` (pendingTrigger)
5. `tai␣` ESC → kỳ vọng raw `tai` (ESC restore exempt)
6. `tai␣gogle␣` → BS×6 → `j`:
   - Phương án 1: kỳ vọng `j` (fresh, không replay)
   - Phương án 2: kỳ vọng `tại` (replay được)
7. **Bug repro**: `tai␣lieejeu␣` → BS×7 → `iieej` → BS×5 → `l` → kỳ vọng `l`
   (fresh, không replay `tài`). Cả 2 phương án phải pass.

### Recommendation

Em đề xuất **làm Phương án 1 trước** (fix bug, ship), Phương án 2 làm sau
nếu nhiều user request case (6). Lý do:
- Phương án 1 chỉ thêm 1 bool, 5 chỗ touch — nhỏ, dễ review
- Phương án 2 thêm state machine mới (counter + invalidation rules) — phải
  cover nhiều edge case (multi-byte width, leadingTriggers, code page)
- Phương án 2 là feature mới chưa từng có → cần Sprint discussion riêng,
  không bundle với bug fix

### File chính cần đụng

- `src/app/system/HookEngine.h` — thêm member
- `src/app/system/HookEngine.cpp` — 5 chỗ (Phương án 1) hoặc 7+ chỗ (Phương án 2)
- Manual test trên Win11 + Win10 (cả Notepad++ và Chrome omnibox để cover
  RichEdit và editMsg channel)

### Refs

- Log file: được dán nguyên si trong conversation 2026-05-18 09:43
- Existing tests: `tests/CommitUndoExemptionTest.cpp` (chỉ test pure logic
  của `IsCommitUndoExemptKey`, không cover HookEngine state machine)
- Related design doc: comment block ở `HookEngine.cpp:994-1003` (H1a
  extraction note) và `HookEngine.cpp:1311-1321` (Post-BS extension design
  2026-05-17)

## 🟡 EnglishProtection chưa catch English-only onset clusters (2026-05-13)

Test probe trên SimpleTelex + allowZwjf=true cho thấy display mangle nhiều
English word ngay cả khi không có double-tone:

| Keys typed | Display |
|---|---|
| where (5) | wh? (3) — `w→ư` + `r` tone applied |
| users (5) | ?e (2) — multi-consumption |
| perfect (7) | p?ct (4) |
| wherre (6) | where (5) — escape gesture cứu |
| ass / bass / pass / mass / less / miss / sorry / error | mất 1 char |
| stress | "stress" (5) — EnglishBias catch được `str-` ✓ |

ESC restore raw (escRawHistory_) work-around được — PeekRaw luôn trả full
keystroke sequence. Nhưng display sai trong lúc gõ vẫn gây bối rối.

**Root cause**: `IsBlockedEnglishTone` (TypingEngine.cpp:296) chỉ block khi
`bias == HardEnglish + !allowEnglishBypass`. Onset 2-3 char như `wh-`,
`us-`, `pe-`, `wr-`, `kn-`, `sc-`, `sp-` chưa đủ confidence để lên HardEnglish.

**Fix scope** (project riêng):
1. List English-only initial clusters: `wh`, `str`, `spr`, `scr`, `kn`, `wr`,
   `gh`, `pn`, `ps`, ... (audit để không over-block "ph" của Vietnamese
   quick-start `f→ph`)
2. Map vs Vietnamese valid onsets (chỉ `kh`, `ph`, `th`, `ch`, `nh`, `ng`,
   `ngh`, `tr`, `gi`, `qu`)
3. Bump bias to HardEnglish khi match English-only cluster ngay từ char 2-3
4. Tests exhaustive: Vietnamese cases không bị over-block, English bị block đủ sớm

Cũng cần **block modifier keys** (w, [, ]) chứ không chỉ tone keys khi bias
là HardEnglish. Hiện tại `w` standalone vẫn insert `ư` qua P8 dù onset
indicate English.

**Wherre case** sẽ tự fix khi EnglishProtection cải thiện: `wh-e` không apply
tone trên 'r' → không có escape gesture cần → display = "wherre" matching
PeekRaw → ESC consistent.

**Sub-issue: Tone-escape drops display char** — `a-s-u-s` display "aus".
`ProcessTone` escape branch (TypingEngine.cpp:503-514) chỉ thêm phím tone
thứ 2 vào `states_`, không khôi phục phím tone đầu. Tested behavior:
ass→as, bass→bas, etc. ESC restore raw work-around qua escRawHistory_,
nhưng có thể fix sâu hơn ở engine (Unikey/EVKey: cả 2 keys được giữ
literal). Cũng sẽ break `usser → user` test (TelexEngineTest.cpp:3198).

Refs: brainstorm session 2026-05-13, commit 892c9e7.

## 🟡 `IsWebView2App` perf instrumentation — investigate when reports recur (2026-05-11)

User report (1 occurrence, single user, not reproducible locally): after
locking the machine for an extended period then unlocking, typing felt
sluggish; Task Manager showed VKey at 100% CPU and several other apps
spiking. Reporter explicitly noted: *"không chắc lỗi hoàn toàn do
VKey hay không"*.

Code smell identified during triage in `HookEngine::IsWebView2App`
(`src/app/system/HookEngine.cpp:2282`):

- **Pass 1**: `CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)`
  against the foreground process. Cross-process snapshot acquires the
  target's loader lock briefly and is frequently intercepted by AV
  (Defender treats cross-process module enumeration like scanner
  behavior). Cost is bounded by `appProfileCache_` (64 entries,
  HWND-keyed) — *each new HWND* runs the snapshot once.
- **Pass 2** (fallback when pass 1 returns `INVALID_HANDLE_VALUE`, common
  on AppContainer / cross-IL targets like `LockApp.exe`, `LogonUI.exe`):
  `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)` — system-wide
  process table snapshot. Heavier in absolute terms; runs once per HWND
  miss when pass 1 fails.
- No `WTSRegisterSessionNotification` → `OnTickPoll` (200ms) keeps
  polling during lock/unlock. Lock cycle transits focus through several
  system HWNDs (`LockApp.exe`, `LogonUI.exe`, credential UI,
  `ShellExperienceHost.exe`…) — each new HWND triggers a snapshot pair.
- `webView2PositiveCache_` is positive-only (line 2275 comment).
  `appProfileCache_` *does* cache negative results by HWND, but is
  bounded at 64 and HWND-keyed (not PID-keyed), so cache thrashing on
  high-HWND-churn moments (lock screen, many notification toasts) can
  re-trigger snapshots for previously-seen apps.

**Why not fix preemptively:** the 4 candidate fixes (negative cache with
TTL/generation, system-process blocklist, session-lock pause, drop pass
2) each add either new state machinery, new lifecycle callbacks, or
behavior changes that risk regressing Tauri/Dorion WebView2 detection
(currently working, user-confirmed). Single anecdotal report + Windows
post-unlock is itself a known multi-app CPU spike window (Defender
resume scan, Search indexer wake, OneDrive sync, Edge update check) →
attribution unclear, complexity:perf-gain ratio bad.

**Shipped instead:** `HOOK_LOG` line on every cache-miss path entry,
recording `pid`, `exe` basename, `pass1` result
(`module_found`/`module_notfound`/`snap_fail`), `pass2` result
(`skip`/`proc_found`/`proc_notfound`/`snap_fail`), `result`, and `dur`
in ms. Cost when log toggle OFF = 0 (runtime-gated atomic load + branch
short-circuits arg evaluation, per debug log toggle perf table above).

### Trigger condition for opening a real fix

- ≥2 independent user reports of post-unlock CPU spike, OR
- Field log shows a single `IsWebView2App` call exceeding 100 ms, OR
- Field log shows pass 2 (`TH32CS_SNAPPROCESS, 0`) running more than
  once per second during normal use (cache thrash evidence).

If none of the above land within ~2 months, close this entry — smell
was over-attributed to a noisy single report.

### Candidate fixes (do NOT pre-implement; design only if triggered)

1. Negative cache for `IsWebView2App` keyed on `exeFullPath` with
   generation-based invalidation (re-check on first keystroke into the
   app, not focus). Must not break Tauri lazy-load: WebView2 may load
   only on first embed → cache-miss-on-first-keystroke is the safe
   invalidation point.
2. System-process early-exit list before `IsWebView2App`: `LockApp.exe`,
   `LogonUI.exe`, `CredentialUIBroker.exe`, `SearchHost.exe`,
   `ShellExperienceHost.exe`, `StartMenuExperienceHost.exe`. Trade-off:
   maintenance burden as Windows ships new shell hosts.
3. `WTSRegisterSessionNotification(NOTIFY_FOR_THIS_SESSION)` →
   `OnTickPoll` early-return while `sessionLocked_` flag set. RDP edge
   cases (`WTS_REMOTE_CONNECT`/`DISCONNECT`) need test.
4. Drop pass 2 (`TH32CS_SNAPPROCESS`) or rate-limit to once per PID per
   N seconds. Trade-off: modern Tauri (host doesn't preload WebView2
   DLLs, only spawns `msedgewebview2.exe` child) regresses unless
   replaced with a cheaper signal (e.g. check for `Chrome_WidgetWin_1`
   descendant which Tauri WebView2 also instantiates).

User report origin: 2026-05-11 morning build feedback (Vietnamese:
*"bị 1 lần sau khi lock máy 1 thời gian, đăng nhập lại thì gõ bị đơ đơ,
mở task manager thì CPU lên 100%"*).

## 🟡 Debug log toggle — perf follow-ups when bug reports come in (2026-05-11)

Shipped `Settings → System → "Bật debug log"` runtime gate routing
`NEXTKEY_LOG/HOOK_LOG/TSF_LOG` (~207 sites) into `NextKey::Logger`. File
sink: `VKey_<process>_<pid>.log` next to VKeyApp.exe (fallback
`%APPDATA%\VKey\logs\`). PID-tagged so Chrome multi-process renderers
don't tear lines.

Measured perf characteristics (analytic, no Windows benchmark run yet):

| State | Cost / call site | Per keystroke (~10 sites) | % CPU @ 30 keys/s |
|---|---|---|---|
| OFF (default) | inline atomic load + branch, args NOT evaluated, ~3-5 ns | ~30-75 ns | ~0.0002% (imperceptible) |
| ON, normal typing | atomic + mutex + format + fputws + fflush, ~10-100 μs | ~100-1000 μs | 0.25-1% |
| ON, burst (60 keys/s) | same | same | up to 3% (still no visible lag) |

Hot path: `LowLevelKeyboardProc` budget ~100ms (Windows timeout 300ms).
Per-callback log cost when ON ≈ 250-500 μs → 200× safety margin. Even
worst-case AV-scan-on-write (~5ms/line) stays well under timeout.

### Follow-ups (open only if real user-reported issue)

- **Log rotation** — file grows ~10 MB/hour when ON. User who forgets to
  toggle off after 8h debug session ends with 80 MB file. Decide rotation
  threshold (5 MB? 50 MB?) + rename pattern (`.log` → `.log.1`).
- **Relax per-line `fflush`** — current `fflush` after every line costs ~5-50 μs
  syscall. If user reports lag while toggle ON, consider switching to
  default buffered I/O. Trade-off: lose ~10 trailing lines on crash, but
  cut ~90% of I/O cost. Issue #108-class bugs need the last lines so
  current default favors reliability.
- **Verbose-vs-normal split** — `HOOK_LOG` logs per-keystroke KEY trace
  (5-10 lines/key). If AV + slow disk push the hook near 300ms timeout
  on enabled state, split into a coarser "errors only" mode and a
  "verbose key trace" mode behind a second sub-toggle.
- **Audit dead `NEXTKEY_LOG` sites** — 100 NEXTKEY_LOG sites were
  `((void)0)` in Release before this change. They now compile to an
  inline gate but the args expressions still exist in the call. If any
  has expensive arg computation that was previously dead-code-eliminated,
  the dead code now stays in the binary (cold but adds icache pressure).
  Quick `objdump | grep Logger::Log | wc` after a Release build catches
  it.

### UI / test gaps (not perf)

- `btn-open-log-folder` click runs `SHCreateDirectoryExW` on first click
  if APPDATA folder doesn't exist → ~50ms UI stutter. Acceptable for
  click-once; only matters if visible to users.
- No Win32 integration test for "toggle UI → file actually written".
  Round-trip is covered at the encode/decode layer
  (`FeatureFlagsTest.RoundTrip_DebugLogEnabled`). Full path needs Sciter
  test harness — overkill until something breaks.

---

## 🟡 HeartbeatPublisherTest — strengthen `IdempotentStartWhileRunning` (2026-05-09)

Surfaced during code review of the watchdog opt-in lifecycle fix
(`docs/plans/2026-05-09-startup-ram-regression-design.md`).

**Gap:** `IdempotentStartWhileRunning` test only verifies `Start()` returns
`true` on the second call. It does **not** verify that no second thread is
spawned. Behavior is correct today because of the
`if (heartbeatEvent_) return true;` guard in `HeartbeatPublisher::Start()`,
but if a future refactor removes that guard, the test would still pass —
the regression would only surface as a thread leak (visible in Process
Explorer threads count) or a hang on `Stop().join()` if the second thread
is dangling.

**Fix idea:** expose a thread-id getter or atomic launch counter on
`HeartbeatPublisher`, then assert it equals 1 after a second `Start()`.
Alternatively, expose a `IsRunning()`-style state and confirm the second
Start observed `running == true` without re-spawning.

**Effort:** ~30 min. Requires touching `HeartbeatPublisher.h` interface
(add an internal counter or thread-id accessor for tests only).

**Priority:** Low. Defer until next change to `HeartbeatPublisher` lands —
tighten the test together with that work to avoid touching the class
twice.

---

## 🟡 Auto-cap on Enter — keystroke-FSM asymmetry vs space (2026-05-08)

**Symptom:** Pressing Enter to break a line, then typing a letter → letter
gets capitalized even when the preceding text is not a sentence end.
Surfaced in `runner-channeltraits-chrome.log` (manual session after chaos
runner): user typed `trong` + edits + `Enter` + `r` → engine emitted `R`.

**Root cause:** `HookEngine.cpp:1258-1259` unconditionally promotes
`autoCapState_` to `ReadyToCapitalize` on `VK_RETURN`, regardless of what
preceded the Enter. The space branch (`:1252-1257`) has a whitespace
gate — it only promotes when the prior state is `AfterPunct` or already
`ReadyToCapitalize`. Enter does not have the same gate.

```cpp
} else if (vkCode == VK_SPACE &&
           (autoCapState_ == AutoCapState::AfterPunct ||
            autoCapState_ == AutoCapState::ReadyToCapitalize)) {
    autoCapState_ = AutoCapState::ReadyToCapitalize;
} else if (vkCode == VK_RETURN) {
    autoCapState_ = AutoCapState::ReadyToCapitalize;  // ← unconditional
}
```

**Why simple gating (Option B) is incomplete:** `AfterPunct` is sticky
across letters (`:1260-1261` "letter key — don't reset"). So
`chrome.com<Enter>r` would still cap because `.` set `AfterPunct` and
`com` did not reset it; Enter would then promote to `ReadyToCapitalize`.

| Case | Today (unconditional Enter) | Option B (gate Enter behind `AfterPunct`) | Desired |
|---|---|---|---|
| `Hello<Enter>r` | cap → `R` | no cap → `r` | no cap |
| `Hello.<Enter>r` | cap → `R` | cap → `R` | cap |
| `chrome.com<Enter>r` | cap → `R` | cap → `R` (sticky AfterPunct) | no cap |

**IPC anchor path is correct.** `SharedState.h::DeriveAnchorFromPreceding`
scans the buffer directly and distinguishes `prev == '.' && skippedWhitespace`
(sentence end) from `prev == '\n'/'\r'` (line start) properly — so when
TSF readonly is registered and anchor is available, HandleAlphaKey
(`:1598-1606`) ignores the keystroke FSM and uses anchor truth. The
asymmetry only bites in hook-only mode (no TSF readonly) where the
keystroke FSM is the sole truth source.

### Options

| Option | Effort | Fixes `chrome.com<Enter>r` | Compat |
|---|---|---|---|
| A — Drop Enter trigger entirely (delete `:1258-1259`) | Trivial | ✓ | Loses "Enter = sentence start" UX for normal paragraph breaks |
| B — Gate Enter behind `AfterPunct` (mirror space) | Trivial | ✗ (sticky `AfterPunct` through letters) | OK for `Hello<Enter>r` |
| B' — Add `LetterAfterPunct` sub-state | Medium | ✓ | Symmetric, principled, no-regression |
| D — Rely on TSF anchor only | None code-side; needs TSF readonly registered | ✓ (anchor logic already correct) | Only works when TSF DLL active and `TSF_READONLY` flag set |

### Recommendation

**B' or D.** B' is the right shape for hook-only mode (state machine
correctness without external dependency). D is the principled
architectural answer (single source of truth = the document), but
requires the TSF readonly path to be active across the apps where users
type — verify `TSF_READONLY` flag adoption in production first before
relying on D alone.

If pursuing B': add `AutoCapState::LetterAfterPunct` between `AfterPunct`
and `Idle`. Transition: any letter at `AfterPunct` → `LetterAfterPunct`.
Enter at `LetterAfterPunct` → `Idle` (paragraph break, not sentence end).
Enter at `AfterPunct` → `ReadyToCapitalize` (sentence end, then break).

### Verification before fix

- [ ] Capture more reproductions in hook log to confirm `chrome.com<Enter>r`
  cap pattern (today the log only shows `trong<edits><Enter>r` which is
  ambiguous — could be intended new sentence).
- [ ] Check `TSF_READONLY` adoption: which apps actually have the TSF DLL
  loaded? If majority, D becomes viable. If minority, B' is required.
- [ ] Add hook-engine-level tests covering the 3 cases in the table above
  (currently no Linux GTest for `autoCapState_` FSM transitions).

### Why this isn't urgent

Behavior matches the convention of most IMEs (Microsoft IME, Google
Pinyin, Unikey, EVKey all cap after Enter unconditionally). Users who
type code/email handles after a line break may notice; users typing
prose generally won't. Fix when a user complaint surfaces or as part of
a broader auto-cap refactor.

---

## 🟡 v3 Watchdog Smoke 4 + 5 — verify Task Scheduler at-logon trigger (2026-05-08)

Phase 2 watchdog smoke 1/2/3 PASS (crash respawn, graceful, hung UI). Smoke 4 + 5 deferred because they require a real logout/login cycle to fire the `\VKey\Watchdog` at-logon trigger.

**Smoke 4 — Kill watchdog alone:**
- `taskkill /F /IM VKeyWatchdog.exe` while VKey runs normally.
- VKey must continue functioning.
- After logout/login: Task Scheduler must relaunch VKeyWatchdog automatically.

**Smoke 5 — Kill both:**
- `taskkill /F /IM VKey.exe VKeyWatchdog.exe` simultaneously.
- After logout/login: Task Scheduler relaunches watchdog → watchdog observes events absent + process not running → respawns VKey.

**Smoke 6 — AV scan (low priority):**
- Run Windows Defender quick scan with watchdog active. Verify VKeyWatchdog.exe not quarantined / no false-positive on the small console-less WIN32 binary.

**Why deferred:** logout/login is disruptive and the trigger mechanism is Windows-managed (StartupHelper just registers the task). Risk of regression from our code is low — `RegisterWatchdogTask()` already verified during first-run UAC accept. Reopen if user reports auto-launch failure.

---

## 🟡 Vietnamese-rule consolidation — Path 1 migration + English-protection reuse remaining (2026-05-08)

**Project design philosophy (anh 2026-05-08):** *nhanh - gọn - nhẹ - mượt - plugin,
**không code phân mảnh***. Honored by the T2.1 sprint — single rule-data
header + plugin contract now exist; two consumers still hold local copies and
need migrating.

### Landed in T2.1 sprint (D1–D4)

| Commit | What |
|---|---|
| `48d25b1` | D1 — Lift `IsFrontBaseVowel` + front-vowel classifier into shared `core/engine/VietnamesePhonologyData.h` |
| `59fd807` / `02a1b2d` | D2 — Lift `kVCPairRules` per-nucleus allowed-coda bitmask into the shared header. Path 2 (`Phonotactics`) now uses `GetAllowedFinals()`; T3 N1/N2/N3 approximation retired. |
| `3f86c40` | D3 — `IPhonologyRules` plugin contract + `DefaultPhonologyRules` singleton. Path 2 (`Phonotactics`) consumes it via DI ctor. |
| `1bfffb7` | D4 — `PhonologyRulePackFactory` + `RulePackId` enum (single dialect today; swap-point for future packs). |

Result: rule data has one source of truth (`VietnamesePhonologyData.h`); the
wstring_view hot path goes through the plugin contract; future dialectal
variants plug in at the factory without forking validator code.

### Remaining (~1 day total)

1. **Path 1 (`PhonotacticsValidator`) migration** — still consumes
   `VietnamesePhonologyData.h` directly instead of going through
   `IPhonologyRules`. Self-documented at `PhonotacticsValidator.cpp:27-32`:
   *"did not migrate this validator. Revisit when a dialectal rule pack is
   actually needed."* Effort ~0.5 day. Discipline: byte-identical chaos.toml
   output, CharState packed-key hot-path cost preserved.
2. **English-protection consumer reuse** — `EnglishProtection.h` still
   re-encodes the `c/m/n/p/t` Vietnamese-coda lexicon (`IsHardEnglishEnd`
   ~line 81, `IsInvalidVietnameseCoda` ~line 289) for its English-bias
   detection axis. Should consume the same coda data from
   `VietnamesePhonologyData.h` rather than maintaining a parallel copy.
   Effort ~0.5 day, orthogonal to Path 1.

### Sequencing

Reopen only when (a) a dialectal rule pack becomes load-bearing (forces
Path 1 migration), or (b) the next phonology-touching feature lands and the
parallel English-coda copy starts drifting from the shared lexicon. Until
then current state is acceptable: the plugin shape exists, rule data is
centralized, and the residual duplication is contained to two well-marked
sites.

---

## 🟡 Test harness — `--host-class` matrix (Sprint 2 D6 deferred, 2026-05-05)

Sprint 2 plan §D6 Tasks 32-33 — `VKeyTestRunner` flag for forced host-class override. Marginal value given existing 132-case natural coverage; reopen as one focused task if QA later needs forced-cell testing.

---

## 🟡 `VkToMacroChar` syscalls per commit trigger (2026-04-22)

`src/app/system/HookEngine.cpp:2851-2888`. Calls `GetAsyncKeyState ×3`,
`GetKeyState`, `MapVirtualKeyW ×2`, `GetForegroundWindow`,
`GetWindowThreadProcessId`, `GetKeyboardLayout`, `ToUnicodeEx` each time.
Only runs on commit triggers (~10/sec human typing), not on the LL hook
hot path — per CODING_RULES Rule 11.3 the syscalls are on the cold path
and acceptable. Reopen only if profile data shows the foreground-HKL
lookup as hot; caching the HKL on focus change would save ~3 syscalls
per commit. No premature optimization without driver.

---

## 🟡 Sub-dialog Instant Apply — tech-debt items (2026-04-22)

- [ ] **`FindWindowW(L"VKeyTrayClass") + PostMessageW` pattern duplicated**
  Now in `AppHelpers.h::SignalConfigChange`, `SettingsDialog.cpp:548,698,1286`,
  `ClassicSettingsDialog.cpp:813,991,998,1033`. Candidate for a
  `PostToTrayWindow(UINT msg, WPARAM = 0, LPARAM = 0)` helper in `AppHelpers.h`.
  Low priority — consistent with existing pattern.

## 🟡 Auto-caps + TSF Apps Feedback — open follow-ups (2026-04-21)

User feedback batch (v2.1.19 Hybrid-TSF testing). Fixed items landed in commits
`7548dea`, `e53176b`, `a3f00c6`, `f89ea4d`. Remaining below.

### Unfinished from user feedback

- [ ] **Windows Search cannot type Vietnamese** — `searchapp.exe` / `SearchHost.exe`
  UWP AppContainer rejects third-party TIP load → TSF DLL never instantiated.
  User workaround (add to TSF list) DID NOT WORK (confirmed on v2.1.21).
  Observation: when Windows Search gains focus from Edge, Input Indicator
  auto-switches from "VKey Vietnamese IME" to "English (US) US Keyboard"
  — indicates Windows is forcibly changing the active IME profile, not just
  blocking our TIP. Screenshot evidence in feedback 2026-04-21.
  **Actual fix path**: add both exes to a TSF-EXCLUSION list ("force Hook
  for these") so Hook handles them. Confirm `WH_KEYBOARD_LL` reaches UWP
  AppContainer. Investigate whether IME profile auto-switch also suppresses
  hook delivery. Affects: Start menu, Settings app search, Win+S.

- [ ] **Arrow-left revive drops auto-cap state** — REPRODUCIBLE 100% in Edge (2026-04-21 retest)
  Steps: type `wqewqe` + space → displays `Wqewqe ` (auto-cap fired on
  first char). Arrow-left once (caret between 'e' and ' '). Type `a` +
  space → final text `wqewqea` (first `W` demoted to lowercase).
  Confirmed on v2.1.19 AND v2.1.21 in Edge native search, GitHub Issue box,
  Google Keep, Facebook. Config: simple_telex, auto_caps=true, tsf_apps
  includes msedge.exe.
  **Code trace expectation**: `InspectPrecedingTextEditSession` reads
  "Wqewqe", `tempEngine->SeedFromText` seeds states with isUpper=true for
  'W'. English-classification TBD — if `HardEnglish` → revive SKIPPED → 'a'
  starts fresh composition, W untouched → would show `Wqewqea`. If revive
  FIRES → SeedFromText + PushChar('a') → Peek composes "Wqewqea" with W
  upper. Either path preserves W — so observed lowercase-demotion is from
  a third code path not yet identified.
  **Hypothesis**: revive DOES fire, but `Peek()` output at
  `CompositionEditSession.h:444` emits lowercase; OR `SetCompositionText`
  writes a different string than composed.
  **Action**: instrument `EngineController.cpp:283` (revive log),
  `CompositionEditSession.h:444` (composed log), ask user to capture with
  DebugView++ and report the composed string.

- [ ] **Arrow-left revive breaks Vietnamese word (strips diacritics)** —
  REPRODUCIBLE 100% in Edge + Word 2024 LTSC (2026-04-21 retest)
  Steps: type `bưởi` + space → `Bưởi `. Arrow-left. Type any letter
  (`a`/`A`/`b`/`B`) → text becomes `buoi` (all caps + horn + tone LOST,
  typed character also missing or misplaced).
  Confirmed on v2.1.19 AND v2.1.21. Config: simple_telex, auto_caps=true.
  **Critical observation**: the OUTPUT "buoi" equals `SeedFromText`'s
  synthetic `rawInput_` (base letters only — see TypingEngine.cpp:1319
  which pushes only `base` to rawInput_, no tone/mod keystrokes). This
  strongly suggests an auto-restore path fires that returns
  `std::wstring(rawInput_.begin(), rawInput_.end())` — matches
  TypingEngine.cpp:1266 in `Commit()`.
  **But**: `ReviveAndTypeEditSession` uses `Peek()` not `Commit()`, so
  auto-restore shouldn't apply. Unless some other path reads rawInput_
  under invalid/HardEnglish state, or `SetCompositionText` is being fed
  raw characters instead of composed Peek output.
  **Action**: same diagnostic as item above. Add log at
  `CompositionEditSession.h:444`: `TSF_LOG(L"Revive composed='%ls'
  rawInput='%ls'", composed, raw)`. Repro in Edge and attach log.

- [ ] **Word-boundary protection test matrix**
  User asks whether gluing two words (no space) corrupts the earlier word in
  either Hook or TSF mode. Architecturally: Hook doesn't touch committed text;
  TSF revive only seeds the trailing word. No regression expected, but no
  explicit test. Add scenarios: `xinchao` + backspace-into-word + retype,
  `bưởichuối` edit sequences, commit-trigger behavior on punctuation glue.

### Tech debt surfaced during code review

- [ ] **Extract shared `Import/ExportStringList` helpers** — `src/app/dialogs/DialogUtils.h`
  4 dialogs now duplicate ~60 lines each: `ExcludedAppsDialog`,
  `MacroTableDialog`, `SpellExclusionsDialog`, `TsfAppsDialog`. Differences
  are: window title, default filename, file header comment, and line
  transform. A templated helper with `std::function<std::wstring(std::string)>`
  transform + 3 string params would unify them and prevent future drift.
  Touching all 4 dialogs in one refactor PR — out of scope for feature work.

- [ ] **i18n the import-confirm MessageBox** — `StringId::IMPORT_KEEP_EXISTING`
  All 4 list dialogs hardcode the Vietnamese UTF-16 escape sequence
  `L"Bạn có muốn giữ lại danh sách hiện tại không?"` + per-dialog title.
  Should go through `S(StringId::...)` like other user-facing strings. Pairs
  with the helper extraction above.

- [ ] **Action-string constants** — 4 dialogs
  `handle_event` compares raw wide strings (`L"import"`, `L"export"`,
  `L"close"`, `L"add-manual"`, `L"add-current"`, `L"delete"`,
  `L"get-running-apps"`). Define `namespace DialogActions { inline constexpr
  const wchar_t* IMPORT = L"import"; ... }` in a shared header so typos become
  compile errors. Pairs with the helper extraction above.

---

## 🟡 Hotkey Refactor — deferred items (2026-04-20)

All non-blocking; fixed items already landed in the refactor.

- [ ] **`ReloadFromToml` parses 7 TOMLs per config bump** — `src/app/system/HookEngine.cpp:368-472`
  Call graph on `configGeneration` bump:
  ```
  ReloadFromToml()
  ├─ LoadOrDefault()           → toml::parse_file  ①
  ├─ LoadMacros()              → toml::parse_file  ②
  ├─ LoadAppOverrides()        → toml::parse_file  ③
  ├─ LoadAllExcludedApps()     → toml::parse_file  ④
  ├─ LoadTsfApps()             → toml::parse_file  ⑤
  └─ configReloadCallback_()   [HotkeyWiring.cpp:28-37]
     ├─ LoadConvertConfigOrDefault()  → toml::parse_file  ⑥
     └─ LoadHotkeyConfigOrDefault()   → toml::parse_file  ⑦
  ```
  **7× parse of same file** per Settings Save. Cold cache ~35-100ms, warm cache <5ms.
  User-paced trigger → imperceptible. **Low priority** — profile first if perceived lag.

- [ ] **`ScopedForegroundRestore` RAII helper** — `src/app/system/TrayIcon.cpp:379-396`
  `prevFg = GetForegroundWindow()` + `SetForegroundWindow(prevFg)` pattern. Only 1 call site today; `ClassicDialogUtils.h:157` and `WindowPickerDialog.cpp:88` do similar one-shot restores but not the full save-and-restore pair. Not enough duplication to justify a helper yet — revisit if a 3rd call site appears.

- [ ] **Slot removal API + `kInvalidSlotId` sentinel** — `src/app/system/HotkeyManager.h:26-42`
  `using SlotId = size_t;` with default `0` means slot 0 is ambiguous (valid id vs. unset). Today's usage is fine (all slots registered at startup, never removed), but if slot removal is ever added, introduce `static constexpr SlotId kInvalid = SIZE_MAX;` and have `UpdateHotkey` return a bool or check against the sentinel. Low priority until a remove API is actually needed.

---

## 🟡 TSF Readonly Context — Phase 2 / 3 / shared infra (2026-04-19)

Phase 1 shipped: auto-cap via `HookContextAnchor` (commits `89d1add`..`b0bbb09`).
Design doc: `docs/plans/2026-04-19-tsf-readonly-context-phase1-design.md`.
Infra (`anchor.currentSyllable[16]`, seqlock helpers) already in place; phase 1
does not read the syllable field.

### Phase 2 — Cross-boundary tone

- [ ] **Hook uses `anchor.currentSyllable` as prefix when buffer is empty**
  Use case: document has `"hoa"` (paste, or user typed then moved cursor back to
  end). User hits `f`. Today Hook buffer is empty → `f` typed literally →
  `"hoaf"`. TSF full-TIP handles this via `EngineController::TryReviveOnType`
  (`src/tsf/EngineController.cpp:105-141`) — read preceding word, seed engine,
  commit via backspace + replace.

  Phase 2 = port `TryReviveOnType` to Hook using the anchor:
  1. In `HookEngine::HandleAlphaKey`, gate on `engine_->Count() == 0` AND
     anchor snapshot has `syllableLen > 0` AND `!isWordStart` AND `isAvailable`.
  2. `IInputEngine::SeedFromText(anchor.currentSyllable, syllableLen)` — API
     already exists (`TryReviveOnType` calls it).
  3. Push current char into engine as normal.
  4. On commit: `SendBackspaces(syllableLen)` + `SendCharEvents(newText)`.

  Risks / gates:
  - **Stale anchor** → backspaces delete wrong chars. Mitigate: re-read anchor
    snapshot immediately before committing and verify `generation` unchanged
    since the read that triggered revive. Abort if changed.
  - **English-word gate**: `TryReviveOnType` uses `IsEnglishWord()` on a
    throwaway engine to skip English words. Hook must do the same or it'll
    revive `"hello" + f → "helló"`.
  - **Commit-char mismatch**: `TryReviveOnType` also handles English protection
    (`ALLOW_ENGLISH_BYPASS`). Hook path needs parity.

  Files: `src/app/system/HookEngine.cpp` (new helper `TryReviveFromAnchor`),
  `tests/HookEngine*` (new tests for paste+tone, click+tone scenarios).

### Phase 3 — Word continuation mid-word click

- [ ] **Hook seeds engine from anchor when user types inside an existing word**
  Use case: `"hu|ong"` with caret between `u` and `o`. User hits `w` expecting
  `"hương"`. Hook today sees empty buffer → literal `w` → `"huwong"`.

  Phase 3 = detect mid-word typing via `anchor.syllableLen > 0 && !isWordStart`
  (same gate as phase 2, but NOT gated on `engine_->Count() == 0` — rather, we
  seed on entry and continue building). Overlaps heavily with phase 2 — likely
  merges into one code path with different commit strategies based on whether
  the cursor is at end of word vs middle.

  Additional risk: detecting cursor position inside the word. TSF gives us
  chars before cursor, not chars after. Hook can't easily see what's after the
  caret without another sync read (expensive + async-locked in TSF).
  Mitigation: phase 3 may require anchor v2 that includes a few chars AFTER
  cursor too. Design pending.

### Shared infra items

- [ ] **Opportunistic prime on `OnSetFocus`** — `src/tsf/ReadonlyContextProvider.cpp:228`
  Currently we wait for first `OnEndEdit` to push an anchor after focus gain.
  First keystroke in a newly-focused app therefore uses `isAvailable=0`
  (cleared by previous focus-out) and falls back to keystroke state. Acceptable
  for phase 1 but phase 2+ will miss revive on the first key after app switch.
  Fix: request a sync read session on focus gain to prime the anchor.

- [ ] **Logging instrumentation** — `src/tsf/ReadonlyContextProvider.cpp:326`
  TODO comment in-place. Wire up once user-facing log infra lands.

---

## 🟡 Earlier Findings — remaining (2026-04-11)

- [ ] `SettingsDialog.cpp:74-75` — IPC handle errors silently discarded with `(void)`. Add logging on failure.
