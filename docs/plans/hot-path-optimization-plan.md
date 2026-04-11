# Hot Path Optimization Plan

> Target: v2.1.14
> Date: 2026-04-11
> Source: Benchmark analysis — NexusKey 2131 us/key vs EVKey 1940 us/key (10% gap, default config)
> Updated: Deep analysis confirmed only 3 of 6 original optimizations are valid.

---

## 1. Problem Summary

NexusKey is 10% slower than EVKey on Vietnamese typing with **all features OFF** (default config). The gap comes from architecture overhead, not feature cost.

| Metric | NexusKey | EVKey64 | Gap |
|--------|----------|---------|-----|
| VN avg | 2131 us/key | 1940 us/key | +10% |
| VN p95 | 6453 us | 4880 us | +32% |
| EN avg | 1436 us/key | 1667 us/key | -14% (faster) |

---

## 2. Deep Analysis Results

### What IS causing the gap

| Source | Estimated overhead | Evidence |
|--------|-------------------|----------|
| GetKeyState redundancy (2 extra syscalls) | ~100-200 us/key | HandleAlphaKey L1119-1120 duplicates ProcessKeyDown L832-833 |
| Vector allocation in ReplaceComposition | ~30-50 us/transform | std::vector<INPUT> created per call (L2005-2006) |
| ComposeAll() heap allocation every Peek() | ~30-50 us/key | std::wstring created and returned by value every keystroke |

### What is NOT causing the gap (ruled out by deep analysis)

| Proposed optimization | Why it's invalid |
|----------------------|-----------------|
| Dirty flag `hasTone_` to skip RelocateTone | **Already handled.** Existing early-return at L939: no tone → return immediately. Flag adds zero value. (13 mutation sites for no gain.) |
| Dirty flag `hasHorn_` to skip ApplyAutoUO | **Unsafe.** 36 mutation sites across 2 engines + shared header. Backspace pop_back can't reliably clear flag without rescanning. |
| Skip CheckEnglishBias when Vietnamese | **Already implemented.** EnglishProtection.h L334: `if (bias == Vietnamese) return;`. This runs on every call. No additional skip possible. |
| ComposeAll dirty-flag cache | **Unsafe.** 92+ mutation sites to states_ across 3 files. One missed dirty = stale display. No compiler enforcement. |
| Check ordering / consonant fast path | **Marginal.** Feature checks (quickStart, quickConsonant, etc.) already short-circuit when config=false. PushChar's if-chain is ~6 bool checks = negligible. |
| Config bitmask for feature gating | **Over-engineering.** The bool checks already compile to identical branching. Bitmask adds complexity for zero runtime gain. |

---

## 3. Execution Plan (revised)

Only 3 optimizations remain. All are in the hook/allocation layer, not engine logic.

### Fix 1: Pass cached GetKeyState to HandleAlphaKey

**File:** `src/app/system/HookEngine.cpp`

**Current (L912, L1118-1120):**
```cpp
// ProcessKeyDown L832-833:
const bool cachedShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
const bool cachedCapsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;

// L912:
return HandleAlphaKey(vkCode);

// HandleAlphaKey L1119-1120:
bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;      // REDUNDANT
bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;  // REDUNDANT
```

**After:**
```cpp
// L912:
return HandleAlphaKey(vkCode, cachedShift, cachedCapsLock);

// HandleAlphaKey:
bool HookEngine::HandleAlphaKey(DWORD vkCode, bool shift, bool capsLock) {
    // Use parameters directly, no GetKeyState calls
```

**Risk:** Low. Signature change only. All callers pass cached values.
**Estimated saving:** ~50-100 us/key (2 fewer kernel transitions)

### Fix 2: Stack buffer for ReplaceComposition SendInput events

**File:** `src/app/system/HookEngine.cpp`

**Current (L2005-2006):**
```cpp
std::vector<INPUT> bsEvents;
bsEvents.reserve(backspaceCount * 2 + (needEmpty ? 4 : 0));
std::vector<INPUT> charEvents;
charEvents.reserve(toSend.size() * 2);
```

**After:**
```cpp
// Vietnamese words max ~8 chars, transforms touch 1-3 chars
// Max events: 8 BS + bait(4) + 8 chars = 20 pairs = 40 INPUT structs
static constexpr size_t kMaxEvents = 48;
INPUT bsEvents[kMaxEvents];
size_t bsCount = 0;
INPUT charEvents[kMaxEvents];
size_t charCount = 0;
```

**Risk:** Low. Local change. Need to update DispatchSendInput signature to take array+size instead of vector.
**Estimated saving:** ~15-25 us per transform

### Fix 3: Reuse ComposeAll buffer (member string, no dirty flag)

**File:** `src/core/engine/TelexEngine.h`, `TelexEngine.cpp`, `VniEngine.h`, `VniEngine.cpp`

**Current:**
```cpp
std::wstring TelexEngine::Peek() const {
    return ComposeAll();  // allocates new string every call
}
std::wstring ComposeAll() const {
    std::wstring result;
    result.reserve(states_.size());
    // ...
    return result;
}
```

**After:**
```cpp
// Header: add mutable member
mutable std::wstring composeBuf_;

// Peek: reuse buffer
std::wstring TelexEngine::Peek() const {
    composeBuf_.clear();
    composeBuf_.reserve(states_.size());
    for (const auto& s : states_) {
        wchar_t ch = Compose(s);
        if (ch != 0) composeBuf_ += ch;
    }
    return composeBuf_;  // still returns by value for safety, but internal buffer reuses allocation
}
```

Note: `composeBuf_` reuses its internal allocation across calls (std::wstring::clear() doesn't free memory). The return is still by value (copies the buffer), but the internal heap allocation is amortized after the first call.

**Even better:** Return `const std::wstring&` and let callers copy only when needed. But this changes the API contract — needs audit of all Peek() callers.

**Risk:** Low-medium. Mutable member in const method is standard pattern. No dirty flag, no stale risk — always rebuilds.
**Estimated saving:** ~20-30 us/key (avoids heap alloc/dealloc cycle)

---

## 4. Expected Impact

| Fix | Saving | Cumulative |
|-----|--------|-----------|
| GetKeyState pass-through | ~50-100 us | ~50-100 us |
| Stack buffer SendInput | ~15-25 us | ~65-125 us |
| ComposeAll buffer reuse | ~20-30 us | ~85-155 us |

Gap to close: 191 us. Expected closure: **85-155 us (45-80%).**

Remaining gap (~36-106 us) is irreducible overhead: NexusKey's state-machine architecture (CharState array + tone/modifier tracking) vs EVKey's simpler approach. This is the cost of features like auto-restore, English protection, and escape handling — even when disabled, the code paths exist.

---

## 5. What NOT To Do

- Do NOT add dirty flags (hasTone_, hasHorn_, composeDirty_) — unsafe with 36-92 mutation sites
- Do NOT add config bitmask — bool checks already compile to identical branching
- Do NOT skip CheckEnglishBias at call site — early return at L334 already handles this
- Do NOT add consonant fast path in PushChar — if-chain is ~6 bool checks, negligible
- Do NOT change Peek() to return const& without auditing all 10+ callers
- Do NOT remove U+202F bait without testing 10+ apps

---

## 6. Verification

After each fix:
1. Build + run 1154 tests on Linux
2. Run `benchmark_ime.ps1 -IME "NexusKey"` on Windows
3. Compare with saved EVKey baseline
4. Smoke test: Notepad, Chrome, VSCode
