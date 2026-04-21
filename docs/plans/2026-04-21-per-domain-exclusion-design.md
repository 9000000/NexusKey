# Per-Domain Exclusion (Browser Extension + Native Messaging)

**Date:** 2026-04-21
**Status:** Design approved, not yet implemented
**Tracking:** GitHub issue [#96](https://github.com/phatMT97/NexusKey/issues/96)

**Goal:** Let users disable Vietnamese input on specific websites (e.g., typing-practice sites, English-learning sites) without manually toggling V/E. Per-app exclusion already exists; this adds per-hostname granularity inside browsers.

---

## 1. Scope & Non-goals

**In scope (v1):**
- Hostname-level exclusion with implicit subdomain suffix match (enter `typingclub.com` → matches `*.typingclub.com`).
- Reference open-source browser extension under `/extension/` in the repo — Chromium-based (Chrome, Edge, Brave, Opera, Vivaldi, Arc) covered by one codebase.
- Chrome Native Messaging as the IPC transport.
- Stateless helper process `NexusKeyBridge.exe` bridging extension stdin/stdout to SharedState.
- Sciter + Classic/Lite settings UI parity.
- Reuse of the existing `isExcludedApp_` passthrough behavior — V/E state preserved across tab switches.
- Fail-open (VN typing works) on any extension/bridge failure.

**Out of scope (v1, deferred):**
- Firefox port — ships as `/extension-firefox/` later, community PR welcome. ~20 lines of diffs (`browser.*` namespace, manifest).
- Wildcard / path matching (`*.foo.com/learn/*`). YAGNI.
- Regex patterns. YAGNI.
- Chrome Web Store / Edge Add-ons distribution. User sideloads unpacked extension.
- Automated extension tests (Puppeteer/Playwright). Manual matrix in `docs/browser-extension.md`.

**Explicitly rejected alternatives** (considered and dropped):
- **UI Automation (UIA) to read browser address bar** — fragile to browser UI changes, breaks in PWAs / fullscreen / reader mode, per-browser quirks make it heavier to maintain than an extension.
- **Local WebSocket on fixed port** — firewall prompt, port collision risk, extension ID unverified.
- **File-drop polling** — disk IO, stale state, ugly.
- **Force V→E on excluded URL** — confusing; passthrough-mode (engine off, V/E unchanged) matches existing excluded-apps behavior.

---

## 2. Architecture

```
┌────────────────── Chrome / Edge / Brave / Firefox ──────────────────┐
│                                                                     │
│  reference extension (MV3, open source, /extension/)                │
│  listens: tabs.onActivated, tabs.onUpdated, windows.onFocusChanged  │
│  sends:   {"type":"url", "browser":"chrome.exe", "url":"..."}       │
│  chrome.runtime.connectNative("com.nexuskey.bridge")                │
└──────────────────────────────┬──────────────────────────────────────┘
                               │  stdin/stdout (4-byte LE + JSON)
                               │  browser spawns ONE host per session
                               ▼
┌──────────────── NexusKeyBridge.exe (new helper) ────────────────────┐
│  Reads length-prefixed JSON frames. Validates shape.                │
│  Writes hostname into per-browser SharedState slot.                 │
│  On stdin EOF → clears slot → exits.                                │
└──────────────────────────────┬──────────────────────────────────────┘
                               │  SharedState ("Local\NexusKeyBrowserUrls")
                               ▼
┌──── HookEngine (EXE) + KeyEventSink/EngineController (TSF DLL) ─────┐
│  On foreground change: read URL slot for currentExe_                │
│                         → isExcludedUrl_ = domainMatcher.Match()    │
│  Hot path: replace `isExcludedApp_` with                            │
│            `(isExcludedApp_ || isExcludedUrl_)`. O(1) lookup.       │
└─────────────────────────────────────────────────────────────────────┘
```

**Key architectural decisions:**
1. **Separate helper EXE, not in `NexusKeyApp.exe`**. Chrome launches a fresh process per session with its own stdin/stdout; routing into the singleton tray app would need a named-pipe hop. Stateless helper is ~200 LoC, crash-isolated, works even if the IME isn't running yet.
2. **Extension in `/extension/` in this repo.** Community PRs land in the same repo. One MV3 manifest covers all Chromium browsers unmodified.
3. **SharedState is the only contact point** between bridge and engine — matches every other IPC in the project. No new events, no new COM.
4. **Separate SharedState mapping** (`Local\NexusKeyBrowserUrls`), not crammed into existing `SharedStateLayout` — isolates seqlock pattern, keeps config-change path lean.

---

## 3. Wire Protocol

**Frame format** (Chrome Native Messaging standard):
- 4-byte little-endian length prefix, then UTF-8 JSON body.
- Chrome caps at 1 MB. We enforce 8 KB in the bridge for safety (hostname fits in < 256 chars).

**Extension → bridge:**

```json
{"type": "url",   "browser": "chrome.exe", "url": "https://www.typingclub.com/lessons/12"}
{"type": "blur",  "browser": "chrome.exe"}
{"type": "hello", "browser": "chrome.exe", "ver": 1}
```

- `url` — sent on tab activate, tab URL update, window focus. Bridge extracts hostname, lowercases, punycode-decodes via `IdnToUnicode`, writes to slot.
- `blur` — browser window lost focus to another app. Bridge clears its slot. (Engine already detects exe change via foreground hook, but `blur` prevents stale URL leaking in the multi-window case.)
- `hello` — handshake on `connectNative`. Forward-compat hook. Bridge replies `ack`.

**Bridge → extension:**

```json
{"type": "ack",   "ver": 1}
{"type": "error", "code": "bad_frame", "msg": "..."}
```

**Bridge does NOT:**
- Read the excluded-domains list. Bridge is a dumb pipe — match logic lives in `HookEngine` / `DomainMatcher` only.
- Store the full URL. Only the hostname hits SharedState. Privacy win: memory-map readers never see query strings or paths.

**Security posture:**
- Native messaging manifest pins `allowed_origins: ["chrome-extension://<our-id>/"]`. Only our (or a user-forked) extension ID can launch the bridge.
- Sideloaded forks get different IDs — users who run community forks must paste their ID into the manifest. Documented.
- Bridge validates JSON shape, rejects frames > 8 KB, exits on malformed input.

---

## 4. NexusKey Code Changes

### 4.1 New files

| File | LoC | Purpose |
|---|---|---|
| `src/bridge/main.cpp` | ~200 | `NexusKeyBridge.exe` entry — stdin frame loop, SharedState writer |
| `src/bridge/NativeMessaging.h` | ~80 | `ReadFrame()` / `WriteFrame()` helpers (4-byte LE + JSON) |
| `src/core/ipc/BrowserUrlState.h` | ~60 | Layout + seqlock access for per-browser URL slots |
| `src/core/config/ExclusionValidator.cpp/h` | ~120 | Shared domain normalization (scheme/www strip, lowercase, IDN, reject IP/path/duplicate) + hostname suffix matcher |
| `src/app/dialogs/ExcludedDomainsDialog.cpp/h` | ~200 | Sciter subdialog — list management, import/export |
| `src/app/classic/ClassicExcludedDomainsDialog.cpp/h` | ~300 | Win32 native dialog (Lite build) |
| `src/app/ui/excludeddomains/excludeddomains.{html,css,js}` | ~150 HTML/CSS + ~80 JS | Subdialog UI |
| `CMakeLists.txt` | +1 target | `NexusKeyBridge` executable, links `NextKeyCore` (SharedState + WinStrings only) |

### 4.2 SharedState additions

New mapping, not added to existing `SharedStateLayout`:

```cpp
// "Local\\NexusKeyBrowserUrls" — independent TTL from config state
struct BrowserUrlSlot {
    wchar_t exeName[64];      // "chrome.exe", "msedge.exe"
    wchar_t hostname[256];    // lowercased, punycode-decoded
    uint64_t lastUpdateTick;  // GetTickCount64() — staleness check (30 s TTL)
    uint32_t generation;      // seqlock for atomic read (odd = writing)
};
static constexpr size_t kMaxBrowserSlots = 8;  // Chrome, Edge, Brave, Firefox, Opera, Vivaldi, Arc, spare
struct BrowserUrlState { BrowserUrlSlot slots[kMaxBrowserSlots]; };
```

**Reader side** uses the **same retry-3× seqlock pattern** as `HookContextAnchor` (see `CLAUDE.md` "HookContextAnchor seqlock" pitfall). Dedicated `WriteSlot()` / `ReadSlot()` entry points — **never** bundled into a whole-struct copy.

### 4.3 Modified files

| File | Change |
|---|---|
| `src/core/config/TypingConfig.h` | `std::vector<std::wstring> excludedDomains` (new field, empty default) |
| `src/core/config/ConfigManager.cpp` | TOML read/write for `[exclusion] domains = [...]`. Normalize via `ExclusionValidator` on load to self-heal legacy entries. |
| `src/app/system/HookEngine.cpp` | `isExcludedUrl_` cache + `ReloadUrlForForeground()` called from same place as `ReloadExcludedApps()`. Compose as `(isExcludedApp_ \|\| isExcludedUrl_)` at lines 220, 601, 1888, and Smart Switch gate at 250. |
| `src/tsf/EngineController.cpp` | Same OR in the TSF read-only gate. |
| `src/app/main.cpp` | On startup: probe browser registry keys, register native-messaging host manifests in HKCU. Reversed on uninstall / feature toggle off. |
| `src/app/dialogs/SettingsDialog.cpp` + `src/app/ui/settings/settings.html` | Add row: toggle `excludeDomainsEnabled` + "Configure domains…" button. |
| `src/app/classic/ClassicSettingsDialog.cpp` + `.rc` | Mirror row in Classic settings. |

### 4.4 Native messaging host manifest registration

Happens in `NexusKeyApp.exe` (not the installer) so portable/unpacked installs work too:
1. On startup, probe each known browser's registry path:
   - `HKCU\Software\Google\Chrome\NativeMessagingHosts\`
   - `HKCU\Software\Microsoft\Edge\NativeMessagingHosts\`
   - `HKCU\Software\BraveSoftware\Brave-Browser\NativeMessagingHosts\`
   - etc.
2. Write manifest JSON to `%LOCALAPPDATA%\NexusKey\bridge-manifest.json` pointing at `NexusKeyBridge.exe`.
3. Register the path under `com.nexuskey.bridge` for each detected browser.
4. On uninstall / feature disable → delete registry entries.

**Settings UI "extension installed?" hint:** if no manifest registered and no browser detected in use, show subtle "⚠ Extension not installed — [guide]" under the feature row. Doesn't block — user can still edit the list; it takes effect once they install.

### 4.5 LoC summary

- C++: ~800 (bridge 280, core 180, dialogs 500, engine hooks 40)
- JS (extension): ~150
- HTML/CSS: ~300

Net-new surface: ~1,250 lines. Hot-path cost: one extra `||` per keystroke check, one SharedState seqlock read per foreground change.

---

## 5. Reference Extension

**Location:** `/extension/` at repo root. One codebase covers Chrome / Edge / Brave / Opera / Vivaldi / Arc. Firefox port = separate `/extension-firefox/` (~3 diffs documented).

**File layout** (~150 LoC JS total):

```
extension/
├── manifest.json          # MV3
├── background.js          # service worker — tab/window listeners + native port
├── icons/ (16, 48, 128)   # reuse NextKey tray icon
└── README.md              # install instructions, PR guidance
```

**`manifest.json`:**

```json
{
  "manifest_version": 3,
  "name": "NexusKey URL Bridge",
  "version": "1.0.0",
  "description": "Tells NexusKey which site you're on so it can disable Vietnamese typing on excluded domains.",
  "permissions": ["tabs", "nativeMessaging"],
  "background": { "service_worker": "background.js" },
  "icons": { "16": "icons/16.png", "48": "icons/48.png", "128": "icons/128.png" }
}
```

No `host_permissions` — `tabs` permission reads URLs, lighter grant than `<all_urls>`, less scary at install time.

**`background.js` (full behavior):**

```js
const HOST = "com.nexuskey.bridge";
const BROWSER_EXE = detectBrowser();   // "chrome.exe" | "msedge.exe" | ...
let port = null;

function connect() {
  port = chrome.runtime.connectNative(HOST);
  port.onMessage.addListener(msg => { /* log acks/errors */ });
  port.onDisconnect.addListener(() => { port = null; /* lazy reconnect */ });
  send({ type: "hello", browser: BROWSER_EXE, ver: 1 });
  pushActiveTab();
}

function send(obj) { if (!port) connect(); port?.postMessage(obj); }

function pushActiveTab() {
  chrome.tabs.query({ active: true, lastFocusedWindow: true }, ([tab]) => {
    if (tab?.url && tab.url.startsWith("http")) {
      send({ type: "url", browser: BROWSER_EXE, url: tab.url });
    }
  });
}

chrome.tabs.onActivated.addListener(pushActiveTab);
chrome.tabs.onUpdated.addListener((id, info, tab) => {
  if (info.url && tab.active) send({ type: "url", browser: BROWSER_EXE, url: info.url });
});
chrome.windows.onFocusChanged.addListener(wid => {
  if (wid === chrome.windows.WINDOW_ID_NONE) send({ type: "blur", browser: BROWSER_EXE });
  else pushActiveTab();
});
```

**MV3 service-worker caveat:** Chrome sleeps service workers after ~30 s idle. The `connectNative` port keeps it alive while events flow; between events it unloads. Next event → `connect()` re-spawns the port and the bridge. ~5 ms extra on first URL change after quiet period. **Do not** add `chrome.alarms` heartbeats — over-engineering.

**Browser detection:** `BROWSER_EXE` baked at extension-load via `navigator.userAgent` (`"Edg/"` → msedge.exe, `"OPR/"` → opera.exe, `"Brave/"` → brave.exe, default chrome.exe). Imperfect (UA switchers lie) but worst case is slot lands in wrong exe → no match → fail-safe.

**`file://`, `chrome://`, `about:` URLs:** filtered in `pushActiveTab` (`.startsWith("http")`). Engine stays in default non-excluded state for local files.

---

## 6. Settings UI

### 6.1 Sciter (main app)

New subdialog `ExcludedDomainsDialog`, sibling to `ExcludedAppsDialog`. Same subprocess-dialog pattern, same TOML round-trip via `ConfigEvent`. ~80% copy of `ExcludedAppsDialog` with `exe` → `domain` and the `WindowPickerDialog` base dropped (no window picker — plain text input).

**Layout:**

```
┌─ Excluded Domains ─────────────────────────────────────────┐
│                                                            │
│  Vietnamese input will be disabled on these websites and   │
│  all their subdomains. Requires the NexusKey browser       │
│  extension. [Install guide →]                              │
│                                                            │
│  ┌─ Add domain ──────────────────────────────┬───────┐     │
│  │ typingclub.com                            │ + Add │     │
│  └───────────────────────────────────────────┴───────┘     │
│                                                            │
│  ┌─ Excluded ──────────────────────────────────────────┐   │
│  │  typingclub.com         (matches *.typingclub.com) ✕│   │
│  │  duolingo.com           (matches *.duolingo.com)   ✕│   │
│  └─────────────────────────────────────────────────────┘   │
│                                                            │
│  [ Import…]  [ Export…]                 [ OK ]  [ Cancel ] │
└────────────────────────────────────────────────────────────┘
```

**Main Settings entry:** one row under the existing "Excluded apps" row in `src/app/ui/settings/settings.html` — same toggle + button pattern. Toggle = `excludeDomainsEnabled`, button opens subdialog.

### 6.2 Classic / Lite

New `ClassicExcludedDomainsDialog` using Win32 native controls — `SysListView32` (consistent with `ClassicExcludedAppsDialog`), `Edit` + `Button` for add row, DEL-key and double-click-to-remove. Add `IDD_EXCLUDED_DOMAINS` template to `NexusKeyLite.rc`, `IDS_*` row in `resource.h`.

**Parent dialog:** one row in `ClassicSettingsDialog.cpp` under "Loại trừ ứng dụng": checkbox + "Danh sách..." button mirroring `ClassicExcludedAppsDialog` invocation.

**Extension hint:** grayed static-text line below the checkbox ("Yêu cầu cài extension trình duyệt — xem docs/browser-extension.md"). No `SysLink` for visual consistency.

### 6.3 Input validation (shared)

`ExclusionValidator::NormalizeDomain(std::wstring&)` — called by both Sciter JS (via dialog binding) and Classic C++:
- Strip scheme (`https://`, `http://`)
- Strip leading `www.`
- Lowercase
- Punycode encode via `IdnToAscii` then re-decode via `IdnToUnicode` for consistent storage
- Reject: empty, whitespace, IPv4/IPv6 literal, `localhost`, contains `/`, `?`, `#` after strip, duplicate of existing entry

**Single source of truth** — zero divergence between Lite and main UI.

### 6.4 Matching semantics (suffix, implicit subdomains)

`DomainMatcher::Match(const std::wstring& hostname)` — returns true iff any entry `E` in excluded set satisfies:
- `hostname == E`, OR
- `hostname` ends with `"." + E`

Explicit guard against suffix-bug: `typingclub.com.evil.com` must NOT match `typingclub.com`. Achieved by checking the `.` boundary.

---

## 7. Engine Integration

### 7.1 Hot path changes

Four single-line ORs in `HookEngine.cpp`:

| Line (current) | Change |
|---|---|
| 220 (`ToggleVietnameseMode` block check) | `if (excludeApps_ && isExcludedApp_)` → `if ((excludeApps_ && isExcludedApp_) \|\| isExcludedUrl_)` |
| 250 (Smart Switch gate) | `if (smartSwitch_ && !isExcludedApp_)` → `if (smartSwitch_ && !isExcludedApp_ && !isExcludedUrl_)` |
| 601 (foreground change — stale-flag detection) | parallel branch for `isExcludedUrl_` |
| 1888 (`modeChangeCallback_`) | `!isExcludedApp_ && vietnameseMode_` → `!isExcludedApp_ && !isExcludedUrl_ && vietnameseMode_` |

Plus one OR in `src/tsf/EngineController.cpp` read-only gate.

### 7.2 New `ReloadUrlForForeground()`

Called from same place as `ReloadExcludedApps()` — on foreground window change and on config reload. Reads slot keyed by `currentExe_`, checks `lastUpdateTick` freshness (30 s TTL), calls `DomainMatcher::Match()`, sets `isExcludedUrl_`. ~40 LoC.

### 7.3 Smart Switch composition

Current behavior: Smart Switch gates on `!isExcludedApp_` (line 250). Extended to `!isExcludedApp_ && !isExcludedUrl_`. Smart Switch naturally pauses on excluded URLs and resumes on non-excluded URLs. Per-app preferences (keyed on `exeName`) still apply to non-excluded pages in the same browser — composes correctly.

**V/E state preservation:** URL exclusion does **not** touch stored V/E state. Engine goes passthrough mode while `isExcludedUrl_=true`; when cleared, prior V/E state is still there. Matches existing excluded-app behavior.

---

## 8. Edge Cases & Failure Modes

| Event | Worst-case lag | Impact |
|---|---|---|
| Switch to excluded tab, type immediately | ~5–20 ms | First 1–2 keystrokes may compose VN. Acceptable — same class of race as existing app-switch detection. |
| Extension SW asleep, user hits excluded tab | ~50–100 ms | Same — few keystrokes leak. Undoable via Escape. |
| Leaving excluded tab back to VN-allowed | ~5 ms | Negligible. |
| Browser crash while on excluded URL | Bridge stdin EOF → clears slot | Engine normal within ~50 ms. |
| Bridge crashes mid-session | Stale slot. `lastUpdateTick > 30 s` → reader ignores. | **Fails open** (VN typing works). Next browser event re-spawns bridge. |

**Stale-slot TTL (30 s) is load-bearing.** Prevents a dead bridge from permanently disabling VN on a browser now closed.

**PWA / standalone web apps:** PWA runs as `msedge.exe --app=https://foo.com`; exe name is still `msedge.exe`. Extension reports URL normally. Works.

**Incognito / Private:** extension must be explicitly enabled for incognito (user setting). Default off. When disabled, bridge never receives incognito URLs → fails open. Privacy-correct default. Documented.

**Two browsers open:** Chrome on excluded, Firefox on allowed. Focused window's exe drives slot lookup. Per-browser slots keep state isolated.

**Multi-window same browser:** `windows.onFocusChanged` fires; `pushActiveTab()` reports the newly-focused window's active tab. Correct.

**User disables extension mid-session:** Native port closes → bridge stdin EOF → bridge clears slot → VN resumes.

**User uninstalls NexusKey:** Registry manifest removed on uninstall. Next browser session `connectNative` fails → extension logs error, silently no-ops. If NexusKey reinstalled, works after browser restart.

**URL with credentials (`https://user:pass@foo.com/`):** `new URL().hostname` strips userinfo in extension. Bridge only stores hostname. Credentials never hit SharedState.

**`file://`, `chrome://`, `about:`:** filtered in extension (`.startsWith("http")`). Engine in default non-excluded state.

**Config reload:** Existing `ConfigEvent` signals `HookEngine` to re-parse TOML. Adding `excludedDomains` hooks into the same path — no new event.

---

## 9. Testing

### 9.1 Unit tests (Linux-buildable, `tests/`)

| Suite | Covers |
|---|---|
| `ExclusionValidatorTest.cpp` (new) | `NormalizeDomain()` — scheme/www strip, lowercase, IDN, reject IP/localhost/paths/duplicates. ~15 cases. |
| `DomainMatcherTest.cpp` (new) | `typingclub.com` in set → `typingclub.com` ✅, `www.typingclub.com` ✅, `lessons.typingclub.com` ✅, `typingclub.com.evil.com` ❌, `notypingclub.com` ❌. ~10 cases. |
| `ConfigManagerTest.cpp` (extend) | TOML round-trip for `[exclusion] domains = [...]`. |
| `NativeMessagingFrameTest.cpp` (new) | `ReadFrame()` / `WriteFrame()` — length prefix, oversize reject (>8 KB), malformed JSON, UTF-8. ~8 cases. |

No new SharedState test — pattern identical to existing `SharedStateTest.cpp` slots. Seqlock reused, not new.

### 9.2 Integration (Windows, manual — `docs/browser-extension.md`)

1. Install NexusKey, run `NextKeyApp.exe`, register TSF DLL.
2. Load `/extension/` unpacked via `chrome://extensions` → Developer mode → Load unpacked.
3. Paste extension ID shown by Chrome into bridge manifest `allowed_origins`, re-register via Settings → "Register extension ID" helper button.
4. Add `typingclub.com` to excluded domains.
5. Navigate to `typingclub.com` → language-bar icon shows disabled, typing `tieng` produces `tieng`, not `tiếng`.
6. Switch to fresh tab → typing `tieng` produces `tiếng`.
7. Repeat matrix: Edge / Brave / Firefox (after port).

### 9.3 Failure-mode checks (manual)

- Close browser while bridge live → VN resumes on other apps within 30 s.
- Kill `NexusKeyBridge.exe` from Task Manager → slot goes stale → engine ignores after TTL → VN resumes.
- Disable extension in browser → VN resumes immediately.
- Re-enable extension → works after navigating to any tab.

### 9.4 Perf

Added code is one extra `||` per keystroke check + one SharedState seqlock read per foreground change. Expected measurable delta: none. Verify via Debug-build logging of `OnKeyDown` duration before/after.

---

## 10. Implementation Order

Each step should leave the tree green.

1. **`ExclusionValidator` + `DomainMatcher` + unit tests** (Linux-buildable, no Win32). Pure logic, easiest to land first.
2. **`BrowserUrlState` SharedState mapping + seqlock helpers** + extending `SharedStateTest.cpp`.
3. **`NativeMessaging.h` framing helpers + `NativeMessagingFrameTest.cpp`** (Linux-buildable).
4. **`NexusKeyBridge.exe`** — wire stdin loop to SharedState. CMake target. Test manually by piping framed JSON into it.
5. **`TypingConfig` + `ConfigManager` TOML round-trip** + extending `ConfigManagerTest.cpp`.
6. **`HookEngine` integration** — `isExcludedUrl_`, `ReloadUrlForForeground()`, 4 OR insertions, Smart Switch gate.
7. **`EngineController` TSF gate OR.**
8. **Native-messaging host manifest registration** in `NexusKeyApp.exe`.
9. **Sciter `ExcludedDomainsDialog` + UI.**
10. **Classic `ClassicExcludedDomainsDialog` + resources.**
11. **Reference extension in `/extension/`.**
12. **`docs/browser-extension.md`** — install guide, PR guide, failure-mode matrix.
13. **Manual integration test matrix** — Chrome / Edge / Brave.

Firefox port and PR guidance deferred to a follow-up issue.

---

## 11. Summary Table

| Decision | Value |
|---|---|
| Approach | Extension + Native Messaging (not UIA) |
| Distribution | Open-source in `/extension/`, sideload, community PRs |
| Matching | Hostname + implicit subdomain suffix |
| Channel | Chrome Native Messaging, stateless helper `NexusKeyBridge.exe` |
| IPC target | New SharedState mapping `Local\NexusKeyBrowserUrls`, seqlock per slot, 8 slots |
| Engine hook | `isExcludedApp_ \|\| isExcludedUrl_` — 4 OR insertions, no hot-path cost |
| UI | Sciter `ExcludedDomainsDialog` + `ClassicExcludedDomainsDialog`, shared `ExclusionValidator` |
| Failure mode | Fails open (VN allowed) via 30 s stale-slot TTL |
| LoC | ~800 C++ + ~150 JS + ~300 HTML/CSS |
| Tests | 4 new gtest suites (all Linux-buildable), manual matrix in docs |
