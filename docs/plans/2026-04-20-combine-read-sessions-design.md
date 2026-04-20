# Combine Read Sessions for Revive + Auto-cap

**Date:** 2026-04-20  
**Status:** Approved  
**Impact:** Performance — reduce TSF edit sessions from 3 to 2 on auto-cap path

## Problem

`HandleKey` A-Z path fires up to 3 sync edit sessions per keystroke when engine buffer is empty:

1. `ReadPrecedingWordEditSession` in `TryReviveOnType` — reads word before cursor
2. `ReadPrecedingCharsEditSession` in `ShouldAutoCapitalize` — reads chars before cursor  
3. `StartCompositionEditSession` — starts composition

Sessions (1) and (2) read overlapping text from the same cursor position.

## Solution

Replace sessions (1) and (2) with single `InspectPrecedingTextEditSession` that:
- Reads 64 chars before cursor (once)
- Extracts Vietnamese word (for revive)
- Checks auto-cap rules (newline, sentence-ending punct, doc start)
- Returns both results to caller

## Session counts after fix

| Path | Before | After |
|------|--------|-------|
| Revive succeeds | 2 | 2 |
| Auto-cap (revive fails) | 3 | 2 |
| Normal typing | 2 | 2 |

## Implementation

### New class: `InspectPrecedingTextEditSession`

```cpp
// CompositionEditSession.h
class InspectPrecedingTextEditSession : public EditSession {
public:
    explicit InspectPrecedingTextEditSession(ITfContext* pContext);
    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override;
    
    [[nodiscard]] const std::wstring& Word() const noexcept;
    [[nodiscard]] ITfRange* DetachWordRange();
    [[nodiscard]] bool ShouldAutoCap() const noexcept;
    [[nodiscard]] bool AtDocStart() const noexcept;

private:
    std::wstring word_;
    CComPtr<ITfRange> wordRange_;
    bool shouldAutoCap_ = false;
    bool atDocStart_ = false;
};
```

### DoEditSession logic

1. Get selection, verify empty (caret not range)
2. Clone and shift start back 64 chars
3. Read text into buffer
4. Walk backward extracting Vietnamese word (using `IsVietnameseLetter`)
5. Check auto-cap: skip whitespace, check for `\n`, `\r`, `.`, `?`, `!`
6. Store results in member fields

### EngineController changes

- Remove `TryReviveOnType()` method
- Remove `ShouldAutoCapitalize()` method  
- Inline combined logic in `HandleKey` using new session
- Keep `ReviveAndTypeEditSession` for the actual revive action

## Files changed

- `src/tsf/CompositionEditSession.h` — add `InspectPrecedingTextEditSession`
- `src/tsf/EngineController.h` — remove two method declarations
- `src/tsf/EngineController.cpp` — replace methods with inline logic

## Testing

Manual test in Chrome/Edge:
1. Type "hoa" → move cursor to end → type "f" → should produce "hoá" (revive)
2. Type "hello. " → type "w" → should produce "W" (auto-cap)
3. Empty doc → type "a" → should produce "A" (auto-cap at doc start)
