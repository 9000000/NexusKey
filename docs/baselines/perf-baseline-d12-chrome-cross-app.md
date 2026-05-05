# perf-baseline-d12-chrome-cross-app

Sprint 1 D12 cross-app smoke run, after the Notepad / RichEditD2DPT
fix (commit `582dab2`). Chaos corpus driven against **Chrome**
(address bar / textarea — non-`useEditMsgPath_` host) instead of the
default Win11 New Notepad. Captured 2026-05-05 alongside the
canonical Notepad baseline (`perf-baseline-d12-richedit-fix-chaos.*`).

## Verdict — 10 / 11 PASS

| # | Case | Verdict | Actual | Notes |
|---|---|---|---|---|
| 1.1 | ghost-hoaf-bs-t | ✅ PASS | `hot` | |
| 1.2 | tone-ghost-toans-bs3-i | ✅ PASS | `ti` | |
| 1.3 | escape-bs-aa-b | ✅ PASS | `b` | |
| 2.1 | x2-space-vieejt-nam | ✅ PASS | `việt nam` | flipped vs Notepad-pre-fix |
| 2.2 | word-boundary-xin-chao-ban | ✅ PASS | `xin chào bạn` | flipped |
| 2.3 | en-vn-transition-hello-vieejt | ✅ PASS | `hello việt` | flipped |
| 3.3 | engine-stress-truongf | ✅ PASS | `trường` | flipped |
| 5.1 | case-tracking-Giar | ✅ PASS | `Giả` | |
| 5.2 | vowel-start-uongs | ✅ PASS | `uống` | flipped |
| **5.3** | **cross-word-bs-vieejt-nam-bs4-s** | ❌ **FAIL** | **`việts`** | new shape, see below |
| 6.1 | autocap-binh-thuongf | ✅ PASS | `bình thường` | |

## Why Chrome differs from Notepad

Fix C (commit `582dab2`) is **gated on `useEditMsgPath_`** — it only takes effect for hosts where `AppDetect: editMsg=1` is set. That flag is true for Win11 New Notepad / WinUI 3 RichEditBox-class hosts. Chrome's input control (the address bar's `OmniboxViewViews`, Gmail's Quill / Lexical, regular `<input>` / `<textarea>`) does **not** use the EditMsg path; it gets the original SendInput-batch (or split-Electron) output channel. So:

- Cases that were FAIL on Notepad because of the `RichEditD2DPT` async caret-lag are FAIL **only on Notepad** — Chrome processes them through a different path that already worked. The 5 / 11 → 11 / 11 jump on Notepad does **not** correspond to a 5 / 11 → 11 / 11 jump on Chrome; Chrome had its own pre-existing pass rate.
- The chaos corpus's `target_app = "notepad"` field is informational, not enforced — the runner just types into whatever window has focus. The locked baseline `perf-baseline-43fb4c1` was captured against Notepad.

## 5.3 on Chrome: failure shape `việts`

**Sequence:** `viejtnam\b\b\b\bs` — type `vieejt`, space, `nam`, BS×4 (deletes `nam` plus the trailing space, BS through commit-undo replays `việt` + `s` → engine state `viết`).

**Engine state at HEAD on this branch:** correct (`viết`). The two protective unit tests committed in D12.5 (`TelexEngineTest.D12_5_*`) confirm engine layer is clean for similar transforms.

**App actual:** `việts` (5 chars: `v-i-ệ-t-s`). Difference vs expected `viết` (4 chars: `v-i-ế-t`):
- The `s` tone modifier did **not** lift `ệ` (j-tone) → `ế` (s-tone).
- An extra `s` literal appended at the end.
- Net effect: the `BS=2 + 'ết'` portion of the synth burst was lost; the `reinjectVk='s'` portion landed.

**Suspected mechanism (not yet localised):**

1. After the BS×4 chain, app should be at `việt` (caret 4). Engine state empty, `previousComposition_` cleared by ResetComposition during commit-undo replay.
2. Engine processes 's' via commit-undo replay path: `ReplayCommittedChars` restores prev=`việt`, then `HandleAlphaKey('s')` produces composition=`viết`.
3. `ReplaceComposition(prev='việt', new='viết')`: commonLen=2, BS=2, toSend=`ết`, **reinjectVk='s'** (set on non-simple-append path because `!editMsgPath`).
4. Non-EditMsg batch path (line ~2934): SendInput batch = [VK_S keydown + BS + BS + 'ế' + 't'] (synthetic, NK marker).
5. **If Chrome is classified as Electron** by `IsKnownElectronExe` heuristic → split path with Sleep: `[VK_S, BS, BS]` sent first, sleep 6 ms, then `['ế', 't']` sent. Chrome (`chrome.exe`) does **not** match the Electron list — but Chrome's renderer process is `Chrome_RenderWidgetHostHWND`, which the heuristic may classify differently. Worth verifying which branch fires for Chrome.
6. Chrome's view runs on a renderer process / compositor — input may be processed async similar to RichEditD2DPT, and the BS+chars portion of the burst could race with the reinjected VK_S. If the renderer drops the synthetic BS or VK_PACKET events under burst, the result is `việt` + `s` literal = `việts`.

**Ruled out:**
- Engine logic (engine layer test passes for `truongwf` and equivalent transforms; replay path is shared with Notepad which now passes 5.3).
- The Notepad-specific async-render race (Chrome class isn't in the EditMsg detection set).

**Investigation paths for whoever picks this up:**

a. **Confirm Chrome's classification.** Reproduce against Chrome with debug build, capture `NexusKey_hook.log`, check `AppDetect: ... electron=? editMsg=?`. If `electron=1`, the split path with Sleep is in play; if both 0, plain batch path is in play. Different fix vectors per branch.

b. **Localise where `BS+'ết'` is dropped.** Add a HOOK_LOG inside the SendInput call sites confirming bytes-sent vs success, and a clipboard read on the test runner side immediately after `s` to capture mid-state. Can mirror the chaos-3.3-only.toml diagnostic pattern (single-case + post-mortem hook log).

c. **Try mirroring the EditMsg fix mechanism.** Chrome's renderer may need a similar "all output via the same channel" rule. Possible directions:
   - Force `useEditMsgPath_` = false but route everything through SendInput (no passthrough at all) and verify whether the batch / split path is consistent enough on Chrome.
   - Or: detect Chrome renderer class and add a new "all-SendInput-batch with no passthrough" path.

d. **Sprint 2 IOutputInjector.** The brainstorm Phase 7.4 roadmap already calls for an `IOutputInjector` abstraction that selects the right output strategy per host. This Chrome 5.3 case is a strong driver for that abstraction — three host classes (classic Win32, RichEditD2DPT EditMsg, Chrome renderer) each need a different but-internally-consistent output channel.

**This does NOT block Sprint 1 PR.** The Sprint 1 D12 gate requires regression-free chaos verdict on the canonical Notepad target — that's met (11 / 11). The Chrome case is a previously-undetected cross-app regression in 5.3 specifically; treat as Sprint 2 follow-up. Document it as a known limitation in the merge PR.

## Reproduce

```
NextKeyTestRunner.exe ^
    --corpus tools\NextKeyTestRunner\corpus\chaos.toml ^
    --hook-log build\Debug\NexusKey_hook.log
```

with foreground = Chrome (address bar or any input field), commit
`582dab2` Debug build.

## L1 timing comparison (Notepad vs Chrome, same commit)

Both runs at the same chaos `inter_key_us` per case:

| Case | Notepad p99 (ms) | Chrome p99 (ms) | Delta |
|---|---|---|---|
| 1.1 | 14 | TBD | — |
| 3.3 | 6 | TBD | — |
| 5.3 | 9 | TBD | — |

(Chrome run did not capture per-case L1; populate when teammate re-runs with `--perf-csv` against Chrome.)
