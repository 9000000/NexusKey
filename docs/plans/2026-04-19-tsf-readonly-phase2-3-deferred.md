# TSF Readonly Context — Phase 2 / 3 / shared infra (DEFERRED design)

**Status**: deferred forward-looking design. No active user complaints driving Phase 2/3 work as of 2026-05-25.

**Phase 1** (auto-cap via `HookContextAnchor`) shipped in commits `89d1add..b0bbb09`. Design doc: [`2026-04-19-tsf-readonly-context-phase1-design.md`](./2026-04-19-tsf-readonly-context-phase1-design.md). Phase 1 infra (`anchor.currentSyllable[16]`, seqlock helpers) is in place and ready for Phase 2 to consume.

**Trigger to revisit**: ≥1 user report of either —
- "Paste Vietnamese word + tone key on next char fails to apply tone" (Phase 2 scenario)
- "Click middle of an existing word + modifier key doesn't continue composition" (Phase 3 scenario)

Until a real report lands, do NOT pre-implement.

---

## Phase 2 — Cross-boundary tone

**Use case**: document has `"hoa"` (paste, or user typed then moved cursor back to end). User hits `f`. Today Hook buffer is empty → `f` typed literally → `"hoaf"`. TSF full-TIP handles this via `EngineController::TryReviveOnType` (`src/tsf/EngineController.cpp:105-141`) — read preceding word, seed engine, commit via backspace + replace.

Phase 2 = port `TryReviveOnType` to Hook using the anchor:

1. In `HookEngine::HandleAlphaKey`, gate on `engine_->Count() == 0` AND anchor snapshot has `syllableLen > 0` AND `!isWordStart` AND `isAvailable`.
2. `IInputEngine::SeedFromText(anchor.currentSyllable, syllableLen)` — API already exists (`TryReviveOnType` calls it).
3. Push current char into engine as normal.
4. On commit: `SendBackspaces(syllableLen)` + `SendCharEvents(newText)`.

### Risks / gates

- **Stale anchor** → backspaces delete wrong chars. Mitigate: re-read anchor snapshot immediately before committing and verify `generation` unchanged since the read that triggered revive. Abort if changed.
- **English-word gate**: `TryReviveOnType` uses `IsEnglishWord()` on a throwaway engine to skip English words. Hook must do the same or it'll revive `"hello" + f → "helló"`.
- **Commit-char mismatch**: `TryReviveOnType` also handles English protection (`ALLOW_ENGLISH_BYPASS`). Hook path needs parity.

### Files to touch

- `src/app/system/HookEngine.cpp` — new helper `TryReviveFromAnchor`
- `tests/HookEngine*` — new tests for paste+tone, click+tone scenarios

---

## Phase 3 — Word continuation mid-word click

**Use case**: `"hu|ong"` with caret between `u` and `o`. User hits `w` expecting `"hương"`. Hook today sees empty buffer → literal `w` → `"huwong"`.

Phase 3 = detect mid-word typing via `anchor.syllableLen > 0 && !isWordStart` (same gate as Phase 2, but NOT gated on `engine_->Count() == 0` — rather, we seed on entry and continue building). Overlaps heavily with Phase 2 — likely merges into one code path with different commit strategies based on whether the cursor is at end of word vs middle.

**Additional risk**: detecting cursor position inside the word. TSF gives us chars before cursor, not chars after. Hook can't easily see what's after the caret without another sync read (expensive + async-locked in TSF). Mitigation: Phase 3 may require **anchor v2** that includes a few chars AFTER cursor too. Design pending until trigger.

---

## Shared infra items

### Opportunistic prime on `OnSetFocus`

`src/tsf/ReadonlyContextProvider.cpp:228` — currently waits for first `OnEndEdit` to push an anchor after focus gain. First keystroke in a newly-focused app therefore uses `isAvailable=0` (cleared by previous focus-out) and falls back to keystroke state. Acceptable for Phase 1 but Phase 2+ will miss revive on the first key after app switch.

**Fix**: request a sync read session on focus gain to prime the anchor.

### Logging instrumentation

`src/tsf/ReadonlyContextProvider.cpp:326` — TODO comment in-place. Wire up once user-facing log infra lands. (Log infra shipped 2026-05-11 — this item is now actionable as a 15-min cleanup if Phase 2 work resumes.)
