# Firefox `<img>` + Backspace → Space-eater bug

**Status:** Documented limitation. Hook-mode cannot fix; TSF mode is the
workaround. Fix paths (extension or TSF revival) deferred.

**Investigation date:** 2026-04-21

**Originally tracked as:** "Firefox surrogate pair offset" (issue #92). That
hypothesis was **wrong** — investigation in this session identified the real
pattern.

---

## The real bug

On Firefox (all Gecko browsers — Firefox, Floorp, LibreWolf, Waterfox, Tor),
when the DOM is in this state:

```
<p><img class="smilie">|</p>           ← caret right after an <img>
```

and any input sequence arrives that contains:

```
<some write> → BS → <more writes>
```

**Firefox silently drops the event immediately after the BS.** This shows up
as "space eaten" when a Vietnamese IME performs its normal correction flow:

1. User types `a` → IME passes through → DOM has `<img>a`
2. User types `s` (Telex tone) → IME emits `BS + VK_PACKET("á")` → DOM has `<img>á`
3. User types `space` → **event lost** → DOM still has `<img>á`
4. User types `a` → passthrough → DOM has `<img>áa`
5. User types `s` → `BS + VK_PACKET("á")` → DOM has `<img>áá`

Expected: `á á` (two chars with space between). Actual: `áá`.

Chrome shows a **mild version** of the same bug — space eaten exactly once
on the first post-`<img>` transition, then subsequent typing is fine.

---

## What does NOT fix it

All tested via `vkpacket-full-repro.ahk` (pure AutoHotkey, no NexusKey in the
loop) on vn-z.vn after inserting a site smilie:

| Hypothesis | Test key | Result |
|---|---|---|
| Scancode vs VK_PACKET space | F9 (no BS) | Works — but no BS, so not the repro pattern |
| Physical 'a' + scancode space interaction | F7 | Fail — space eaten |
| Physical 'a' + VK_PACKET space | F8 | Fail |
| VK_PACKET 'a' (no physical) + scancode space | F5 | Fail |
| VK_PACKET 'a' + VK_PACKET space | F6 | Fail |
| Atomic batch BS + á + space in one Send | F2 | Fail |
| Longer Sleep (200ms) between correction and space | F3 | Fail |
| Everything atomic in single SendInput call | F4 | Fail |

All VK_PACKET without BS works (F10). **BS is the necessary trigger.**

Tested IMEs that reproduce: NexusKey (latest), NexusKey v2.1.16 (pre-WASD-fix),
UniKey. Rules out NexusKey regression, rules out passthrough architecture
as the cause.

---

## What DOES work

1. **`document.execCommand('insertText', false, 'á á')`** from the page
   context (DevTools Console or a browser extension content script).
   Verified via `devtools-probe.js` → `__ns.probe1()` on vn-z.vn.

2. **`Ctrl+V` clipboard paste.** Bypasses the VK_PACKET IPC path entirely.
   **Not usable** as a general fix: stomps user clipboard, fires `onpaste`
   handlers that break Google Docs / Notion / Slack.

3. **TSF mode.** NexusKey ships a TSF DLL (see `src/tsf/`) but it is disabled
   by default per `project_hook_only.md`. TSF uses `ITfInsertAtSelection`
   which does NOT use the `BS + VK_PACKET` correction pattern. Users who hit
   this bug can enable TSF mode as an opt-in workaround.

---

## Spike artefacts in this directory

| File | Purpose |
|---|---|
| `devtools-probe.js` | Paste into DevTools Console on any site; reports DOM state, then `__ns.probe1()` runs `execCommand('insertText')` and reports whether it works |
| `spike.html` | Local contenteditable/textarea/input scratchpad — opens in Firefox to test DOM mutation without a real site |
| `vkpacket-repro.ahk` | Simple F9 → VK_PACKET "á space á" (confirmed WORKS) |
| `vkpacket-mix-repro.ahk` | Variations mixing VK_PACKET + scancode for space |
| `vkpacket-full-repro.ahk` | Full NexusKey-style patterns (F2-F11) — the definitive reproducer set |

---

## Why documented instead of fixed

- **Extension + execCommand fix**: ~2 weeks (Firefox extension port is
  deferred in current per-domain-exclusion design — see
  `docs/plans/2026-04-21-per-domain-exclusion-design.md`). Overkill for
  a bug that only affects forum-class sites with sprite `<img>` smilies.
- **TSF revival for Firefox only**: ~1 day spike. Viable but against the
  project's hook-only preference.
- **Accept + document + TSF fallback**: current choice. Users on sprite-smilie
  forums can enable TSF mode. Modern Unicode-emoji sites are unaffected.

---

## Future-AI notes

- Do **not** attempt to "fix" this by tweaking `InjectKey`, `ReplaceComposition`,
  Sleep timings, or `synthEventsPending_` guards. None of those paths work.
  Evidence is in the AHK scripts above.
- Do **not** draft a Bugzilla report using the old "surrogate pair offset"
  framing — it will not reproduce for Mozilla engineers (Win+. emoji works
  fine; bug needs `<img>` specifically).
- If filing upstream, use `F7` from `vkpacket-full-repro.ahk` as the minimum
  repro. File under `Core :: Widget: Win32`.
- If revisiting a fix, the two viable paths are: extension with DOM
  `execCommand` (proven), or TSF revival (untested end-to-end on this bug
  but the input path differs fundamentally).
