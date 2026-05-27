# Cay IME Exploration — What's Worth Learning

**Date**: 2026-05-22
**Status**: Notes-only, no action taken
**Trigger**: anh hỏi `https://github.com/tctvn/cay` có gì để học hỏi không

## Repo at a Glance

- **Cay** — Telex IME minimalist, ~22KB exe, GPL-3.0, hook-only (no TSF)
- 6 source files, ~1300 LOC total (vs NexusKey ~30K+)
- No tests, no config, no macros, no app overrides, no multi-method
- Scope **không thể so sánh** với NexusKey. Đây là exercise pick patterns, không phải benchmark architecture.

| File | LOC | Role |
|------|-----|------|
| `src/CayEngine.cpp` | 841 | Telex state machine |
| `src/CayData.cpp` | ~500 | Tone/vowel tables |
| `src/InputInjector.cpp` | 80 | Atomic SendInput batch |
| `src/KeyboardHookManager.cpp` | 119 | LL hook + 256-bit keystate bitmask |
| `src/no_crt.cpp` | 56 | `/NODEFAULTLIB` operator new/delete (size hack) |
| `src/main.cpp` | ~200 | Tray icon + toggle |

## Patterns Worth Considering

### ⭐ 1. Dummy `'a'` injection thay ZWJ (uncertain win)

`InputInjector.cpp:48-66`. Prepend `'a' down + 'a' up` vào `SendInput` batch, dùng 1 extra backspace để xoá:

```cpp
bool useDummy = (backspaceCount > 0);
if (useDummy) {
    FillUnicodeInput(&inputs[idx++], L'a', 0);
    FillUnicodeInput(&inputs[idx++], L'a', KEYEVENTF_KEYUP);
}
int totalBs = backspaceCount + (useDummy ? 1 : 0);
// ... rest of batch
SendInput((UINT)idx, inputs, sizeof(INPUT));
```

**Lý thuyết Cay đưa ra**: ZWJ/ZWSP bị strip bởi GitHub/CodeMirror/strict editors → backspace count desync → ghost char. `'a'` là content thật, mọi editor xử lý đúng → BS count khớp + clear autocomplete selection. SendInput atomic batch giấu flash visually.

**Status**: anh đánh giá lý thuyết **chưa thuyết phục lắm**. Lý do nghi ngờ chính đáng:
- Atomicity của SendInput không 100% guaranteed (game raw-input, terminal raw mode, một số Electron app có message loop riêng).
- `'a'` có thể trigger autocomplete khác → state drift kiểu khác.
- Tác giả Cay đưa ra lý do trong comment nhưng **không có empirical evidence** (no test, no repro screenshot, no profiler trace).
- Cộng đồng IME VN historically dùng ZWJ → có thể có lý do.

**Nếu sau này muốn theo đuổi**: cần repro ghost-char Chrome/Facebook hiện tại với ZWJ baseline trước, rồi A/B với dummy-`'a'` trên cùng repro. Không blanket-swap. Related: `docs/ghost-chars-chrome-investigation.md`.

### ⭐ 2. Centralized rendering — single `UpdateScreen` site

`CayEngine.cpp:830` — kết thúc `OnKeyDown` chỉ có 1 chỗ gọi `UpdateScreen(_text, _textLen)` (trừ VK_BACK đặc biệt). `UpdateScreen` tự tính diff theo common-prefix (line 119-137), chỉ inject delta:

```cpp
int commonPrefixLen = 0;
for (int i = 0; i < minLen; i++) {
    if (_lastOutput[i] == newOutput[i]) commonPrefixLen++;
    else break;
}
int backspacesNeeded = _lastOutputLen - commonPrefixLen;
const wchar_t* textToType = newOutput + commonPrefixLen;
int textToTypeLen = newOutputLen - commonPrefixLen;
```

Áp dụng cho NexusKey: aligns với de-god probe (`docs/plans/2026-05-22-hookengine-degod-probe.md`). NexusKey hiện có nhiều mutation site phân tán. Cay validates idea "single rendering site khả thi cho IME Telex" — nhưng caveat: Cay scope nhỏ hơn nhiều (no TSF anchor, no SharedState IPC, no FSM stage).

### ⭐ 3. MSVC size flags cho DLL

`CMakeLists.txt:31-58`:
- `/O1 /Os /Gy` — optimize size + function-level linking
- `/OPT:REF /OPT:ICF` — strip unreferenced + merge identical COMDATs
- `/GS- /EHs-c- /GR-` — no buffer cookie / no exceptions / no RTTI

EXE NexusKey không cần (Sciter MB-scale). **DLL injection target `VKeyTSF.dll` có thể giảm size** nếu áp dụng. Cần verify TSF code không dùng exceptions/RTTI trước khi apply `/EHs-c- /GR-`.

### 🔸 4. Pointer-walk syllable validator (reference)

`CayEngine.cpp:179-297` `IsCompleteSyllable()`: grammar parser cứng `initial + nucleus + final + tail` với tables dài-trước:

- Initials: `ngh, gh, gi, ng, nh, ph, qu, th, tr, ch, kh, đ, b, c, d, ...`
- Nuclei: `iêu, yêu, ươu, uôi, ươi, oai, oay, uya, uyê, ...` (3-vowel first)
- Finals: `ng, nh, ch, c, m, n, p, t`
- Tails: `i, y, o, u`

Đặc biệt: handle "gi" rollback nếu sau gi không phải vowel (`gì`, `gìn` → `i` là nucleus).

So với NexusKey `IsHardEnglishToneContext` (V+C+V heuristic): Cay **rigorous hơn về grammar**, có thể dùng làm **confidence signal phụ** cho bypass decision. **CẢNH BÁO**:
- Cay bypass aggressive, loanwords ("menu", "computer") có thể misfire — anh có thể đã có lý do giữ NexusKey conservative.
- Tables Cay không cover phương ngữ / proper noun / borrowed terms.
- Không port nguyên bản, dùng làm reference grammar.

### 🔸 5. 256-bit keystate bitmask debounce

`KeyboardHookManager.cpp:33-46` + `:77-79`:

```cpp
DWORD64 _keyState[4];  // 256 bits

if (isDown && s_instance->TestKeyBit(vk)) {
    return CallNextHookEx(...);   // already down, skip
}
```

Cheap robustness add nếu NexusKey hook chưa debounce. Verify trước, có thể NexusKey đã handle qua mechanism khác.

### 🔸 6. Word recall semantics (Space → BS)

`CayEngine.cpp:687-706`. Sau commit (Space/Tab/Return), `SaveState()` lưu `_savedBuffer/_savedText/_savedToneIndex`. Backspace ngay sau khi buffer empty + `_canRestore=true` → restore raw buffer, **KHÔNG set `e.handled=true`** → để OS tự xoá ký tự Space.

```cpp
// DO NOT set e.handled = true here. We let the OS physically delete the Space character.
return;
```

Comparable với NexusKey commit-undo flow (`project_commit_undo_synth_guard_exemption.md`). Cay đơn giản hơn vì không có synth-guard race. Pattern "let OS handle the trigger key" có thể đáng tham khảo nếu commit-undo NexusKey còn edge case.

## What NOT to Copy

| Pattern | Lý do bỏ |
|---------|----------|
| `no_crt.cpp` / NODEFAULTLIB → 22KB | Vô nghĩa cho Sciter app (MB-scale anyway) |
| Stateless re-derive toàn bộ trên BS | Scope khả thi vì Cay không có TSF anchor / SharedState IPC / config snapshot. Architecture diff quá lớn |
| Tone tables / Telex tables | NexusKey đã có đầy đủ + edge cases years |
| Zero tests | NexusKey có 1900+. Không envy |
| Aggressive `CommitWord()` trên Space/Tab/punct | NexusKey có macro / commit-undo / app overrides — không thể naive commit |
| Single `_buffer[]` flat raw history | NexusKey state model phức tạp hơn vì features. De-god probe đang reduce, không phải replace |

## Action Items (deferred)

- [ ] **Bỏ ngỏ**: Spike dummy-`'a'` vs ZWJ — chỉ làm khi reproduce ghost-char Chrome/Facebook concrete (chưa convincing đủ để spend cycle giờ).
- [ ] **Bỏ ngỏ**: Đo size `VKeyTSF.dll` trước/sau `/OPT:ICF /OPT:REF /Gy` — nếu >10% giảm thì commit.
- [ ] **Tham khảo**: Khi de-god probe Phase 2+ cần shape target rendering, nhìn lại `UpdateScreen()` Cay làm reference.

## Conclusion

Cay là **good study reference cho minimalist IME**, không phải template. Patterns đáng học nằm ở **build flags, atomic batch shape, single rendering site discipline**. Theory chính (dummy `'a'`) cần empirical validation trước khi áp dụng. Architecture differences quá lớn để compare 1:1.

**Bottom line**: file này tồn tại để khi nào touching ghost-char investigation hoặc DLL size optimization, không phải google lại.
