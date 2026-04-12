# Double-Key Preservation (ff, ss, rr)

**Status**: Research needed  
**Priority**: Low — current behavior is Telex standard  
**Date**: 2026-04-12  
**Origin**: Gõ Nhanh comparison (docs/gonhanh-architecture-comparison.md Rule 10.4)

## Problem

Tone escape collapses double keys: `off` → `of`, `boss` → `bos`, `err` → `er`.  
User must type 3 keys (e.g., `o-f-f-f`) to produce "off".

## Root Cause

`TelexEngine::ProcessTone()` line 362: `EraseConsumedRaw(target.toneRawIdx)` removes the first tone key from `rawInput_` on escape. After escape, `rawInput_ == composed` → `ShouldAutoRestore` returns false.

## Conflict: Double-Click Keyboards

Fixing this conflicts with tone escape — a core Telex feature:

| Scenario | Keys | Current | If fixed |
|----------|------|---------|----------|
| "và" + keyboard double-click | v-a-f-f | `vaf` (1 BS to fix) | `vaff` (2 BS — worse) |
| Typing "off" | o-f-f | `of` (missing f) | `off` (correct) |

All Vietnamese IMEs (Unikey, OpenKey, Gõ Nhanh) have this same behavior.

## Research Directions

1. **Spell-check-gated only**: Only preserve doubles when `spellCheckDisabled_` AND entire composed is ASCII. Doesn't help double-click keyboard users who have spell check ON.

2. **Timing-based**: If two identical keys arrive within ~30ms, treat as double-click (don't escape). Requires platform-level timing info not available in engine.

3. **Context-based**: If the word after escape is a known English word (off, boss, less), preserve. Requires dictionary — too heavy.

4. **Accept current behavior**: Document that `o-f-f-f` = "off" is standard Telex. Most users already know this.

## Decision

Deferred. Current behavior matches Telex standard. Revisit if users specifically request.
