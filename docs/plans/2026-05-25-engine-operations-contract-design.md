# Engine Operations Contract — Design Notes

**Date:** 2026-05-25
**Status:** Brainstorm output, post first-review revision (awaiting follow-up)
**Author:** PhatMT (with Claude pair)
**Trigger:** Sau khi land `c6369dd` (adjacent circumflex fix cho `vijeet`/`ngufoon`), anh nhận xét: *"engine của chúng ta thật sự chưa ổn, mặc dù mình có rất nhiều tài liệu về luật gõ tiếng Việt"*.

> **Note for reviewers:** Commit `c6369dd` is currently **local only on
> `feat/architecture-review-v3.1` (ahead 1, not pushed yet)**. If `git show
> c6369dd` fails on your tree, that's why. Push pending; this doc + the fix
> ship together once review notes are folded in.

---

## Revision history

- **2026-05-25 v1:** Initial brainstorm output. Proposed universal B4 guard
  ("pre-state Valid → reject").
- **2026-05-25 v2 (this rev):** Reviewer ([teammate]) flagged v1's B4 as too
  broad — Vietnamese late w/breve/horn on Valid syllables (`cuarw`, `hoaw`,
  `muaw`, VNI `cua7`) is documented legitimate behavior, not typo. Reframed
  B4 as **per-modifier acceptance policy**. Added probe tests confirming
  current code already handles these correctly (`HandleHornW` separate path
  from `HandleAdjacentCircumflex`; `c6369dd` doesn't regress them).

---

## 1. Bối cảnh (cho người đọc cold)

### 1.1 Hai bug gần đây cùng pattern

**`ad09f15` (2026-05-23) — `susata → suất`**
Free-marking circumflex (`HandleAdjacentCircumflex` nhánh free-marking, `TypingEngine.cpp:984-1031`) gọi `RelocateToneToTarget()` sau khi `mod = newMod`, nhưng `WouldBeValidSyllable` chỉ speculate modifier change, không speculate tone-relocate. Validator nhìn state "tone bị bỏ lại" → kêu Invalid → reject promotion hợp lệ. Fix: thêm opt-in `speculateRelocateTone` cho free-marking callsite.

**`c6369dd` (2026-05-25) — `vijeet → việt`, `ngufoon → nguồn`**
Cùng pattern bug, nhưng ở **nhánh adjacent** của cùng hàm. Adjacent branch validate mod-only nhưng runtime đặt tone tại chỗ "không relocate". Khi user gõ tone TRƯỚC modifier (vd `v + i + j` rồi `e + e`), validator nhìn `vịê` thấy tone trên `i` mà luật `iê` yêu cầu trên `ê` → Invalid → reject → bug.

Fix: branch theo pre-state syllable.
- `ValidPrefix` (đang construct) → speculate WITH relocate, apply WITH `RelocateToneToTarget()`.
- `Valid` (syllable đã complete như `của`) → giữ mod-only path. Bảo vệ typo guard cho case `cuara → cua` + `a` thừa.
- `Invalid` → reject.

### 1.2 Phát hiện trong review

VNI có cùng pattern bug ở `ProcessVniVowelModifier` (`TypingEngine.cpp:1953`) — đã thêm vào `docs/TODO.md`. Horn paths đã có TODO từ 2026-05-23.

### 1.3 Câu hỏi rộng hơn

Tại sao engine vẫn lộ structural bug mặc dù:
- Đã có lookup table phonotactics (`kDiphthongClassic/Modern`, `IsTriphthong`, `IsBareSmartTriphthongTail`)
- Đã có invariant rõ (commit ad09f15: "speculate path phải mirror apply path")
- 2037 unit tests đang pass

---

## 2. Reframe vấn đề (đã align với anh trong brainstorm)

### 2.1 Data layer đã ổn

Single-source-of-truth ở **DATA** đã được tuân thủ:
- `kDiphthongClassic[6][6]` / `kDiphthongModern[6][6]` (`VietnameseTables.h:200, 211`)
- `IsTriphthong(v1, v2, v3)`, `IsBareSmartTriphthongTail(...)`
- `Phonology::ValidateSyllableState(states, count, allowZwjf)` đọc đúng các bảng này.

Khi anh đọc `kDiphthongModern[u][o] = 2`, đó là source of truth. Validator dùng nó. Bug `nguồn` Classic không phải vì table sai — table đúng.

### 2.2 Operations layer là chỗ phân mảnh

```
TABLE  (truth, OK)
  ↓
PRIMITIVES  (ValidateSyllableState, FindToneTarget, RelocateToneToTarget,
             RelocateToneToHornVowel, WouldBeValidSyllable, ShouldRejectModifier,
             IsToneRelocBlockedByP4)  — riêng lẻ đúng
  ↓
CALLSITES   (HandleAdjacentCircumflex, HandleHornW, HandleVniCircumflex,
             ProcessVniVowelModifier, ...)
             — TỰ COMPOSE primitives, dễ sai
  ↓
OUTPUT
```

Bug `vijeet`/`ngufoon` không phải table sai. Là **caller compose sai**: `HandleAdjacentCircumflex` gọi validator mod-only nhưng apply path có relocate. ad09f15 fix free-marking branch nhưng adjacent branch không bị force-update theo → divergence âm thầm 2 ngày.

Mỗi callsite đang tự quyết "gọi validator nào, apply gì, relocate hay không" — invariant `speculate-mirrors-apply` chỉ tồn tại trong comment + đầu người review.

→ **Source of truth ở DATA đã có; nhưng OPERATIONS chưa có source of truth.**

---

## 3. Đề xuất: 4-step modifier contract

Đề xuất xuất phát từ luật tiếng Việt thuần:

> Đặt dấu thanh → đặt dấu mũ → đặt lại dấu thanh nếu tổ hợp nguyên âm thay đổi → guard rule TV (không tự phá syllable đã complete).

### 3.1 Flow chính

> **Revision 2026-05-25 (post-review):** B4 originally drafted as universal
> "Valid → reject" rule. Reviewer flagged this as too broad — Vietnamese
> phonotactics explicitly supports late horn/breve modifiers on Valid
> syllables (`cuarw → cửa`, `hoaw → hoă`, `muaw → mưa`; see
> `category-10-w-modifier-priority-7-levels.md:10`). B4 must be a
> **per-modifier acceptance policy**, not a syllable-state-level rule.

```
KEY ĐẾN
  │
  ├─ Tone key (s/f/r/x/j/z)
  │   └─ B1. Place tone theo phonotactics (current FindToneTarget)
  │
  ├─ Modifier key (a/e/o/w/d/[/])
  │   ├─ B4-pre. GUARD per-modifier acceptance policy:
  │   │         (see 3.2 — different modifiers have different rules
  │   │          about whether Valid pre-state can be mutated)
  │   ├─ B2. Apply modifier lên vowel target
  │   └─ B3. Re-place tone NẾU vowel cluster thay đổi
  │          (chỉ relocate khi có rule diphthong != 0;
  │           giữ logic IsToneRelocBlockedByP4)
  │
  └─ Vowel/Consonant thường
      └─ Push + B3 (re-place tone nếu cluster thay đổi)
```

### 3.2 Per-modifier acceptance policy (B4)

Each modifier declares whether it can mutate a Valid (complete) syllable.
The decision is **modifier-specific**, not universal.

| Modifier | Pre-state Valid → mutate allowed? | Rationale | Example |
|---|---|---|---|
| **Circumflex** `aa/ee/oo` | ❌ Reject | Late adjacent circumflex on a complete syllable is almost always a typo. Typo guard from `ad09f15`. | `của` + `a` → reject, keep `của` + literal `a` |
| **Horn** `w` (priorities P1-P6) | ✅ Accept | Vietnamese supports late horn construction: `cua` → `r` (tone) → `w` (horn) → `cửa`. Documented in `vietnamese-phonology-spec-distillate.md:103`. | `cuarw` → `cửa`, `muaw` → `mưa` |
| **Breve** `aw` (P3 oa→oă, P7 standalone) | ✅ Accept | Late breve on Valid `hoa` → `hoă` is legitimate (WP-04 in category-10 distillate). | `hoaw` → `hoă`, `toaw` → `toă` |
| **Stroke** `dd` (d→đ) | ⚠️ Audit pending | Doesn't mutate vowel cluster, so B3 doesn't apply. Whether late `dd` on Valid is legitimate or typo needs probe. | `addd` → ? |
| **Bracket** `[`/`]` (VNI shortcut for `ơ`/`ư`) | ⚠️ Audit pending | Same family as Horn; likely Accept by analogy. Needs probe. | `cuar]` → ? |
| **VNI tone keys** `1-5` | n/a | Tone keys go through B1, not B4. | — |
| **VNI vowel modifiers** `6/7/8` | Mirror Telex counterparts | `6` = circumflex (reject Valid like aa/ee/oo), `7` = horn (accept Valid like w), `8` = breve (accept Valid like aw). | `cua7` → `cử`/`cửa` (accept Valid) |

**Pre-state × post-state matrix (for modifiers that REJECT Valid):**

| Pre-state | Post-state | Decision | Example |
|---|---|---|---|
| `Valid` | Valid | **Reject** (typo guard) | `của`+a → reject |
| `Valid` | Invalid | Reject | (rare; modifier broke syllable) |
| `ValidPrefix` | Valid | **Accept** (promote) | `vịe`+e → `việ` |
| `ValidPrefix` | ValidPrefix | Accept (still constructing) | `chuye`+e → `chuyê` |
| `ValidPrefix` | Invalid | Reject | (modifier doesn't help) |
| `Invalid` | * | Reject | (not a syllable anyway) |

**Pre-state × post-state matrix (for modifiers that ACCEPT Valid):**

| Pre-state | Post-state | Decision | Example |
|---|---|---|---|
| `Valid` | Valid | Accept | `cua`+w → `cưa` |
| `Valid` | ValidPrefix | Accept | `hoa`+w → `hoă` (becomes prefix for `hoăc`, `hoăn`) |
| `Valid` | Invalid | Reject | (would break syllable) |
| `ValidPrefix` | * | Accept if non-Invalid | (standard mid-construction) |
| `Invalid` | * | Reject | |

### 3.3 Verify mental với 7 case (3 mới sau review)

| Input | Modifier | Flow | Result | Đúng? |
|---|---|---|---|---|
| `vijeet` | Circumflex (reject-Valid) | `vịe` ValidPrefix → accept | `việt` | ✅ |
| `ngufoon` | Circumflex (reject-Valid) | `ngùo` ValidPrefix → accept | `nguồn` | ✅ |
| `của`+a (typo) | Circumflex (reject-Valid) | `của` Valid → REJECT | `cua` + literal `a` | ✅ typo guard giữ |
| `chuyeenj` | Circumflex (reject-Valid) | `chuye` ValidPrefix → accept | `chuyện` | ✅ |
| `suất` (`susata`) | Circumflex free-marking | ValidPrefix → speculate-relocate | `suất` | ✅ ad09f15 |
| **`cuarw`** | Horn (accept-Valid) | `của` Valid → accept, P5 → tone relocate u→ư | `cửa` | ✅ test ran 2026-05-25 |
| **`hoaw`** | Breve (accept-Valid) | `hoa` Valid → accept, P3 → breve trên a | `hoă` | ✅ test ran 2026-05-25 |

### 3.4 Cảnh báo — gates phải đứng TRƯỚC B4

Reviewer confirmed current gate ordering in `HandleModifierAction`
(`TypingEngine.cpp:486`) is correct. Gates run before the modifier
contract:

1. **English bias** (`HasInvalidAdjacentVowelPair`, `IsHardEnglishStart/End`)
2. **Exclusion list** (`WouldModifierKeyMatchExclusion`)
3. **Escape state** (`escape_`, `toneEscaped_`, `dModifierEscaped_`)
4. **Spell-check mode** (`spellCheckEnabled`, `effectiveSpellCheck`)

These cannot be folded into B4 — they answer "should the modifier fire
at all?" while B4 answers "given that it fires, does the pre-state allow
mutation?". Different concerns, different layers.

---

## 4. Audit callsites

| Callsite | B2 apply | B3 relocate | B4 pre-state guard | Status |
|---|---|---|---|---|
| `HandleAdjacentCircumflex` adjacent (`TypingEngine.cpp:905`) | ✓ | ✓ branched | ✓ ValidPrefix branch | ✅ c6369dd |
| `HandleAdjacentCircumflex` free-marking (`TypingEngine.cpp:984`) | ✓ | ✓ speculate-relocate | ✓ via ShouldRejectModifier | ✅ ad09f15 |
| `HandleHornW` P7 Breve standalone-a (`TypingEngine.cpp:1285`) | ✓ | none | ✓ mod-only (matches no-relocate) | ✅ compliant |
| `HandleHornW` P5/P6 horn check (`TypingEngine.cpp:1261, 1274`) | ✓ | `RelocateToneToHornVowel` | ❌ mod-only mismatch | ❌ TODO Horn 2026-05-23 |
| `ProcessVniVowelModifier` (`TypingEngine.cpp:1953`) | ✓ | `RelocateToneToTarget` | ❌ mod-only mismatch | ❌ TODO VNI 2026-05-25 |
| `HandleHornW` P1-P4 (`TypingEngine.cpp:1149, 1171, 1178, 1185`) | ✓ | `RelocateToneToHornVowel` | ⚠️ no validate at all | ❓ NEEDS AUDIT |
| `HandleVniHorn` (`TypingEngine.cpp:1873, 1879, 1885, 1914`) | ✓ | `RelocateToneToTarget` | ⚠️ unclear | ❓ NEEDS AUDIT |
| `HandleStrokeD` (`TypingEngine.cpp:1327`, d→đ) | ✓ | no cluster change | maybe gated | ❓ Low priority (no vowel impact) |
| `FinalizeRegularChar` (`TypingEngine.cpp:452`) | n/a | ✓ B3 mechanism after every char | n/a | ✅ mechanism OK |

**Tổng kết:**
- ✅ Compliant: 3 callsites
- ❌ Known violations: 2 (đã tracked trong `docs/TODO.md`)
- ❓ Cần audit: 3-4

---

## 5. Migration plan — 7 PR incremental

| # | PR title | Scope | Risk | Stop signal |
|---|---|---|---|---|
| **1** | `fix(engine): VNI ProcessVniVowelModifier ValidPrefix branch` | Apply pattern c6369dd cho L1953. Add probe test mirroring `ToneMidSmartAccentTest`. | **Low** — pattern đã proven | Test fail bất ngờ → escalate |
| **2** | `fix(engine): Horn P5/P6 speculate-relocate parity` | Extend `WouldBeValidSyllable` via `enum RelocationKind { None, TargetTone, HornVowel }` (TODO 2026-05-23 sketched this — reviewer reinforced: **don't extend the current bool flag**). Apply ở L1261, L1274. | **Medium** — relocate function khác | Concrete VN repro chưa có → docs/TODO note "no repro yet", dừng |
| **3** | `refactor(engine): audit HornW P1-P4 callsites` | Read L1149-1185, document tại sao không validate. **Reviewer note:** "some 'no validate' paths are probably intentional late-modifier flows" — `cuarw`/`hoaw`/`muaw` proven legitimate, so audit with probes, don't guard blindly. Add validate ONLY if probe surfaces a wrong-accept. | **Low** | Nếu no-validate là intentional (gated upstream) → ghi comment + dừng |
| **4** | `refactor(engine): audit HandleVniHorn callsites` | Tương tự #3 cho L1847+. | **Low** | Same |
| **5** | `docs(CODING_RULES): 4-step modifier contract` | Document B1-B2-B3-B4 contract + invariant "speculate mirrors apply". Reference PR #1-#4 làm examples. | **Zero** | n/a |
| **6** | `refactor(engine): extract TryApplyModifier helper` (**optional**) | Sau khi 4-5 callsite cùng pattern lộ ra → extract common helper. | **High** | Callsite shape khác nhau quá → skip, để CODING_RULES (#5) làm guardrail |
| **7** | `chore(tools): lint script for speculate-apply violations` (**optional**) | grep script scan callsites match pattern `RelocateToneTo*` mà không có `speculateRelocateTone=true`. CI integration. | **Zero** | n/a |

### 5.1 Natural checkpoints

- **Sau PR 1-2:** Tất cả known bug đóng. Engine usable. Có thể stop ngay.
- **Sau PR 3-4:** Hoàn tất audit. Nếu phát hiện thêm bug → loop về PR 1-style fix.
- **Sau PR 5:** Đã có guardrail document cho code mới. Stop tại đây cũng OK.
- **PR 6-7:** Chỉ làm nếu thực sự thấy value (pattern repeat-able + tooling cost-effective).

### 5.2 Effort estimate

- PR 1-2: ~1 ngày work mỗi PR (đã có pattern, có test fixture)
- PR 3-4: ~1-2 ngày (audit + có thể test mới)
- PR 5: 0.5 ngày (doc only)
- PR 6-7: 1-3 ngày (optional, scope unclear)

**Tổng:** ~4-6 ngày cho PR 1-5. PR 6-7 separate.

---

## 6. Anti-pattern cần tránh

- ❌ **Boil-the-ocean**: refactor toàn engine 1 PR. Mỗi PR 1 file mục tiêu.
- ❌ **Promise defer mà không verify**: "Phase X sẽ fix Y" mà không probe trước. Memory `feedback_defer_with_promise` cảnh báo.
- ❌ **Restructure rule layer**: Memory `typing_engine_stability` ghi rõ — rule layer (tone/mark/P1-P8 placement logic) đã stable. Refactor chỉ ở operations layer (dispatch/escape/context).
- ❌ **Document mà không enforce**: PR 5 (CODING_RULES) có giá trị thấp nếu không kèm review checklist hoặc lint (PR 7).
- ❌ **Quên gate khác**: English bias, exclusion, escape, spell-check phải preserve. Contract không bypass gates.

---

## 7. Open questions cho teammate review

> v2: Some questions resolved by reviewer. Remaining + new questions below.

### Resolved by reviewer

- ~~"Scope B4 guard universal or per-modifier?"~~ → **Per-modifier acceptance policy** (Section 3.2 revised).
- ~~"Gate ordering"~~ → Confirmed: English/escape/spell/exclusion run **before** modifier contract, in `HandleModifierAction` (`TypingEngine.cpp:486`). Stays as-is.
- ~~"Of`của + a` design decision"~~ → Specifically a typo-guard for **adjacent circumflex aa**, not a syllable-state rule. Stays.

### Still open

1. **Stroke `dd` policy (B4 column):** Late `dd` on Valid syllable — typo or legitimate? Probe needed. Examples to test:
   - `ad` + `d` → `ađ` legitimate? Or reject?
   - Suggested probe: `cad` (no syllable in VN), `dad` → `đađ`?

2. **Bracket `[`/`]` policy:** VNI shortcut for `ơ`/`ư`. Likely "accept Valid" by analogy with Horn (`cuar[` → `cửa`?). Probe needed.

3. **HornW P1-P4 audit findings** (after PR 3 probes): If probes show "no validate" is intentional (gates upstream sufficient), no code change — just comment. If probes find a bug, PR 3 becomes a fix PR.

4. **VNI parity** (`vi5e6t → việt`): Reviewer confirmed this is the canonical probe for PR 1. Are there VNI-specific edge cases (`6/7/8` interaction with `1-5` tone keys) the Telex bug pattern doesn't surface?

5. **TryApplyModifier helper (PR #6) — even more questionable now:** With per-modifier policy, the helper would need to encode "accept-Valid" vs "reject-Valid" per modifier. Effort vs benefit ratio worse than v1. Reviewer's "rename to ModifierProposal" framing suggests the value is in the **contract shape**, not the helper extraction. Reconsider scope of PR #6 — maybe drop entirely in favor of CODING_RULES (PR #5).

6. **Lint script (PR #7):** Compile-time enforcement vs grep — depends on whether contract is encoded as a class (compile-time) or convention (runtime grep). Per the renaming above, lean toward convention + CODING_RULES + manual review.

### New from reviewer

7. **`ModifierProposal` shape:** Reviewer suggests "each callsite/proposal declares relocation kind plus whether Valid → mutation is allowed." Could be a struct passed to a unified helper, OR docstring contract per callsite. Which form fits NexusKey style?

8. **Tests coverage gap:** Reviewer noted "existing targeted regression filter 14 tests passed but doesn't cover `vi5e6t` or `vijeet`/`ngufoon` early-tone adjacent". `vijeet`/`ngufoon` now covered by `ToneMidSmartAccentTest` (added in c6369dd). `vi5e6t` is the explicit PR 1 probe target.

---

## 8. References

- Bug fix: commit `ad09f15` (free-marking circumflex), `c6369dd` (adjacent circumflex)
- TODO follow-ups: `docs/TODO.md` sections "WouldBeValidSyllable speculation parity — VNI vowel modifier" + "WouldBeValidSyllable speculation parity for Horn paths"
- Tables: `src/core/engine/VietnameseTables.h:200, 211`
- Validator: `src/core/engine/PhonotacticsValidator.h`
- Test fixtures: `tests/TelexEngineTest.cpp::ToneMidSmartAccentTest`, `tests/PhonotacticsValidatorTest.cpp::PhonotacticsValidatorVCPairTest::AdjacentHeuristic_*`
- Late-modifier tests confirming horn/breve still accept Valid (added 2026-05-25 v2): `ToneMidSmartAccentTest::Cuarw_LateHornAfterValid`, `Hoaw_LateBreveAfterValid`, `Muaw_LateHornAfterValidUaPair`
- Phonology distillates referenced by reviewer: `docs/vietnamese-phonology-spec-distillate.md:103`, `docs/category-10-w-modifier-priority-7-levels.md:10`
- Memory: `project_typing_engine_stability`, `feedback_defer_with_promise`, `feedback_probe_before_theorize`

---

## 9. What this doc is NOT

- ❌ NOT a directive to refactor everything. Migration plan is optional per-PR.
- ❌ NOT a critique of past work. Engine is functional; this addresses **structural risk** not correctness.
- ❌ NOT a complete spec. Open questions (Section 7) need team input before implementation.
- ❌ NOT urgent. Bug đã đóng. Đây là tech-debt awareness, ưu tiên có thể defer.
