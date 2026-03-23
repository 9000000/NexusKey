# Session Fixes — 2026-03-23

## 1. Fix: l-u-u-w → "luư" thay vì "lưu" [TelexEngine]

**File**: `src/core/engine/TelexEngine.cpp` — `ApplyW()`
**Test**: `TelexEngineTest.W_UU_HornsFirstU`, `W_UU_NoConsonant`, `W_UU_WithTone`

**Root cause**: Khi có 2 chữ `u` liên tiếp, `uIdx` luôn trỏ vào chữ `u` CUỐI (vòng lặp cập nhật liên tục). P5 áp horn vào chữ cuối → "luư" sai.

**Fix**: Track thêm `firstUIdx` (chữ `u` đầu tiên chưa có modifier). Detect pattern `hasUU` (2 chữ u liên tiếp). P5 dùng `firstUIdx` khi `hasUU=true`:
```cpp
size_t targetU = (hasUU && firstUIdx != SIZE_MAX) ? firstUIdx : uIdx;
states_[targetU].mod = Modifier::Horn;
```

**Lý do**: Trong tiếng Việt, "uu+w" = "ưu" (horn trên chữ u đầu = âm hạt nhân, chữ u sau = âm đệm cuối). Ví dụ: lưu, cưu, hưu, tưu.

---

## 2. Fix: Char swallowing trong Claude CLI [HookEngine]

**File**: `src/app/system/HookEngine.cpp` — `HandleAlphaKey()` passthrough condition

**Vấn đề**: Commit `234d432` đã tắt passthrough cho cả console apps (`!skipEmptyChar_`), khiến MỌI keystroke trong terminal đều đi qua synthetic SendInput. Điều này tạo overhead không cần thiết và có thể gây swallowing.

**Fix**: Revert về `!isElectronApp_` — chỉ tắt passthrough cho Electron/Qt standalone apps:
```cpp
// Trước (234d432): !skipEmptyChar_   — tắt cho cả Electron + console
// Sau:             !isElectronApp_   — chỉ tắt cho Electron/Qt
if (!autoCapped && currentCodeTable_ == CodeTable::Unicode &&
    !hadSynthInWord_ && !isElectronApp_ && ...)
```

**Lý do**: Console apps (Windows Terminal) dùng single FIFO input queue — physical passthrough và synthetic corrections luôn đúng thứ tự, không có race condition. Electron dùng multi-process architecture (browser + renderer), physical và synthetic đi 2 path riêng → cần tắt passthrough.

**Vẫn còn**: `hadSynthInWord_` đảm bảo sau khi có correction đầu tiên trong word, tất cả char sau đó dùng synthetic (không mix physical+synthetic trong cùng word).

---

## 3. Fix: Sleep(2) → Sleep(15) cho console corrections [HookEngine]

**File**: `src/app/system/HookEngine.cpp` — `ReplaceComposition()`
**Lines**: ~1480 (non-Unicode path) và ~1560 (Unicode fast path)

**Lý do**: Windows timer resolution là ~15.6ms. `Sleep(2)` không được đảm bảo — có thể sleep thực tế lâu hơn hoặc không đủ thời gian cho Node.js libuv xử lý BS events. Đặc biệt tệ khi cold boot (V8 JIT chưa warm, Node.js event loop chậm hơn).

**Lưu ý**: Sleep chỉ fire khi có backspace thực sự (correction), không phải mỗi keystroke. Cost: 15ms/correction (chấp nhận được).

**Nếu vẫn còn swallowing**: Tăng lên Sleep(30) hoặc Sleep(50). Nếu Sleep không đủ → cần async sending architecture (worker thread cho SendInput, không sleep trong hook callback).

---

## 4. Thảo luận (CHƯA implement): English protection cho "approved"

**Vấn đề**: Khi tắt spellcheck để gõ chữ viết tắt tiếng Việt (mtrường, đc, k...), các từ tiếng Anh như "approved" bị lỗi vì:
- "app" + phím `r` (dấu hỏi Telex) → engine áp dấu hỏi vào 'a' → "ảpp"
- Spellcheck là "backstop" duy nhất biết "app" không phải tiếng Việt → khi tắt spellcheck, không còn backstop

**Giải pháp đề xuất (chưa code)**: Thêm rule vào `CheckEnglishBias` (EnglishProtection.h):

> **Coda structure rule**: Tiếng Việt không bao giờ có 2+ phụ âm liên tiếp sau nguyên âm trừ các tổ hợp: ch, ng, nh. Nếu vi phạm → `bias = HardEnglish` → chặn tất cả phím dấu.

```
"app" → pp sau a → KHÔNG hợp lệ → HardEnglish → chặn 'r' dấu ✓
"anh" → nh sau a → hợp lệ → cho qua ✓
"ang" → ng sau a → hợp lệ → cho qua ✓
"amp" → mp sau a → KHÔNG hợp lệ → HardEnglish ✓
```

**Tại sao tách khỏi spellcheck**: Spellcheck dùng từ điển (Responsibility A: "có phải từ tiếng Việt không"). Coda rule là structural check (Responsibility B: "có phải cấu trúc âm tiết tiếng Việt hợp lệ không"). Hai cái này nên độc lập — user tắt A vẫn giữ được B.

**File cần sửa**: `src/core/engine/EnglishProtection.h` — hàm `CheckEnglishBias()`
