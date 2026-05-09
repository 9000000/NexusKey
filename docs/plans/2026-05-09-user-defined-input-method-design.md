# Thêm Kiểu Gõ "Tự Định Nghĩa" (User-defined Input Method)

Kế hoạch này mô tả các bước để xây dựng tính năng "Kiểu gõ Tự định nghĩa" tương tự Unikey, sử dụng giao diện chọn danh sách hành động hợp nhất (Single Dropdown).

---

## 5-Question Pre-Code Gate (CODE_GOVERNANCE.md — bắt buộc)

### Q1 — Layer Check
Thay đổi thuộc **Engine layer** (sửa `IsTelexMode`, thêm enum) + **UI layer** (dialog mới, settings). Không xâm phạm Hook layer hay Output layer. ✅

### Q2 — Performance Impact
- `customKeyMap` lookup: **< 1ns** — array index bằng `static_cast<uint8_t>(lower)`, O(1) constant.
- `IsTelexMode()` thêm 1 phép so sánh: **~0.3ns** thêm. Không ảnh hưởng budget.
- TOML load/save `customKeyMap`: **cold path** (khi mở Settings / đổi config), không chạy trong hook. ✅

### Q3 — Native Alternative
`customKeyMap` dùng `std::array<TypingAction, 128>` (stack-allocated, cache-friendly). Không có giải pháp nhẹ hơn. Rejected: `std::unordered_map` (heap allocation, hash overhead trên hot path). ✅

### Q4 — No-Lock / No-Exception Rule
Không thêm mutex, lock, dynamic allocation, hay try/catch nào trên hot path. `customKeyMap` đã có sẵn trong `TypingConfig` (copied via RCU atomic swap — existing pattern). ✅

### Q5 — Trade-off
- **Gain**: Người dùng tự gán phím theo ý muốn, tương thích Unikey workflow.
- **Give up**: Thêm ~200 dòng code C++ (engine + config) + 3 file UI mới cho mỗi phiên bản (Sciter + Classic). Tăng nhẹ config file size (~128 bytes TOML section). Không ảnh hưởng performance hay stability.

---

## Đánh Giá Kỹ Thuật (Code Review)

Sau khi rà soát kỹ toàn bộ codebase, mình phát hiện **5 vấn đề nghiêm trọng** mà bản plan ban đầu chưa xử lý và **3 điểm cải tiến nhỏ** cần bổ sung. Nếu bỏ sót, feature sẽ compile OK nhưng **sẽ hỏng lúc runtime**.

### Vấn đề 1: `IsTelexMode()` / `IsVniMode()` sẽ hỏng logic

> [!CAUTION]
> **Đây là lỗi nghiêm trọng nhất.** Hiện tại engine dùng 2 hàm helper để quyết định luồng xử lý phím:
> ```cpp
> bool IsTelexMode() const { return config_.inputMethod != InputMethod::VNI; }
> bool IsVniMode()   const { return config_.inputMethod == InputMethod::VNI
>                                || config_.inputMethod == InputMethod::Combined; }
> ```
> Nếu thêm `UserDefined = 4` mà **không sửa 2 hàm này**, thì khi chọn UserDefined:
> - `IsTelexMode()` = **true** (vì != VNI) → Telex modifiers (aa, ee, oo, w, dd, [], s/f/r/x/j/z) sẽ **vẫn hoạt động song song** với customKeyMap.
> - `IsVniMode()` = **false** → VNI digit modifiers sẽ bị tắt.
>
> **Kết quả**: UserDefined sẽ không bao giờ thực sự "tự định nghĩa" vì Telex rules luôn chạy đè lên.

**Giải pháp**: Sửa `IsTelexMode()`:
```cpp
bool IsTelexMode() const {
    return config_.inputMethod != InputMethod::VNI
        && config_.inputMethod != InputMethod::UserDefined;
}
```

> [!CAUTION]
> **Vấn đề 1b (liên quan): PushChar modifier dispatch sẽ KHÔNG CHẠY!**
>
> Sửa `IsTelexMode()` chỉ giải quyết việc `ClassifyKey()` trả `None`. Nhưng trong `PushChar()`, modifier actions được route qua **2 block có mode gate**:
> ```cpp
> // Dòng 381: CHỈ chạy khi IsTelexMode() == true
> if (IsTelexMode() && IsTelexModifierAction(action)) { ... }
>
> // Dòng 447: CHỈ chạy khi IsVniMode() == true
> if (IsVniMode() && IsVniModifierAction(action)) { ... }
> ```
> Khi `UserDefined`: cả hai gate đều `false` → **mọi modifier action rơi xuống `ProcessChar(c)` (literal)** → feature hỏng!
>
> **Giải pháp**: Mở rộng mode gate để bao gồm UserDefined, **GIỮ NGUYÊN** toàn bộ logic spell-check, English protection, và escape:
> ```cpp
> // Dòng 381: thêm UserDefined vào gate cho Telex-style actions
> bool telexActions = IsTelexMode() || config_.inputMethod == InputMethod::UserDefined;
> if (telexActions && IsTelexModifierAction(action)) { ... }
>
> // Dòng 447: thêm UserDefined vào gate cho VNI-style actions
> bool vniActions = IsVniMode() || config_.inputMethod == InputMethod::UserDefined;
> if (vniActions && IsVniModifierAction(action)) { ... }
> ```
> Bằng cách này:
> - ✅ **Spell check** vẫn hoạt động (gating logic bên trong block không đổi)
> - ✅ **English protection** vẫn hoạt động (`engProt_.bias` check giữ nguyên)
> - ✅ **Escape** (gõ 2 lần huỷ) vẫn hoạt động (`escape_.isEscaped()` check giữ nguyên)
> - ✅ UserDefined chạy **cả hai block** (vì user có thể gán cả Telex-style lẫn VNI-style actions)
> - ✅ Không duplicate code — chỉ thêm `||` vào điều kiện
>
> **Lưu ý**: Các handler mới (`HornOrInsertU`, `UndoAllMarks`, `InsertXxx`) cần thêm vào `ProcessModifier()` switch + tạo helper `IsUserDefinedOnlyAction()` để route qua block riêng (vì chúng không phải Telex cũng không phải VNI).

### Vấn đề 2: SharedState IPC — comment và sync

> [!WARNING]
> `SharedState.inputMethod` (uint8_t, dòng 237 SharedState.h) chấp nhận value 4 OK. Nhưng:
> - Comment ghi `// 0=Telex, 1=VNI, 2=SimpleTelex` → thiếu 3=Combined, 4=UserDefined.
> - `customKeyMap` (128 bytes) **không sync qua SharedState** — chỉ sync qua TOML khi `configGeneration` thay đổi. Flow hiện tại đã đúng, chỉ cần cập nhật comment.

### Vấn đề 3: Per-app Overrides cần hỗ trợ value 4

> [!WARNING]
> `AppOverrideEntry.inputMethod` (int8_t, ConfigManager.h dòng 18) và các dropdown trong `appoverrides.js` (dòng 12-18) + `ClassicAppOverridesDialog` đều thiếu option "Tự định nghĩa". Cần thêm vào cả 2 UI.

### Vấn đề 4: Cần hàm string↔enum cho TOML

> [!IMPORTANT]
> ConfigManager chưa có hàm convert `TypingAction ↔ string`. Cần 2 hàm mới:
> `TypingActionToString()` và `StringToTypingAction()` (dùng switch + string_view).

### Vấn đề 5: CMakeLists.txt — file mới cần thêm vào build

> [!IMPORTANT]
> Các file `.cpp` mới phải thêm vào `NEXUSKEY_SOURCES` / `NEXUSKEY_LITE_SOURCES`. HTML/CSS/JS thêm vào resource pack list.

### Cải tiến phụ 1: Import/Export file `.keymap`
Unikey hỗ trợ "Chọn file" / "Ghi file". Có thể để sprint sau, không block release.

### Vấn đề 6: Escape sẽ KHÔNG hoạt động cho CircumflexA/E/O trong UserDefined

> [!CAUTION]
> **Trước đó plan ghi "escape tự động hoạt động" — SAI!**
>
> `HandleAdjacentCircumflex()` (dòng 652) dùng **key-based matching**:
> ```cpp
> if (last.IsVowel() && last.base == lower) {   // lower = ký tự user gõ
>     if (last.mod == Modifier::Circumflex) {
>         last.mod = Modifier::None;  // ← escape: xoá mũ
>         ProcessChar(c);             // ← thêm ký tự literal
>     }
> }
> ```
> Telex: `a` + `a` → `lower='a'`, `last.base='a'` → match ✅ → â.
> Gõ tiếp `a` → `lower='a'`, `last.base='a'`, `last.mod==Circumflex` → escape ✅ → `aa`.
>
> UserDefined `q→CircumflexA`: `a` + `q` → `lower='q'`, `last.base='a'` → **`'q' != 'a'` → KHÔNG match** ❌.
> Cả **apply lẫn escape đều hỏng** vì cùng condition.
>
> **Giải pháp**: Sửa `HandleAdjacentCircumflex()` — khi UserDefined, match theo **action target** thay vì key:
> ```cpp
> // Helper: map CircumflexA→'a', CircumflexE→'e', CircumflexO→'o'
> constexpr wchar_t ActionToVowel(TypingAction a) {
>     switch (a) {
>         case TypingAction::CircumflexA: return L'a';
>         case TypingAction::CircumflexE: return L'e';
>         case TypingAction::CircumflexO: return L'o';
>         default: return 0;
>     }
> }
>
> // Trong HandleAdjacentCircumflex:
> wchar_t targetBase = (config_.inputMethod == InputMethod::UserDefined)
>     ? ActionToVowel(action)   // action-based (UserDefined)
>     : lower;                   // key-based (Telex — giữ nguyên)
> if (last.IsVowel() && last.base == targetBase) { ... }
> ```
> ⚠️ Escape cũng tự động sửa theo vì dùng cùng condition.
> ⚠️ **Backward scan** (free marking, dòng 701) cũng dùng `it->base == lower` → cần sửa tương tự.
> ⚠️ `HandleStrokeD` (dòng 1040) dùng `FindStrokeDTarget()` — không dựa trên key char → **OK, không cần sửa**.

### Cải tiến phụ 3: `SimpleTelex` guards tự động bị bỏ qua
Có 3 chỗ check `inputMethod == SimpleTelex` trong engine. Với UserDefined, chúng tự động trả `false`. ✅ OK.

---

## Thứ Tự Triển Khai (theo dependency)

> [!IMPORTANT]
> **Làm đúng thứ tự này**, nếu không sẽ compile fail hoặc test fail giữa chừng:
> 1. **Core C++** (TypingConfig → TypingAction → TypingEngine → ConfigManager) — build và chạy unit test trước.
> 2. **Sciter UI** (HTML/CSS/JS → UserDefinedDialog.cpp → SettingsDialog integration → main.cpp) — test UI thủ công.
> 3. **Classic UI** (resource.h → .rc → ClassicUserDefinedDialog → ClassicSettingsDialog) — test UI thủ công.
> 4. **Các file phụ** (appoverrides.js, strings.js, SharedState comment, CMakeLists).

---

## Kế Hoạch Chi Tiết

### 1. Core & Cấu hình (C++)

#### [MODIFY] [TypingConfig.h](file:///home/phatmt/code/NexusKey/src/core/config/TypingConfig.h)
- Thêm `UserDefined = 4` vào enum `InputMethod`.
- ⚠️ Giá trị **phải là 4** (3 = Combined đã tồn tại). Đừng dùng số khác.

#### [MODIFY] [TypingEngine.h](file:///home/phatmt/code/NexusKey/src/core/engine/TypingEngine.h)
- Sửa `IsTelexMode()`: thêm `&& config_.inputMethod != InputMethod::UserDefined`.

#### [MODIFY] [TypingEngine.cpp](file:///home/phatmt/code/NexusKey/src/core/engine/TypingEngine.cpp)
3 chỗ sửa:
1. **Dòng 381**: Mở rộng Telex modifier gate — `(IsTelexMode() || config_.inputMethod == InputMethod::UserDefined)`.
2. **Dòng 447**: Mở rộng VNI modifier gate — `(IsVniMode() || config_.inputMethod == InputMethod::UserDefined)`.
3. **`ProcessModifier()`** (dòng 582): Thêm handler cho actions mới (`HornOrInsertU`, `HornOrInsertUNoStart`, `UndoAllMarks`, `InsertXxx`).
- ⚠️ **Không bypass** spell check / English protection / escape — giữ nguyên toàn bộ guard logic hiện có.

#### [MODIFY] [TypingAction.h](file:///home/phatmt/code/NexusKey/src/core/engine/TypingAction.h)
- Thêm `TypingActionToString()` và `StringToTypingAction()`.
- Dùng `constexpr` hoặc `inline` (header-only, không cần `.cpp` riêng).
- Tham khảo bảng mapping ở cuối plan cho danh sách đầy đủ string↔enum.

#### [MODIFY] [ConfigManager.h](file:///home/phatmt/code/NexusKey/src/core/config/ConfigManager.h) & [ConfigManager.cpp](file:///home/phatmt/code/NexusKey/src/core/config/ConfigManager.cpp)
- Thêm `LoadCustomKeyMap()` và `SaveCustomKeyMap()`.
- TOML format ví dụ:
  ```toml
  [UserDefinedKeyMap]
  s = "ToneAcute"
  f = "ToneGrave"
  r = "ToneHook"
  d = "StrokeD"
  ```
- Tích hợp vào `LoadFromFile()` (đọc section `[UserDefinedKeyMap]` → fill vào `config.customKeyMap[]`) và `SaveToFile()` (iterate 128 entries, chỉ ghi những entry != `None`).
- ⚠️ Chỉ lưu các entries có action khác `None`. Khi load, init mảng toàn bộ `None` trước rồi ghi đè.

#### [MODIFY] [SharedState.h](file:///home/phatmt/code/NexusKey/src/core/ipc/SharedState.h)
- Cập nhật comment dòng 237.

#### Thuật toán Nạp Template
- "Nạp Telex": `ClassifyKey(c, true, false)` cho a-z, 0-9, `[`, `]`.
- "Nạp VNI": `ClassifyKey(c, false, true)`.

---

### 2. Giao diện Hiện đại (Sciter - `NexusKey.exe`)

> [!NOTE]
> **Reference implementation**: Copy structure từ [MacroTableDialog.h](file:///home/phatmt/code/NexusKey/src/app/dialogs/MacroTableDialog.h) + [MacroTableDialog.cpp](file:///home/phatmt/code/NexusKey/src/app/dialogs/MacroTableDialog.cpp) và [macro/macro.html](file:///home/phatmt/code/NexusKey/src/app/ui/macro/macro.html). Đây là dialog gần giống nhất với UserDefined (có bảng danh sách, thêm/xoá, import/export).

#### [NEW] `src/app/ui/userdefined/userdefined.html`, `.css`, `.js`

Tuân thủ **SubDialog Checklist** (`docs/subdialog-checklist.md`):
- HTML: Load `theme.css` + `base.css` qua `<link>` (KHÔNG dùng `@import`). Load `utils.js` trước `userdefined.js`. Có `#main-container`, `#btn-close`, `#val-action`.
- CSS: `html, body { background: transparent; }`, `.container { background: var(--bg-glass); }`. Dùng theme tokens, không hardcode màu.
- JS: Gọi `initSubDialog()` trong `document.ready`. Dùng `triggerAction()` pattern. KHÔNG define local `setBackgroundOpacity`.

#### [NEW] `src/app/dialogs/UserDefinedDialog.h` & `.cpp`

Tuân thủ SubDialog checklist:
- Kế thừa `SciterSubDialog`. Constructor:
  ```cpp
  UserDefinedDialog::UserDefinedDialog(HWND parent)
      : SciterSubDialog({
          L"this://app/userdefined/userdefined.html",
          L"NexusKey - User Defined Input",
          460, 550, parent, true, 36, 40, true
      }) {
      // Load current keymap
      auto config = ConfigManager::LoadOrDefault();
      keyMap_ = config.customKeyMap;  // std::array<TypingAction, 128>
      populateList();
  }
  ```
- Override `handle_event()` (KHÔNG dùng `on_event()`).
- Persist pattern (copy từ MacroTableDialog):
  ```cpp
  void UserDefinedDialog::persistAndSignal() {
      auto config = ConfigManager::LoadOrDefault();
      config.customKeyMap = keyMap_;
      (void)ConfigManager::SaveToFile(ConfigManager::GetConfigPath(), config);
      SignalConfigChange();  // ← bump configGeneration → HookEngine reload
  }
  ```
- Load data từ `ConfigManager` trong constructor, gọi `populateList()` sau.
- ⚠️ **KHÔNG** dùng `onBeforeClose()` để save — save mỗi lần thêm/xoá (giống MacroTableDialog). Tránh mất data khi crash.

> [!TIP]
> **Realtime Apply (không cần tắt app)**: Khi user nhấn "Lưu", dialog ghi `customKeyMap` xuống TOML rồi bump `configGeneration`. HookEngine detect thay đổi mỗi phím gõ (trong `QuickSyncFromSharedState()`) → gọi `ReloadFromToml()` → tạo lại engine mới với `customKeyMap` mới. **Phím tiếp theo đã dùng keymap mới ngay**. Flow này đã có sẵn và được dùng bởi tất cả SubDialog hiện tại (Macro, ExcludedApps, AppOverrides).

#### Integration (theo SubDialog checklist)
- **SharedConstants.h**: Thêm `WM_NEXUSKEY_OPEN_USERDEFINED = WM_USER + 115` (giá trị tiếp theo sau 114).
- **SettingsDialog.cpp**: `handleButtonClick(L"btn-userdefined")` → `PostMessage(get_hwnd(), WM_NEXUSKEY_OPEN_USERDEFINED, 0, 0)`. Trong `SubclassProc` → `SpawnSubprocess(L"NexusKey - User Defined Input", "--userdefined")`.
- **main.cpp**: Thêm `--userdefined` command-line check trước main section → `RunUserDefinedSubprocess()` (copy pattern từ `RunMacroSubprocess()`).

#### [MODIFY] [settings.html](file:///home/phatmt/code/NexusKey/src/app/ui/settings/settings.html)
- Thêm option `value="4"` vào `#input-type`.
- Thêm nút ⚙️ **giữa toggle V/E và dropdown**. Ẩn/hiện theo giá trị dropdown.

#### [MODIFY] [SettingsDialog.h](file:///home/phatmt/code/NexusKey/src/app/dialogs/SettingsDialog.h) & `.cpp`
- Thêm SOM function `onOpenUserDefined()`.

#### [MODIFY] [appoverrides.js](file:///home/phatmt/code/NexusKey/src/app/ui/appoverrides/appoverrides.js)
- Thêm `4: "Tự định nghĩa"` vào `inputMethodLabels`.

#### [MODIFY] `src/app/ui/shared/strings.js`
- Bổ sung chuỗi i18n.

---

### 3. Giao diện Classic (Win32 - `NexusKeyLite.exe`)

> [!NOTE]
> **Reference implementation**: Copy structure từ [ClassicMacroTableDialog.h](file:///home/phatmt/code/NexusKey/src/app/classic/ClassicMacroTableDialog.h) + [ClassicMacroTableDialog.cpp](file:///home/phatmt/code/NexusKey/src/app/classic/ClassicMacroTableDialog.cpp). Đây là Classic dialog gần giống nhất.

#### [MODIFY] [resource.h](file:///home/phatmt/code/NexusKey/src/app/classic/resource.h) & [NexusKeyLite.rc](file:///home/phatmt/code/NexusKey/src/app/classic/NexusKeyLite.rc)
- Thêm `IDD_USER_DEFINED_DIALOG` và các control IDs (`IDC_COMBO_ACTION`, `IDC_EDIT_KEY`, `IDC_LIST_KEYMAP`, `IDC_BTN_ADD`, `IDC_BTN_DELETE`, `IDC_BTN_LOAD_TELEX`, `IDC_BTN_LOAD_VNI`).
- Thêm dialog template cho UserDefined.
- Thêm nút `...` (ví dụ `IDC_BTN_CUSTOM_KEYMAP`) vào Settings dialog `.rc`, bên phải ComboBox kiểu gõ.

#### [NEW] `src/app/classic/ClassicUserDefinedDialog.h` & `.cpp`
- Kế thừa `ClassicDialogBase` hoặc dùng `DialogBoxParam` trực tiếp.
- `OnInitDialog`: fill ComboBox action với `TypingActionToString()`, load `customKeyMap` từ ConfigManager vào ListBox.
- `OnCommand`: xử lý Add/Delete/Load Template.
- Save: gọi `ConfigManager::SaveToFile()` rồi `SignalConfigChange()` — pattern giống Classic version khác.

#### [MODIFY] [ClassicSettingsDialog.cpp](file:///home/phatmt/code/NexusKey/src/app/classic/ClassicSettingsDialog.cpp)
- Thêm "Tự định nghĩa" vào ComboBox kiểu gõ (index 4).
- Trong `CBN_SELCHANGE` handler: Enable/Disable nút `...` theo giá trị chọn.
- Trong `BN_CLICKED` handler cho `IDC_BTN_CUSTOM_KEYMAP`: mở `ClassicUserDefinedDialog`.

#### [MODIFY] [ClassicAppOverridesDialog.cpp](file:///home/phatmt/code/NexusKey/src/app/classic/ClassicAppOverridesDialog.cpp)
- Thêm option "Tự định nghĩa" vào dropdown Input Method (giá trị 4).

---

### 4. Build System

#### [MODIFY] [CMakeLists.txt](file:///home/phatmt/code/NexusKey/CMakeLists.txt)
- Thêm `.cpp` files vào source lists. Thêm resources vào pack list.

---

## Danh sách Hành động (TypingAction Mapping)

### So sánh với Unikey

Đối chiếu với dropdown của Unikey, phân tích từng hành động:

| # | Unikey | NexusKey TypingAction | Trạng thái |
|---|---|---|---|
| 1 | Xoá dấu | `ClearTone` | ✅ Có |
| 2 | Dấu Sắc ' | `ToneAcute` | ✅ Có |
| 3 | Dấu Huyền ` | `ToneGrave` | ✅ Có |
| 4 | Dấu Hỏi ? | `ToneHook` | ✅ Có |
| 5 | Dấu Ngã ~ | `ToneTilde` | ✅ Có |
| 6 | Dấu Nặng . | `ToneDot` | ✅ Có |
| 7 | Dấu mũ chung cho a,e,o → â,ê,ô | `VniCircumflex` | ✅ Có |
| 8 | Dấu mũ cho a → â | `CircumflexA` | ✅ Có |
| 9 | Dấu mũ cho e → ê | `CircumflexE` | ✅ Có |
| 10 | Dấu mũ cho o → ô | `CircumflexO` | ✅ Có |
| 11 | Dấu móc cho a,u,o → ă,ư,ơ | `HornW` | ✅ Có (HornW xử lý cả 3: u→ư, o→ơ, a→ă fallback) |
| 12 | Dấu móc cho uo → ươ | `HornW` | ✅ Có (HornW P2 xử lý uo→ươ pair) |
| 13 | Dấu móc cho u → ư | `HornInsertU` | ✅ Có |
| 14 | Dấu móc cho o → ơ | `HornInsertO` | ✅ Có |
| 15 | Dấu móc cho a → ă | `VniBreve` | ✅ Có (VniBreve tìm a gần nhất → ă) |
| 16 | Dấu gạch d → đ | `StrokeD` | ✅ Có |
| 17 | Dấu móc a,u,o **hoặc là chữ ư** | — | ⚠️ **THIẾU** |
| 18 | Dấu móc a,u,o **hoặc chữ ư trừ đầu từ** | — | ⚠️ **THIẾU** |
| 19 | Thoát bỏ dấu | — | ⚠️ **THIẾU** |
| 20-33 | Chữ ă, Ă, â, Â, đ, Đ, ê, Ê, ô, Ô, ơ, Ơ, ư, Ư | — | ⚠️ **THIẾU** |

### Enum `TypingAction` cần bổ sung

> [!WARNING]
> Cần thêm các enum values sau. Mỗi cái cần handler mới trong `ProcessModifier()`.

```cpp
enum class TypingAction : uint8_t {
    // ... existing 17 values (None → VniStroke) ...

    // -- Mới: Unikey-compatible actions --
    HornOrInsertU,           // Móc a,u,o → ă,ư,ơ; fallback: chèn ư nếu không match
    HornOrInsertUNoStart,    // Giống trên, nhưng KHÔNG chèn ư ở đầu từ
    UndoAllMarks,            // Thoát bỏ dấu (xoá tất cả modifier + tone)

    // -- Mới: Chèn ký tự trực tiếp --
    InsertABreve,            // Chữ ă
    InsertABreveUpper,       // Chữ Ă
    InsertACircumflex,       // Chữ â
    InsertACircumflexUpper,  // Chữ Â
    InsertDStroke,           // Chữ đ
    InsertDStrokeUpper,      // Chữ Đ
    InsertECircumflex,       // Chữ ê
    InsertECircumflexUpper,  // Chữ Ê
    InsertOCircumflex,       // Chữ ô
    InsertOCircumflexUpper,  // Chữ Ô
    InsertOHorn,             // Chữ ơ
    InsertOHornUpper,        // Chữ Ơ
    InsertUHorn,             // Chữ ư
    InsertUHornUpper,        // Chữ Ư
};
// Tổng: 17 + 3 + 14 = 34 values. Fit trong uint8_t (max 255). ✅
```

### Bảng UI hoàn chỉnh (theo nhóm `<optgroup>`)

#### Nhóm 1: Dấu thanh

| TypingAction | Nhãn UI |
|---|---|
| `ClearTone` | Xoá dấu |
| `ToneAcute` | Dấu Sắc |
| `ToneGrave` | Dấu Huyền |
| `ToneHook` | Dấu Hỏi |
| `ToneTilde` | Dấu Ngã |
| `ToneDot` | Dấu Nặng |

#### Nhóm 2: Dấu mũ

| TypingAction | Nhãn UI |
|---|---|
| `VniCircumflex` | Mũ chung cho a,e,o → â,ê,ô |
| `CircumflexA` | Mũ cho a → â |
| `CircumflexE` | Mũ cho e → ê |
| `CircumflexO` | Mũ cho o → ô |

#### Nhóm 3: Dấu móc / trăng

| TypingAction | Nhãn UI |
|---|---|
| `HornW` | Móc cho a,u,o → ă,ư,ơ |
| `HornInsertU` | Móc cho u → ư |
| `HornInsertO` | Móc cho o → ơ |
| `VniBreve` | Móc cho a → ă |
| `HornOrInsertU` | Móc cho a,u,o hoặc là chữ ư **(MỚI)** |
| `HornOrInsertUNoStart` | Móc cho a,u,o hoặc chữ ư trừ đầu từ **(MỚI)** |

#### Nhóm 4: Dấu gạch

| TypingAction | Nhãn UI |
|---|---|
| `StrokeD` | Gạch d → đ |
| `VniStroke` | Gạch chung (VNI 9) |

#### Nhóm 5: Đặc biệt

| TypingAction | Nhãn UI |
|---|---|
| `UndoAllMarks` | Thoát bỏ dấu **(MỚI)** |

#### Nhóm 6: Chèn chữ trực tiếp

| TypingAction | Nhãn UI |
|---|---|
| `InsertABreve` / `InsertABreveUpper` | Chữ ă / Ă **(MỚI)** |
| `InsertACircumflex` / `InsertACircumflexUpper` | Chữ â / Â **(MỚI)** |
| `InsertDStroke` / `InsertDStrokeUpper` | Chữ đ / Đ **(MỚI)** |
| `InsertECircumflex` / `InsertECircumflexUpper` | Chữ ê / Ê **(MỚI)** |
| `InsertOCircumflex` / `InsertOCircumflexUpper` | Chữ ô / Ô **(MỚI)** |
| `InsertOHorn` / `InsertOHornUpper` | Chữ ơ / Ơ **(MỚI)** |
| `InsertUHorn` / `InsertUHornUpper` | Chữ ư / Ư **(MỚI)** |

---

## Verification Plan

1. **Unit test**: `TypingEngine` với `UserDefined` + customKeyMap `q→ToneAcute`. Gõ `aq` → `á`. Gõ `s` → literal `s` (không phải dấu sắc).
2. **Unit test escape**: customKeyMap `s→ToneAcute`. Gõ `as` → `á`, gõ tiếp `s` → `as` (escape hoạt động).
3. **Regression**: Chạy toàn bộ test corpus (sustained.toml, chaos) với Telex/VNI → pass 100%. Zero regression.
4. **Sciter UI**: Settings → "Tự định nghĩa" → ⚙️ → Dialog → Nạp Telex → sửa phím → lưu → gõ thử → phím mới hoạt động ngay.
5. **Classic UI**: Tương tự bước 4 với NexusKeyLite và nút `...`.
6. **Per-app override**: Gán app dùng "Tự định nghĩa" trong AppOverrides → chuyển sang app đó → kiểm tra engine dùng đúng customKeyMap.
7. **Persistence**: Thoát app → mở lại → customKeyMap giữ nguyên trên cả UI lẫn engine.
8. **Mode switching**: Đổi từ UserDefined sang Telex → các phím Telex chuẩn hoạt động. Đổi ngược lại → customKeyMap hoạt động. Không bị "dính" config.

---

## Common Pitfalls (Lỗi thường gặp khi triển khai)

| # | Lỗi | Hậu quả | Cách phòng |
|---|---|---|---|
| 1 | Quên sửa `IsTelexMode()` | Telex rules chạy đè lên customKeyMap | Đọc kỹ Vấn đề 1 ở trên |
| 2 | Dùng `@import url()` trong CSS | Sciter không load được theme → dialog trắng | Dùng `<link rel="stylesheet">` |
| 3 | Override `on_event()` thay vì `handle_event()` | Events không bao giờ fire | Sciter `window::handle_event` skip `on_event()` |
| 4 | Quên `el.set_value(sciter::value(L""))` sau handle action | Action chỉ fire 1 lần, lần sau bị skip | Clear action value mỗi lần xử lý |
| 5 | Call `call_function()` trong constructor | JS chưa load → crash/no-op | OK sau `populateList()` — HTML đã load |
| 6 | Lưu toàn bộ 128 entries vào TOML (kể cả `None`) | File config phình to, khó đọc | Chỉ ghi entries có action ≠ `None` |
| 7 | Quên thêm file `.cpp` vào CMakeLists | Linker error: undefined reference | Kiểm tra build sau khi thêm file |
| 8 | Hardcode template Telex/VNI trên UI | Bị lệch với engine nếu engine update | Dùng `ClassifyKey()` để sinh template |
| 9 | Quên `SignalConfigChange()` sau save | Config lưu TOML nhưng engine không reload | Luôn gọi sau `SaveToFile()` |
| 10 | Dùng `onBeforeClose()` để save lần đầu | Mất data nếu app crash trước khi đóng dialog | Save mỗi lần thêm/xoá (pattern `persistAndSignal`) |
