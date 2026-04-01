# Docs Index

## Core Reference (Sharded)

### Architecture/

- **[index.md](./Architecture/index.md)** - Table of contents
- **[1-core-design-principles.md](./Architecture/1-core-design-principles.md)** - EXE/DLL split, shared memory IPC
- **[2-component-architecture.md](./Architecture/2-component-architecture.md)** - Component responsibilities and boundaries
- **[3-communication-architecture.md](./Architecture/3-communication-architecture.md)** - SharedState, seqlock, named events
- **[4-startup-sequences.md](./Architecture/4-startup-sequences.md)** - EXE and DLL initialization order
- **[5-runtime-behavior.md](./Architecture/5-runtime-behavior.md)** - Keystroke flow, mode switching
- **[6-failure-handling.md](./Architecture/6-failure-handling.md)** - Error recovery and fallback strategies
- **[7-state-isolation.md](./Architecture/7-state-isolation.md)** - Cross-process state separation
- **[8-named-event-protocol.md](./Architecture/8-named-event-protocol.md)** - Config reload event signaling
- **[9-security-considerations.md](./Architecture/9-security-considerations.md)** - DACL, HKCU, shared memory threats
- **[10-summary-the-two-questions.md](./Architecture/10-summary-the-two-questions.md)** - Quick decision framework
- **[appendix-a-quick-reference.md](./Architecture/appendix-a-quick-reference.md)** - Lookup table for components

### CODING_RULES/

- **[index.md](./CODING_RULES/index.md)** - Table of contents
- **[1-namespace-organization.md](./CODING_RULES/1-namespace-organization.md)** - NextKey:: namespace, no `using` in headers
- **[2-memory-resource-management.md](./CODING_RULES/2-memory-resource-management.md)** - Smart pointers, CComPtr, RAII
- **[3-error-handling.md](./CODING_RULES/3-error-handling.md)** - Never block user, async notify + fallback
- **[4-interface-based-design.md](./CODING_RULES/4-interface-based-design.md)** - Abstract interfaces for testability
- **[5-struct-versioning.md](./CODING_RULES/5-struct-versioning.md)** - SharedState versioning, 7-step toggle checklist
- **[6-logging.md](./CODING_RULES/6-logging.md)** - Debug logging patterns
- **[7-debug-architecture.md](./CODING_RULES/7-debug-architecture.md)** - DebugConsole, OutputDebugString
- **[8-tsf-specific-rules.md](./CODING_RULES/8-tsf-specific-rules.md)** - Edit sessions, composition cleanup
- **[9-naming-conventions.md](./CODING_RULES/9-naming-conventions.md)** - PascalCase/camelCase/UPPER_SNAKE rules

### telex-test-specification/

- **[index.md](./telex-test-specification/index.md)** - Table of contents
- **[test-organization-strategy.md](./telex-test-specification/test-organization-strategy.md)** - Test suite structure and naming
- **[category-1-basic-vowels-no-transformation.md](./telex-test-specification/category-1-basic-vowels-no-transformation.md)** - Plain vowel passthrough tests
- **[category-2-circumflex-modifier.md](./telex-test-specification/category-2-circumflex-modifier.md)** - aa/ee/oo → â/ê/ô tests
- **[category-3-breve-modifier.md](./telex-test-specification/category-3-breve-modifier.md)** - aw → ă tests
- **[category-4-horn-modifier.md](./telex-test-specification/category-4-horn-modifier.md)** - ow/uw → ơ/ư tests
- **[category-5-stroke-modifier.md](./telex-test-specification/category-5-stroke-modifier.md)** - dd → đ tests
- **[category-6-tone-marks.md](./telex-test-specification/category-6-tone-marks.md)** - s/f/r/x/j tone application tests
- **[category-7-falling-diphthongs-tone-on-first-vowel.md](./telex-test-specification/category-7-falling-diphthongs-tone-on-first-vowel.md)** - ai/ao/au/oi/ui tone placement
- **[category-8-rising-diphthongs-tone-on-second-vowel.md](./telex-test-specification/category-8-rising-diphthongs-tone-on-second-vowel.md)** - oa/oe/uy/uê tone placement
- **[category-9-triphthongs.md](./telex-test-specification/category-9-triphthongs.md)** - iêu/ươi/oai triphthong tests
- **[category-10-w-modifier-priority-7-levels.md](./telex-test-specification/category-10-w-modifier-priority-7-levels.md)** - 'w' key priority resolution tests
- **[category-11-tone-placement-priority.md](./telex-test-specification/category-11-tone-placement-priority.md)** - Horn > modified > diphthong > default
- **[category-12-tone-relocation.md](./telex-test-specification/category-12-tone-relocation.md)** - Tone moves when horn applied after tone
- **[category-13-escape-mechanism.md](./telex-test-specification/category-13-escape-mechanism.md)** - Double-press escape tests
- **[category-14-edge-cases.md](./telex-test-specification/category-14-edge-cases.md)** - Boundary and unusual input tests
- **[category-15-real-vietnamese-words.md](./telex-test-specification/category-15-real-vietnamese-words.md)** - End-to-end word typing tests
- **[category-16-stop-final-tone-block-spellcheckenabled-true.md](./telex-test-specification/category-16-stop-final-tone-block-spellcheckenabled-true.md)** - Stop-final tone restriction tests
- **[implementation-checklist.md](./telex-test-specification/implementation-checklist.md)** - Feature coverage tracking
- **[known-issues-to-investigate.md](./telex-test-specification/known-issues-to-investigate.md)** - Open bugs and edge cases
- **[test-execution-strategy.md](./telex-test-specification/test-execution-strategy.md)** - How to run and filter tests

## Distillates (Token-Optimized)

- **[vietnamese-phonology-spec-distillate.md](./vietnamese-phonology-spec-distillate.md)** - Vietnamese syllable/tone/modifier rules for engine dev
- **[SECURITY_FIXES-distillate.md](./SECURITY_FIXES-distillate.md)** - 7 security fixes: DACL, config limits, CLSID, enums, seqlock, buffer, ZIP
- **[tray-icon-sync-analysis-v2-distillate.md](./tray-icon-sync-analysis-v2-distillate.md)** - SharedState read-only bug, tray icon sync debugging
- **[plans/pre-tone-stop-final-check-distillate.md](./plans/pre-tone-stop-final-check-distillate.md)** - Stop-final tone blocking: design, code, test cases

## Standalone Docs

- **[Planning.md](./Planning.md)** - Hybrid TSF + Hook architecture planning
- **[tsf-hook-coordination.md](./tsf-hook-coordination.md)** - TSF/Hook coexistence, no double-processing
- **[subdialog-checklist.md](./subdialog-checklist.md)** - Sciter subdialog implementation checklist
- **[Win10_UI_Fixed.md](./Win10_UI_Fixed.md)** - Win10 rendering and tray icon sync fixes
- **[_optimize_UI.md](./_optimize_UI.md)** - Sciter UI optimization rollback notes

## Plans

- **[plans/2026-03-28-sendinput-universal-output.md](./plans/2026-03-28-sendinput-universal-output.md)** - Universal SendInput output implementation plan
- **[plans/2026-03-28-manual-test-matrix.md](./plans/2026-03-28-manual-test-matrix.md)** - Manual test matrix for SendInput output

## Superpowers (Specs & Plans)

- **[superpowers/specs/2026-03-30-auto-disable-vn-non-english-layout-design.md](./superpowers/specs/2026-03-30-auto-disable-vn-non-english-layout-design.md)** - Auto-disable Vietnamese for incompatible layouts design
- **[superpowers/plans/2026-03-30-auto-disable-vn-non-english-layout.md](./superpowers/plans/2026-03-30-auto-disable-vn-non-english-layout.md)** - Auto-disable Vietnamese implementation plan
- **[superpowers/plans/2026-04-01-excludedapps-import-export.md](./superpowers/plans/2026-04-01-excludedapps-import-export.md)** - ExcludedApps import/export implementation plan

## Implemented (Historical)

- **[implemented/2026-03-23-session-fixes.md](./implemented/2026-03-23-session-fixes.md)** - l-u-u-w fix, engine state fixes
- **[implemented/quick-consonant-backspace.md](./implemented/quick-consonant-backspace.md)** - Quick consonant backspace & auto-restore

## Tech Debt (Resolved)

- **[tech-debt/engine-backspace-engprot-duplication.md](./tech-debt/engine-backspace-engprot-duplication.md)** - RESOLVED: English protection recalc duplication
- **[tech-debt/pushchar-literal-path-duplication.md](./tech-debt/pushchar-literal-path-duplication.md)** - RESOLVED: PushChar literal path duplication

## Archive

Original (pre-distillation/pre-shard) files in `archive/` — not indexed.
