# NexusKey Code Governance

> Stated by the project owner 2026-05-05. Operationalizes [`PHILOSOPHY.md`](PHILOSOPHY.md).
> Every PR, design proposal, and architectural change MUST pass Part 1's 5-question gate before code is written.
> Three collaborators (humans + AI agents) share this file as the canonical context.

This document has three parts:

1. **The 5-question pre-code gate** — mandatory checklist printed in every architectural proposal.
2. **Architectural prescriptions** — five named patterns the codebase commits to (FSM core, output injection, IPC, plugins, corner cases). Each section is tagged with shipping status.
3. **Phasing** — which prescription ships in which sprint; the rest is roadmap, not current scope.

If a tactical decision conflicts with this document, this document wins. Update the document if the conflict represents a real design change.

---

## Part 1 — The 5-Question Pre-Code Gate

> Stop adding components ad-hoc. Stop patching bugs with locks.
> BEFORE proposing any code or architecture, the proposer (human or AI) MUST print the answers to all five questions below.

### Q1 — Layer Check
Is this written in the right layer (Hook layer / Engine layer / Output layer / UI layer)?

### Q2 — Performance Impact
What is the estimated latency cost (ns / µs / ms)? State a number, not "small" or "fast".

### Q3 — Native Alternative
Is there a lighter native OS solution (raw API, atomic primitive, lock-free structure)? Name the alternative considered and why it was rejected.

### Q4 — No-Lock / No-Exception Rule
Does this code use mutex, lock, dynamic allocation, or `try/catch` on the **hot path**?

- If YES → **REJECT IMMEDIATELY**. Replace with `std::atomic` / lock-free / FSM state.
- The hot path = anything reachable from `LowLevelKeyboardProc` (Rule #11.2).
- The single allowed exception: top-level rate-limited `try/catch` in the hook entry function (Rule #11.5), used as a hard-stop safety net only.

### Q5 — Trade-off
What is the explicit trade-off between performance and feature/cost? Name what we gain AND what we give up. "It's better" is not an answer.

A proposal that does not print answers to all 5 questions is sent back to the design moment.

---

## Part 2 — Architectural Prescriptions

Each section names a pattern the codebase commits to. The status tag identifies whether it has shipped, is currently being built, or is roadmap only.

### §1 — FSM Engine Core (no try/catch, no mutex) &nbsp;&nbsp;`[STATUS: roadmap, Sprint 3]`

- Replace `if/else` typing rules with a finite state machine encoded as a 2D static array.
- O(1) execution: `nextState = (*activeTable)[currentState][inputChar]`.
- **Dual tables** for "free typing" mode: `TABLE_STRICT` and `TABLE_FREE` generated at compile time. Hot-swap via `std::atomic<const int(*)[256]> activeTable`.
- Foreign consonants (`z`, `w`, `j`, `f`) and shorthand rules (`cc -> ch`) are valid transitions returning an Action ID. Engine consults atomic flags to enable/disable.
- **Multi-word rollback**: FSM keeps `HistoryRingBuffer<Record, 256>` of the last 256 keystroke/state pairs. Backspace past whitespace rewinds the history pointer to revive the previous word's state — O(1) editing.

### §2 — Output Injection Strategy &nbsp;&nbsp;`[STATUS: in-progress, Sprint 2 T3]`

- The engine NEVER calls OS APIs directly. It emits generic commands (concretely, `Replace(bsCount, text)` and `SendKey(vk)`).
- `IOutputInjector` interface consumes the commands. Concrete implementations:
  - `Win32SendInputInjector` — fastest, batch SendInput; covers Win32 plain Edit, Chrome omnibox, Notepad++, Chromium renderer textareas. Optional bait-char prefix for Chromium autocomplete-dismiss quirk.
  - `RichEditEmReplaceSelInjector` — sent-message channel via `EM_REPLACESEL` for hosts that require it (Win11 New Notepad RichEditD2DPT, Sprint 1 D12 verdict). A generic `WmCharInjector` may be added later for legacy hosts that need character-level WM_CHAR delivery; not in scope for T3.
  - `SplitDispatchInjector(int sleepMs)` — Electron (6 ms) and console (5 ms): split SendInput with `Sleep` between BS batch and char batch.
  - `TsfInjector` — TSF composition channel. Deferred until TSF revival.
- A focus-change callback (cold path) classifies the focused HWND and creates the right injector via factory. The injector is published to a `std::shared_ptr<IOutputInjector>` member via `std::atomic_store` — RCU pattern, same as the `config_` precedent (Sprint 1 D6).
- Hot path reads the published pointer via `std::atomic_load` and makes one virtual call. Each impl is marked `final` so the compiler can devirtualize when the static type is known.

### §3 — IPC + Concurrency (SPSC ring buffer + self-healing) &nbsp;&nbsp;`[STATUS: roadmap, Sprint 4]`

- **SPSC lock-free ring buffer** in shared memory. TSF and Hook NEVER capture keys in parallel — one is producer, the other observer. Resolves the historical "x2 space" race.
- **Side-channel context**: TSF reads context (e.g., password field) and publishes `std::atomic<InputContext>`. FSM reads to gate behavior.
- **Minimal hook + watchdog**: `HookEngine.cpp` only does `SetWindowsHookEx` and pushes events into the SPSC ring. A separate watchdog handles:
  - Windows silently dropping the hook on `LowLevelHooksTimeout`.
  - Hook hijacking (another app installs a hook above ours and skips `CallNextHookEx`).
  - Re-installation and self-healing.
- The "passive self-healing heartbeat" (T5 in current Phase 7.4 roadmap) is the first concrete step toward this prescription.

### §4 — Plugin Extension Layer &nbsp;&nbsp;`[STATUS: roadmap, Sprint 4+]`

- **Natural pass-through**: a key that violates the rules from the start (e.g., bare `J` or `W` in strict mode) sends the FSM into `STATE_PASS_THROUGH`. Raw key emitted to OS unchanged.
- **Fallback plugin** hooks `OnInvalidTransition`: reads history buffer → emits `Delete` to remove Vietnamese composition → emits `Insert` of the raw key → moves FSM to `STATE_PASS_THROUGH`.
- Plugin registration is a cold-path concern. Hot path consults a fixed registered handler via atomic pointer, never iterates a runtime list.

### §5 — Corner Cases &amp; Exceptions &nbsp;&nbsp;`[STATUS: partially shipped]`

- **Context reset**: arrow keys, mouse click → push `CMD_RESET` to the ring buffer (or directly clear engine composition state today). Already shipped: navigation keys cancel commit-undo and clear engine state in `HookEngine`.
- **Macro plugin** hooks `OnWordBoundary` (space / enter): looks up hash map, expands match. Already shipped: macro system in HookEngine.
- **Toggle hotkey** (Ctrl + Shift): hook handles inline, updates atomic V/E flag, posts async message to UI thread. Already shipped via `HotkeyManager`.
- **Quick Convert / Clipboard**: NEVER block FSM. Hotkey wakes a background worker thread that simulates `Ctrl+C`, manipulates clipboard, simulates `Ctrl+V`. Already shipped.

---

## Part 3 — Phasing

| Section | Status | Sprint | Notes |
|---|---|---|---|
| Part 1 — 5-question gate | **Active** | always | Print in every architectural proposal. |
| §1 FSM engine core | Roadmap | Sprint 3 | Engine refactor; T3 (output) does not touch the engine state machine. |
| §2 IOutputInjector | **In progress** | Sprint 2 T3 | See [`docs/plans/sprint-2-output-injector.md`](plans/sprint-2-output-injector.md) (work-in-progress). |
| §3 SPSC + Watchdog | Roadmap | Sprint 4 | Decouples hook capture from engine processing. T5 in current Phase 7.4 ("passive self-healing heartbeat") is the first step. |
| §4 Plugin extension | Roadmap | Sprint 4+ | Built on top of FSM (§1) and SPSC (§3). |
| §5 Corner cases | Mixed | shipped + ongoing | Arrow / mouse / macro / toggle / clipboard already in place; cleanup pass during §1. |

**Scope rule for current sprint (Sprint 2)**: T3 implements §2 ONLY. Bundling §1, §3, or §4 into T3 inflates scope from 2 weeks to 6+ weeks and violates Q5 (trade-off).

---

## Companion documents

- [`PHILOSOPHY.md`](PHILOSOPHY.md) — the four pillars (Nhanh / Nhẹ / Mượt / Mở rộng-no-cost), test-first, three pre-code questions. This file's 5-question gate is the operational form of those three questions.
- [`CODING_RULES/11-hook-system-rules.md`](CODING_RULES/11-hook-system-rules.md) — the 1 ms hot-path budget that Q4 enforces.
- [`CODING_RULES/4-interface-based-design.md`](CODING_RULES/4-interface-based-design.md) — the interface pattern §2 follows.
- [`../PROJECT_MAP.md`](../PROJECT_MAP.md) — module boundaries that Q1 refers to.
