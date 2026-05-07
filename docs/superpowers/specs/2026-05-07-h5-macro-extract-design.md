# H5 — Macro Extract to `core/MacroCase.h`

**Status:** Design approved 2026-05-07. Awaiting implementation plan.
**Sprint:** 3 / HookEngine refactor backlog (per `docs/REFACTOR_STATUS.md` §C)
**Predecessors:** H2 (dead code), H3 (atomic migration), H7 (history comments) — all merged via PR #139, #140.
**Successors:** H1 (`ProcessKeyDown` decompose) — biggest architectural win remaining.
**Brainstorm:** Inline session 2026-05-07 (Q1 → option A test ambition; Q2 → Cut 1.5 cut point).

---

## 1. Goal

Extract macro-expansion decision logic from `HookEngine::TryExpandMacro` (217 LOC, `HookEngine.cpp:3205-3422`) into a Linux-portable, unit-testable module at `src/core/MacroCase.h`. After H5, `HookEngine.cpp` shrinks by ~185 LOC, gains zero new behavior, and the macro decision logic is fully covered by gtest on Linux without depending on Win32 APIs.

**Non-goals (explicitly deferred):**
- Decomposing `ProcessKeyDown` itself → tracked as H1.
- Changing macro syntax, adding new escape sequences (e.g. `\t`), or extending matching priorities → out of scope.
- Replacing `IOutputInjector` semantics, `sending_` flag contract, or `RecordSynthDispatch()` placement → unchanged.
- Verifying Vietnamese diacritic case mapping (`'ô'→'Ô'`) on Linux → tests use ASCII-only mapper; full Vietnamese path verified manually on Windows (smoke via `tools/run-chaos.ps1`).

H5 is a pure refactor. With the new code in place, all 1,477 existing GTests must pass unchanged, and Windows behavior must be byte-identical to Main `3257758` for every macro test case (manual smoke).

---

## 2. Requirements

### 2.1 Functional

- F1: `HookEngine::TryExpandMacro(wchar_t triggerChar)` keeps its existing signature and call sites unchanged. Behavior is byte-identical to Main.
- F2: `Macro::Plan(...)` (free function in `core/MacroCase.h`) returns a `MacroPlan` struct describing the matched entry, backspace count, post-auto-caps expansion (with `\n` escapes intact), and clipboard-vs-SendInput dispatch decision.
- F3: `Macro::ExpandEscapesForClipboard(...)` (free function) converts `\n` literal escape sequences into `\r\n`. No other escape sequences are processed.
- F4: `Macro::BuildSegments(...)` (free function) returns an ordered list of segments interleaved with `VK_RETURN` markers, applying per-character `CodeTableConverter::ConvertChar` for non-Unicode tables.
- F5: Auto-capitalization rules unchanged from current code: fires only when `autoCapsEnabled` AND match was flexible (`!matchedExact && !matchedViaComposition`) AND stored expansion is all-lowercase. Transform: typed all-upper → expansion all-upper; typed first-upper → expansion first-upper. The `\n` escape's `n` is skipped during all-upper transform.
- F6: Match priorities 1-4 unchanged: full-buffer exact, full-buffer lower, buffer-without-trigger exact, buffer-without-trigger lower, prevComposition+trigger lower, prevComposition lower. Composition matches set `matchedViaComposition = true`.
- F7: Backspace count unchanged across all five branches: composition match (codeTable-aware via `previousEncodedWidths`), cross-commit (`rawMacroBuffer.size()` minus trigger), composition path (codeTable-aware), default (`rawMacroBuffer.size()` minus trigger).
- F8: `Macro::CaseMapper` is an abstract class with `void Upper(wchar_t* buf, size_t n)` and `void Lower(wchar_t* buf, size_t n)`. Production injects `Win32CaseMapper` wrapping `CharUpperBuffW`/`CharLowerBuffW`. Tests inject `AsciiCaseMapper`.

### 2.2 Non-functional

- NF1: `core/MacroCase.h` and `core/MacroCase.cpp` compile and run on Linux (`build-linux`, no Win32 deps). Verified by `NextKeyTests` building+running cleanly.
- NF2: Zero behavior change. All 1,477 existing GTests pass unchanged. Manual Windows smoke (chaos suite) shows zero regression on baseline hosts (Notepad, Notepad++, Discord, GPT, Chrome).
- NF3: Hot-path cost: `Macro::Plan()` does the same N hash lookups as today (≤ 6 in worst case across all priorities), one pass over rawMacroBuffer for letter classification, one or two case-mapper calls. No new allocations beyond the already-existing `expansion` wstring copy.
- NF4: `MacroCase.h` has no Windows includes. Imports `core/config/TypingConfig.h` for `CodeTable` enum and `core/engine/CodeTableConverter.h` (already Linux-portable).
- NF5: `Win32CaseMapper.h` lives at `src/app/system/Win32CaseMapper.h` (next to `HookEngine.cpp`). Header-only; the Win32 dependency stays inside the `app/system/` layer.
- NF6: Test additions: 36 cases in new file `tests/MacroCaseTest.cpp` (~280 LOC). Final test count `1,477 + 36 = 1,513`.
- NF7: `HookEngine.cpp` line count decreases by ≥ 150 LOC. `TryExpandMacro` body shrinks from 217 LOC to ≤ 35 LOC of orchestration.

---

## 3. Design

### 3.1 `core/MacroCase.h` interface

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "core/config/TypingConfig.h"   // CodeTable enum

namespace NextKey::Macro {

class CaseMapper {
public:
    virtual ~CaseMapper() = default;
    virtual void Upper(wchar_t* buf, std::size_t n) const = 0;
    virtual void Lower(wchar_t* buf, std::size_t n) const = 0;
};

struct PlanInputs {
    const std::wstring& rawMacroBuffer;
    const std::wstring& previousComposition;
    const std::vector<uint8_t>& previousEncodedWidths;
    const std::unordered_map<std::wstring, std::wstring>& macroTable;
    bool macroCrossCommit;
    CodeTable currentCodeTable;
    bool autoCapsEnabled;
    wchar_t triggerChar;
    std::size_t clipboardThreshold;     // = kMacroClipboardThreshold (200)
};

struct MacroPlan {
    bool matched{false};
    bool isPartOfMacro{false};          // → ExpandedEatTrigger vs ExpandedPassTrigger
    std::size_t bsCount{0};
    std::wstring expansion;             // post auto-caps; `\n` escapes still literal
    bool useClipboard{false};           // expansion.size() > threshold && Unicode codeTable
};

[[nodiscard]] MacroPlan Plan(const PlanInputs& in, const CaseMapper& mapper);

[[nodiscard]] std::wstring ExpandEscapesForClipboard(std::wstring_view expansion);

struct Segment {
    bool isReturn{false};
    std::wstring text;                  // empty when isReturn == true
};
[[nodiscard]] std::vector<Segment> BuildSegments(std::wstring_view expansion,
                                                 CodeTable codeTable);

} // namespace NextKey::Macro
```

### 3.2 `core/MacroCase.cpp` body

`Plan()` is a near line-for-line port of `TryExpandMacro` lines 3206-3331:
1. Compute `lowerKey` using a file-local helper `LowerCopy(std::wstring) -> std::wstring` defined `static` inside `MacroCase.cpp` (3-line `for-each towlower` loop). The existing `ToLowerAscii` at `app/helpers/AppHelpers.h:65` is in the app layer; we don't promote it to `core/` for H5 — that's a cross-cutting refactor with 16+ callers and is out of scope here. The 3-line duplication is acceptable; if a future cleanup consolidates lowercase helpers into `core/`, this site updates trivially.
2. Run priority 1, 2, 3/4 lookups against `in.macroTable`.
3. If `it == end`: return `{matched: false}`.
4. Compute `bsCount` via the five-branch logic exactly as today.
5. Copy `expansion = it->second`. If auto-caps fires, run the existing all-lower probe (using `mapper.Lower`), then either all-upper transform (skipping `\n`) or first-upper transform (using `mapper.Upper`).
6. Compute `useClipboard = (in.currentCodeTable == CodeTable::Unicode && expansion.size() > in.clipboardThreshold)`.
7. Return populated `MacroPlan`.

`ExpandEscapesForClipboard()` — single-pass loop, identical to `HookEngine.cpp:3344-3352`:
```cpp
std::wstring out;
out.reserve(expansion.size());
for (std::size_t i = 0; i < expansion.size(); ++i) {
    if (expansion[i] == L'\\' && i + 1 < expansion.size() && expansion[i + 1] == L'n') {
        out += L"\r\n";
        ++i;
    } else {
        out += expansion[i];
    }
}
return out;
```

`BuildSegments()` — single-pass loop, mirrors `HookEngine.cpp:3382-3413` but emits `Segment` records instead of calling injector:
```cpp
std::vector<Segment> segments;
Segment cur{};
auto flush = [&]() {
    if (!cur.text.empty()) { segments.push_back(std::move(cur)); cur = {}; }
};
for (std::size_t i = 0; i < expansion.size(); ++i) {
    if (expansion[i] == L'\\' && i + 1 < expansion.size() && expansion[i + 1] == L'n') {
        flush();
        segments.push_back(Segment{true, {}});
        ++i;
        continue;
    }
    if (codeTable != CodeTable::Unicode) {
        auto enc = CodeTableConverter::ConvertChar(expansion[i], codeTable);
        cur.text += enc.units[0];
        if (enc.count == 2) cur.text += enc.units[1];
    } else {
        cur.text += expansion[i];
    }
}
flush();
return segments;
```

### 3.3 `app/system/Win32CaseMapper.h`

```cpp
#pragma once
#include <windows.h>
#include "core/MacroCase.h"

namespace NextKey {

class Win32CaseMapper final : public Macro::CaseMapper {
public:
    void Upper(wchar_t* buf, std::size_t n) const override {
        ::CharUpperBuffW(buf, static_cast<DWORD>(n));
    }
    void Lower(wchar_t* buf, std::size_t n) const override {
        ::CharLowerBuffW(buf, static_cast<DWORD>(n));
    }
};

} // namespace NextKey
```

### 3.4 `HookEngine::TryExpandMacro` post-refactor

```cpp
HookEngine::MacroResult HookEngine::TryExpandMacro(wchar_t triggerChar) {
    Win32CaseMapper mapper;
    Macro::PlanInputs inputs{
        rawMacroBuffer_, previousComposition_, previousEncodedWidths_,
        macroTable_,
        macroCrossCommit_,
        currentCodeTable_,
        autoCapsMacro_.load(std::memory_order_acquire),
        triggerChar,
        kMacroClipboardThreshold,
    };
    auto plan = Macro::Plan(inputs, mapper);
    if (!plan.matched) return MacroResult::NoMatch;

    auto inj = injector_.load(std::memory_order_acquire);

    if (plan.useClipboard) {
        if (plan.bsCount > 0) {
            sending_ = true;
            if (!inj->Replace(plan.bsCount, std::wstring_view{})) {
                HOOK_LOG(L"  TryExpandMacro[clipboard]: BS injector reported partial delivery");
            }
            sending_ = false;
            RecordSynthDispatch();
        }
        auto clipText = Macro::ExpandEscapesForClipboard(plan.expansion);
        ClipboardPaste(clipText);
        HOOK_LOG(L"  TryExpandMacro: clipboard paste %zu chars (raw %zu)",
                 clipText.size(), plan.expansion.size());
    } else {
        sending_ = true;
        std::size_t pendingBs = plan.bsCount;
        for (const auto& s : Macro::BuildSegments(plan.expansion, currentCodeTable_)) {
            if (s.isReturn) {
                if (pendingBs > 0) {
                    if (!inj->Replace(pendingBs, std::wstring_view{})) {
                        HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
                    }
                    pendingBs = 0;
                }
                inj->SendKey(VK_RETURN);
            } else if (!s.text.empty()) {
                if (!inj->Replace(pendingBs, std::wstring_view(s.text))) {
                    HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
                }
                pendingBs = 0;
            }
        }
        if (pendingBs > 0) {
            if (!inj->Replace(pendingBs, std::wstring_view{})) {
                HOOK_LOG(L"  TryExpandMacro[send]: injector reported partial delivery");
            }
        }
        sending_ = false;
        RecordSynthDispatch();
    }

    ClearWordState();
    CancelCommitUndo();
    return plan.isPartOfMacro ? MacroResult::ExpandedEatTrigger
                              : MacroResult::ExpandedPassTrigger;
}
```

Total ~50 LOC including the inputs struct init and dual-branch dispatcher.

### 3.5 CMake updates

- `src/CMakeLists.txt`: add `core/MacroCase.cpp` to the `NextKeyCore` target source list.
- `tests/CMakeLists.txt`: add `MacroCaseTest.cpp` to the `NextKeyTests` source list.
- No new include directories — `core/` is already on the include path.

---

## 4. Data flow

```
HookEngine::TryExpandMacro(triggerChar)
    │
    ├── Build PlanInputs from engine member state
    │
    ├── Macro::Plan(inputs, win32Mapper)
    │       │
    │       ├── ToLowerAscii(rawMacroBuffer)
    │       ├── Priority 1: full-buffer exact / lower
    │       ├── Priority 2: buffer-without-trigger exact / lower
    │       ├── Priority 3/4: prevComposition + trigger / lower
    │       ├── if no match → MacroPlan{matched: false}
    │       ├── Compute bsCount (5-branch)
    │       ├── Auto-caps: if all-lower expansion + flexible match
    │       │       ├── mapper.Lower(probe) → expansionLower
    │       │       ├── Detect allUpper / firstUpper from rawMacroBuffer
    │       │       └── mapper.Upper(span) — skipping \n during all-upper
    │       ├── useClipboard = Unicode && expansion.size() > 200
    │       └── return MacroPlan{matched: true, ...}
    │
    ├── if !plan.matched → return MacroResult::NoMatch
    │
    ├── if plan.useClipboard:
    │       ├── inj->Replace(bsCount, {}) [if bs > 0; sending_/RecordSynthDispatch wrapped]
    │       └── ClipboardPaste(Macro::ExpandEscapesForClipboard(plan.expansion))
    │
    └── else (SendInput path):
            ├── sending_ = true
            ├── for s in Macro::BuildSegments(plan.expansion, codeTable):
            │       ├── if s.isReturn: flush pendingBs as Replace(bs, {}); inj->SendKey(VK_RETURN)
            │       └── else if !s.text.empty(): inj->Replace(pendingBs, s.text); pendingBs = 0
            ├── flush trailing pendingBs as Replace(bs, {})
            ├── sending_ = false
            └── RecordSynthDispatch()

        [unconditional cleanup at end:]
        ClearWordState()
        CancelCommitUndo()
        return ExpandedEatTrigger / ExpandedPassTrigger
```

---

## 5. Error handling

- `Macro::Plan` is pure — no exceptions, single allocation (the `expansion` copy from `it->second`). NoMatch path returns immediately with `matched: false`.
- `Macro::ExpandEscapesForClipboard` and `Macro::BuildSegments` are pure — no exceptions, allocation bounded by output size.
- `CaseMapper::Upper/Lower` are `noexcept` in production (`Win32CaseMapper` wraps Win32 calls that don't throw) and tests (`AsciiCaseMapper`). The interface itself is not declared `noexcept` to keep the contract flexible if a future mapper needs it.
- `inj->Replace(...)` partial-delivery branches in `HookEngine` continue to emit `HOOK_LOG(...)` on `false` return — same six log sites as today, all preserved.
- `IOutputInjector::Replace` `[[nodiscard]]` attribute respected at every call site (compiler enforced under `/WX`).

---

## 6. Testing

### 6.1 New file: `tests/MacroCaseTest.cpp`

Linux gtest, 36 cases / ~280 LOC. Uses `AsciiCaseMapper`:

```cpp
struct AsciiCaseMapper final : NextKey::Macro::CaseMapper {
    void Upper(wchar_t* buf, std::size_t n) const override {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] >= L'a' && buf[i] <= L'z') buf[i] = buf[i] - L'a' + L'A';
    }
    void Lower(wchar_t* buf, std::size_t n) const override {
        for (std::size_t i = 0; i < n; ++i)
            if (buf[i] >= L'A' && buf[i] <= L'Z') buf[i] = buf[i] - L'A' + L'a';
    }
};
```

Coverage map:

| Suite | Cases | What |
|---|---|---|
| `Plan_MatchPriorities` | 6 | Full-buffer exact, full-buffer lower, no-trigger exact, no-trigger lower, prevComp+trigger, prevComp lower |
| `Plan_StoredKeyCaseRule` | 3 | Uppercase-key exact-only enforcement; lowercase-key case-insensitive; mixed user case typed against lowercase key |
| `Plan_BsCount` | 5 | matchedViaComposition Unicode; matchedViaComposition TCVN3 widths; macroCrossCommit with/without trigger; default with previousComposition non-empty (Unicode + TCVN3); default no previousComposition |
| `Plan_AutoCapsDecision` | 6 | autoCapsEnabled OFF → no-op; matchedExact → no-op; matchedViaComposition → no-op; expansion has uppercase → no-op; allUpper rawBuffer → all-upper expansion; firstUpper rawBuffer → first-upper expansion |
| `Plan_AutoCapsEscapePreserved` | 2 | All-upper transform skips `n` in `\n`; all-upper transform of plain `n` (no preceding `\\`) still uppercases |
| `Plan_UseClipboard` | 3 | Unicode + size > threshold → true; Unicode + size ≤ threshold → false; non-Unicode → always false regardless of size |
| `ExpandEscapesForClipboard` | 5 | Empty; no-`\n`; single `\n`; multiple `\n`; trailing/leading `\n`; literal `\\` followed by non-`n` |
| `BuildSegments_Unicode` | 4 | Empty → empty vector; pure text → 1 segment; text-`\n`-text → text/return/text; leading `\n` → return/text; trailing `\n` → text/return |
| `BuildSegments_NonUnicode` | 2 | TCVN3 single-byte chars; TCVN3 two-byte chars (verify both `enc.units` appended) |

### 6.2 Smoke verification

After implementation:
1. `cmake --build build-linux --target NextKeyTests && ./build-linux/tests/NextKeyTests` — expect `1,513 / 1,513` PASS (1,477 existing + 36 new).
2. Windows MSVC build — full target `NexusKey`, expect zero `/WX` warnings.
3. Manual chaos run on Notepad host: `pwsh tools/run-chaos.ps1 -Host Notepad` — expect identical edit-distance verdict to current Main baseline.

### 6.3 Existing tests untouched

- `tests/MacroPrefixTest.cpp` (87 LOC) — covers `MacroEntry` save-path canonicalization. Unrelated to runtime expansion. No changes expected.

---

## 7. Implementation order

1. **Create `core/MacroCase.h`** with the interface from §3.1. Minimal; compiles by itself.
2. **Create `core/MacroCase.cpp`** with `Plan`, `ExpandEscapesForClipboard`, `BuildSegments` ported line-for-line from `HookEngine.cpp:3206-3331` and `:3344-3413`. Add a `static` file-local helper `LowerCopy(std::wstring s) -> std::wstring` (3-line `towlower` loop) — do NOT depend on `app/helpers/AppHelpers.h::ToLowerAscii` (cross-layer).
3. **Add to `src/CMakeLists.txt`** — verify Linux build of `NextKeyCore` succeeds.
4. **Create `app/system/Win32CaseMapper.h`** (header-only).
5. **Refactor `HookEngine::TryExpandMacro`** body per §3.4. Remove the original 217 LOC; replace with the ~50 LOC orchestrator. Verify Windows build.
6. **Create `tests/MacroCaseTest.cpp`** with the 30 cases per §6.1. Verify `1,507 / 1,507` PASS on Linux.
7. **Manual Windows smoke** — chaos run, confirm zero regression.
8. **Commit**: single commit per file pair (header + impl), final commit for HookEngine refactor + tests. Branch `refactor/h5-macro-extract`. PR title: `refactor(H5): extract macro expansion logic to core/MacroCase`.

---

## 8. Open considerations (none blocking)

- `ToLowerAscii` (`app/helpers/AppHelpers.h:65`) is in the app layer and shared by 16+ call sites; `ClassicDialogUtils.h:49` duplicates it. Promoting to `core/` is a separate refactor outside H5 scope. `MacroCase.cpp` instead defines a `static` file-local 3-line `LowerCopy` helper. Documented as accepted micro-duplication; future consolidation into `core/Strings.h` would update both `MacroCase.cpp` and `ClassicDialogUtils.h`.
- `MacroCase.h` compiles against `CodeTableConverter`, which lives in `src/core/engine/`. Layering rule satisfied (engine/ is the strict-zero-Win32 zone; `MacroCase` consuming engine/ symbols is fine — the dependency direction is `app → core/MacroCase → core/engine`, which respects the existing layering).
- Future escape additions (e.g. `\t`) become single-file changes in `MacroCase.cpp`: extend the auto-caps skip set, the `ExpandEscapesForClipboard` switch, and the `BuildSegments` loop. HookEngine code untouched. This was the deciding consideration for Cut 1.5 over Cut 1.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Vietnamese diacritic case-mapping behavior diverges between Win32 and the AsciiCaseMapper used in tests, hiding a bug. | Manual Windows smoke (chaos suite) catches any regression on real Vietnamese text input. Tests cover decision logic; transform verified by integration. |
| `BuildSegments` allocation pattern (one `std::vector<Segment>` + per-segment `std::wstring`) measurably slower than current segment-loop. | Macro expansion is rare (user types macro trigger → fires once). Allocation cost dwarfed by actual SendInput latency. If profiling shows regression, swap `vector<Segment>` for `small_vector` or stack-array. Not worth pre-optimizing. |
| Sequence of `inj->Replace` / `SendKey` calls subtly differs from current code (e.g., flushing trailing bs differently). | §3.4 dispatcher is line-for-line equivalent to current code. Manual chaos run on multiple hosts confirms identical SendInput trace. |
| `ToLowerAscii` symbol collision when promoting (if it's defined in two places). | Step 2 of §7 verifies the source of truth before extracting. Single canonical definition kept. |
