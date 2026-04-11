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
- **[10-refactoring-checklist.md](./CODING_RULES/10-refactoring-checklist.md)** - Refactoring safety checklist

### telex-test-specification/

- **[index.md](./telex-test-specification/index.md)** - Table of contents (21 files)

## Distillates (Token-Optimized)

- **[vietnamese-phonology-spec-distillate.md](./vietnamese-phonology-spec-distillate.md)** - Vietnamese syllable/tone/modifier rules
- **[SECURITY_FIXES-distillate.md](./SECURITY_FIXES-distillate.md)** - 7 security fixes: DACL, config limits, CLSID, enums, seqlock, buffer, ZIP
- **[tray-icon-sync-analysis-v2-distillate.md](./tray-icon-sync-analysis-v2-distillate.md)** - SharedState read-only bug, tray icon sync
- **[plans/pre-tone-stop-final-check-distillate.md](./plans/pre-tone-stop-final-check-distillate.md)** - Stop-final tone blocking: design, code, test cases

## Reference

- **[SECURITY.md](./SECURITY.md)** - Security hardening & threat model
- **[tsf-hook-coordination.md](./tsf-hook-coordination.md)** - TSF/Hook coexistence, no double-processing
- **[subdialog-checklist.md](./subdialog-checklist.md)** - Sciter subdialog implementation checklist
- **[TODO.md](./TODO.md)** - Outstanding tasks and review findings

## Plans (Active)

- **[plans/2026-03-28-sendinput-universal-output.md](./plans/2026-03-28-sendinput-universal-output.md)** - Universal SendInput output plan
- **[plans/2026-03-28-manual-test-matrix.md](./plans/2026-03-28-manual-test-matrix.md)** - Manual test matrix for SendInput

## Resources

- **[diagrams/](./diagrams/)** - Excalidraw diagrams (system overview, engine state machine, config flow)
- **[images/](./images/)** - UI screenshots
- **[x-unikey-1.0.4/](./x-unikey-1.0.4/)** - Unikey source code reference

## Archive

Historical docs (completed plans, resolved tech debt, old reviews) in `archive/` — not indexed.
