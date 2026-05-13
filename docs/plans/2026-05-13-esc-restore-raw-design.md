# ESC Restore Raw Keys — Design

**Date**: 2026-05-13
**Author**: brainstorm session (PhatMT + AI assistant)
**Status**: Design approved, ready for implementation plan

---

## 1. Problem

User gõ một từ tiếng Việt (vd `v-i-r-u-s` → composed `víu` + buffer còn ký tự thừa), nhận ra đó là từ tiếng Anh. Hiện tại không có cách nhanh để khôi phục về raw keys đã gõ (`virus`). Phải xoá từng phím rồi gõ lại — lặp lại nhiều lần trong ngày.

Đây là tính năng chuẩn của các IME tiếng Việt khác (Unikey, EVKey, GoTiengViet).

## 2. Behavior contract

**Toggle**: `escRestoreRawEnabled` trong Settings → Bảng gõ. **Default OFF** (opt-in, advanced).

**Trigger**: phím `VK_ESCAPE` (down), không kèm modifier (Ctrl/Alt/Win/Shift).

**Kích hoạt khi cả 3 đúng**:
1. `escRestoreRawEnabled == true`.
2. Engine ở Vietnamese mode.
3. Engine có composition non-empty (`engine_->Count() > 0`).

**Hành động khi kích hoạt**:
1. Lấy raw keys từ engine (chuỗi user đã gõ, bảo toàn case).
2. Backspace composed text khỏi app.
3. Inject raw text vào app.
4. Reset engine state, end composition.
5. Eat phím ESC (app không thấy).

**Hành động khi KHÔNG kích hoạt**:
- Buffer rỗng / config OFF / English mode → pass-through ESC y nguyên cho app.
- Logic `tempOffMacroEsc` hiện có vẫn chạy bình thường (orthogonal — chỉ hoạt động khi buffer rỗng).

**Edge cases**:
- Raw == composed (chưa transform, vd `v-i`): vẫn commit literal "vi" + end composition + eat ESC. Đơn giản hơn so với phân biệt có/không transform.
- Password / excluded app / TSF readonly: IME không xử lý → ESC pass-through tự nhiên.
- Case: rawInput đã giữ case gốc (`V-I-R-U-S` → "VIRUS").
- Quick-start consonant (`f`→`ph`): rawInput giữ `f`, sau ESC ra `f` (không phải `ph`) — vì đó là phím user thực sự gõ.

## 3. Architecture

### Engine layer (`src/core/engine/`)

`TypingEngine`:
- Hạ tầng đã sẵn: `std::vector<wchar_t> rawInput_` track raw keys cho tone-escape feature hiện có.
- Thêm API mới:
  - `[[nodiscard]] std::wstring PeekRaw() const` — copy `rawInput_` thành wstring, không mutate state.
  - `Reset()` (đã có) — tách riêng. Caller gọi `PeekRaw()` trước, nếu output dispatch thành công mới gọi `Reset()`.

`IInputEngine`:
- Thêm `virtual std::wstring PeekRaw() const { return L""; }` — default no-op cho các engine khác.

**Lý do tách `PeekRaw()` + `Reset()` riêng**: nếu output dispatch (edit session/SendInput) fail giữa chừng, engine state chưa bị xoá → fallback an toàn về flow cũ.

### Config layer (`src/core/config/`, `src/core/ipc/`)

7-bước theo `docs/CODING_RULES/5-struct-versioning.md`:

1. `FeatureFlags::ESC_RESTORE_RAW = 1u << N` trong `SharedState.h` (N = bit chưa dùng — check `extFeatureFlags` còn ≥ 1 bit).
2. `bool escRestoreRawEnabled = false` trong `TypingConfig.h`.
3. Encode/Decode trong `EncodeFeatureFlags`/`DecodeFeatureFlags` (`SharedState.h`).
4. Load/save TOML key `esc-restore-raw` trong `ConfigManager.cpp`.
5. UI: SettingMetadata entry (xem section 5) + i18n strings.
6. ApplyConfig line trong `HookEngine.cpp` (atomic store).
7. **⚠️ Field copy trong `SharedStateManager::Write()`** — Debug pass, Release fail nếu quên.

### TSF DLL (`src/tsf/`)

`KeyEventSink.cpp` — branch mới trong `OnKeyDown` cho `VK_ESCAPE`:
```
if (escRestoreRawEnabled && vnMode && engineController_->HasComposition()
    && !ctrl && !alt && !win && !shift) {
    if (engineController_->CommitRawAndEnd(pContext)) {
        *pfEaten = TRUE;
        return S_OK;
    }
    // fall through nếu fail → hành vi cũ
}
```

`EngineController::CommitRawAndEnd(ITfContext*)`:
1. `auto raw = engine_->PeekRaw()`; nếu rỗng → return false.
2. Request edit session `TF_ES_SYNC | TF_ES_READWRITE`.
3. Trong edit session: replace composition range với `raw`, end composition.
4. Edit session thành công → `engine_->Reset()`, return true.
5. Fail → log, return false (giữ state).

### Hook engine (`src/app/system/`)

`HookEngine.cpp` — branch mới đặt **trước** `tempOffMacroEsc` branch hiện có (line 1261). Sau early-return hierarchy (synthetic / nCode<0 / sending_ / modifier check).

```
if (escRestoreRawEnabled_.load(std::memory_order_acquire)
    && vkCode == VK_ESCAPE
    && !ctrl && !alt && !win && !shift
    && engine_->Count() > 0) {
    auto raw = engine_->PeekRaw();
    size_t bsCount = engine_->Count();  // displayed chars
    outputInjector_->Replace(bsCount, raw);  // backspace + inject via IOutputInjector
    engine_->Reset();
    return KeyOutcome::Eat;
}
```

Dùng `IOutputInjector` plugin layer có sẵn → tự động đúng cho Electron / Console / RichEditD2DPT / Win32 hosts. Không sleep trong hook body (rule 11.6).

## 4. Performance gates (per PHILOSOPHY.md + rule 11)

| Pillar | Impact |
|---|---|
| Nhanh | Hot-path zero-cost khi config OFF (1 atomic load). Khi ON, code chỉ chạy khi user nhấn ESC + buffer non-empty (rất hiếm). |
| Nhẹ | 1 bit SharedState + 1 bool TypingConfig + wstring copy < 10 chars. |
| Mượt | Output dispatch qua IOutputInjector đã được test cho 4 host classes (baseline `chaos.toml`). |
| Mở rộng | Feature gated OFF mặc định — user không bật không trả phí. |

**Code Governance Q1–Q5**:
- Q1 Layer: engine + TSF + Hook (đúng layer, không xuyên).
- Q2 Perf: < 1µs khi OFF; khi ON kích hoạt cost = SendInput (user tự nhấn ESC, chấp nhận được).
- Q3 Native: không API mới, dùng IOutputInjector có sẵn.
- Q4 No-lock: atomic config read.
- Q5 Trade-off: 1 bit SharedState (kiểm tra còn chỗ) vs convenience cho power user.

## 5. UI placement

**Tab**: Bảng gõ (Tab 0) — left column (col 0).

**Vị trí**: sau `allow-english-bypass` ("Gõ tự do"), trước `auto-caps` ("Viết hoa chữ cái đầu").

**SettingMetadata entry** (`src/core/config/SettingMetadata.h`):
```cpp
NK_TYPING("esc-restore-raw",     escRestoreRawEnabled,
          "ESC trả lại phím gốc","ESC restores raw keys",
          L"Khi đang gõ, bấm Esc để hủy biến đổi tiếng Việt và giữ nguyên ký tự gốc (vd: víu → virus)",
          L"Press Esc while typing to undo Vietnamese conversion and keep raw keys (e.g. víu → virus)",
                                                            <new-msg-id>, 0, 0),
```

**Hai UI build tự động phủ**:
- Sciter (`NextKeyApp`): toggle render từ kSettings array.
- Classic (`NextKeyLite`): toggle render từ kSettings array qua `meta.win32Id` (cần resource ID mới: `IDC_CHECK_ESC_RESTORE_RAW` trong `src/app/classic/resource.h`).

## 6. Test plan

**Test-first** (per `project_test_first` memory): viết test FAIL trước khi sửa engine.

Tests trong `tests/TelexEngineTest.cpp` (Linux build):

```
EscRestoreRaw_BasicVirus           // "v,i,r,u,s" → PeekRaw == L"virus", Reset clears
EscRestoreRaw_PreservesCase        // "V,I,R,U,S" → PeekRaw == L"VIRUS"
EscRestoreRaw_MixedCase            // "Vi,RuS" → giữ case từng ký tự
EscRestoreRaw_QuickStartConsonant  // "f" → composed "ph" → PeekRaw == L"f"
EscRestoreRaw_AfterToneEscape      // verify behavior sau khi tone đã escape sẵn
EscRestoreRaw_EmptyBuffer          // PeekRaw == L"" khi engine rỗng
EscRestoreRaw_ResetIsIdempotent    // 2 lần Reset không crash
```

**Integration smoke** (Win32 manual): bật toggle → restart → gõ `virus` trong Notepad / Chrome / Discord / Notepad++ / VS Code → ESC → verify thấy "virus".

**Regression**: verify hành vi cũ vẫn nguyên khi toggle OFF (default), đặc biệt `tempOffMacroEsc` (ESC empty buffer + macro feature).

## 7. File checklist

| File | Change |
|---|---|
| `src/core/engine/TypingEngine.h` | Khai báo `PeekRaw()` |
| `src/core/engine/TypingEngine.cpp` | Implement `PeekRaw()` |
| `src/core/engine/IInputEngine.h` | Virtual `PeekRaw()` default no-op |
| `src/core/config/TypingConfig.h` | `bool escRestoreRawEnabled = false` |
| `src/core/config/SettingMetadata.h` | Entry mới sau `allow-english-bypass` |
| `src/core/ipc/SharedState.h` | `FeatureFlags::ESC_RESTORE_RAW` + encode/decode |
| `src/core/ipc/SharedStateManager.cpp` | **Write() field copy (step 7)** |
| `src/core/config/ConfigManager.cpp` | TOML load/save |
| `src/tsf/EngineController.{h,cpp}` | `CommitRawAndEnd()` |
| `src/tsf/KeyEventSink.cpp` | VK_ESCAPE branch |
| `src/app/system/HookEngine.cpp` | ESC branch trước `tempOffMacroEsc` + ApplyConfig |
| `src/app/dialogs/SettingsDialog.cpp` | Sciter UI binding (auto via kSettings) |
| `src/app/ui/settings/settings.html` + `.js` | Toggle markup nếu cần |
| `src/app/ui/shared/strings.js` | i18n VN/EN string |
| `src/core/Strings.cpp` | C++ i18n strings |
| `src/app/classic/resource.h` | `IDC_CHECK_ESC_RESTORE_RAW` |
| `src/app/classic/NexusKeyLite.rc` | Dialog template (nếu cần row mới) |
| `src/app/classic/ClassicSettingsDialog.cpp` | Verify auto-render qua kSettings |
| `tests/TelexEngineTest.cpp` | 7 unit tests (test-first) |

## 8. Open questions trước implementation

1. Bit còn trống trong `extFeatureFlags` (max 24)? Cần verify.
2. Message ID kế tiếp chưa dùng (2206/2207/2209)? Cần grep `Strings.cpp` + `strings.js`.
3. Quick-start consonant: verify thực tế `rawInput_` chứa gì khi `f` expand thành `ph` — đọc code TypingEngine.cpp section "Quick start consonant".
4. Hook mode: `IOutputInjector::Replace(bsCount, text)` có signature chính xác như giả định? Verify khi viết code.

---

## 9. Out of scope

- Force-English-mode-cho-word (option C của Q1) — không làm.
- Toast notification khi ESC fire — không làm, silent.
- Multi-word undo / undo stack — engine chỉ buffer 1 word.
- Macro escape (đã có `tempOffMacroEsc` riêng).
- Auto-detect English word + tự revert — đã có `EnglishProtection` heuristics riêng.
