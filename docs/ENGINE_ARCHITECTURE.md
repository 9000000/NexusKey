# Kiến trúc Engine Xử lý Tiếng Việt trong VKey

Tài liệu này mô tả chi tiết kiến trúc tổng quan, mô hình Engine kép (Dual-Engine), cơ chế nạp động an toàn (Dynamic Loading), giao diện C ABI và mô hình bảo mật ngoại tuyến (100% Offline) của **VKey**.

---

## 1. Tóm tắt Nhanh (Dành cho Người dùng)

Để xử lý ký tự người dùng gõ từ bàn phím thành chữ tiếng Việt có dấu (ví dụ: gõ `v` `i` `e` `t` `s` thành `việt`), VKey tách biệt làm 2 phần:

1. **Hạ tầng tiếp nhận phím (VKey Shell)**: Đảm nhận việc hứng phím từ hệ điều hành Windows (thông qua TSF hoặc Hook), hiển thị giao diện và gửi chuỗi phím tới Engine xử lý.
2. **Bộ xử lý ngôn ngữ (Typing Engine)**: Kiểm tra các quy tắc chính tả tiếng Việt, tính toán vị trí đặt dấu thanh và trả lại kết quả chuỗi ký tự đã bỏ dấu.

VKey hỗ trợ **mô hình Engine kép**:
- **Engine C++ (Tích hợp sẵn)**: Là engine mặc định, mã nguồn mở 100%, đi kèm sẵn trong ứng dụng. Xử lý đầy đủ các kiểu gõ Telex, VNI và quy tắc chính tả tiêu chuẩn.
- **Engine Rust Nâng cao (Tùy chọn)**: Là thư viện mở rộng (`vkey_engine.dll`), bổ sung tính năng **Kiểm tra Chính tả Nâng cao** giúp tự động sửa lỗi gõ nhanh bị đảo phím (ví dụ: `hcaof` → `chào`), đề xuất từ vựng và xử lý ngữ cảnh nâng cao.

> **Tự động chuyển đổi (Fallback)**: Nếu Engine Rust không có mặt hoặc bị gỡ bỏ, VKey sẽ tự động dùng Engine C++ mặc định. Việc gõ tiếng Việt của bạn luôn đảm bảo thông suốt và không bao giờ bị gián đoạn.

---

## 2. Kiến trúc Tổng quan (System Architecture)

Sơ đồ luồng dữ liệu xử lý phím gõ trong VKey:

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        Hệ điều hành Windows                            │
│                 (Windows TSF IME / Low-Level Hook)                     │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Phím gõ (Virtual Key / Char)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                              VKey Host                                 │
│          (Coordinator / Key Event Pipeline / UI Sciter)                │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Chuỗi phím UTF-16
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   TypingEngine Abstraction Interface                   │
└─────────────────┬──────────────────────────────────┬───────────────────┘
                  │                                  │
      (Bật Advanced Spell Check)              (Mặc định / Fallback)
                  │                                  │
                  ▼                                  ▼
┌──────────────────────────────────┐ ┌──────────────────────────────────┐
│       Rust Engine (C ABI)        │ │        C++ Engine (Built-in)     │
│   (Thư viện tùy chọn: DLL/SO)    │ │      (Mã nguồn mở tích hợp)      │
└──────────────────────────────────┘ └──────────────────────────────────┘
```

### Các thành phần chính:
- **`Coordinator`**: Tiếp nhận sự kiện bàn phím từ TSF (`NextKeyTSF.dll`) hoặc Hook (`NextKeyHook.dll`), điều phối trạng thái gõ và quản lý bộ đệm composition buffer.
- **`ITypingEngine` Interface**: Lớp trừu tượng định nghĩa các hàm xử lý phím chuẩn (`PushChar`, `Backspace`, `Reset`, `PeekText`, `CommitText`).
- **`CppEngine`**: Implementation viết bằng C++20 tích hợp trực tiếp trong mã nguồn VKey.
- **`RustEngineLoader`**: Component chịu trách nhiệm kiểm tra an toàn, nạp động file `vkey_engine.dll` tại runtime và ánh xạ các hàm C ABI sang `ITypingEngine`.

---

## 3. Giao diện C ABI & Nạp Động (C ABI Specification)

Engine Rust được đóng gói dưới dạng thư viện liên kết động (`vkey_engine.dll` trên Windows / `libvkey_engine.so` trên Linux) và giao tiếp với VKey thông qua giao diện **C ABI** chuẩn định nghĩa tại `extern/vkey_engine/include/vkey_engine.h`.

### Đặc điểm thiết kế C ABI:
1. **Truyền nhận UTF-16**: Tất cả chuỗi ký tự đi qua ranh giới ABI đều sử dụng mảng `uint16_t*` (tương thích trực tiếp với `wchar_t` trên Windows), giúp triệt tiêu hoàn toàn chi phí chuyển đổi bảng mã (transcoding).
2. **Không cấp phát bộ nhớ chéo (Zero cross-boundary allocation)**: VKey tự quản lý và cấp phát bộ đệm (caller-allocated buffer), Engine chỉ ghi kết quả vào bộ đệm được truyền sang.
3. **Quản lý con trỏ ẩn (Opaque Handle)**: Engine trả về con trỏ `VKeyEngine*` ẩn state bên trong, đảm bảo an toàn bộ nhớ giữa C++ và Rust.

### Bảng tóm tắt hàm C ABI cốt lõi:

| Hàm C ABI | Chức năng |
| :--- | :--- |
| `vkey_engine_create(method, flags)` | Khởi tạo một instance engine (Telex/VNI) với các cờ tính năng. |
| `vkey_engine_push_char(engine, codepoint)` | Đưa một ký tự Unicode (ASCII key) vào bộ đệm xử lý. |
| `vkey_engine_backspace(engine)` | Xóa ký tự cuối cùng trong bộ đệm đang gõ. |
| `vkey_engine_peek_utf16(engine, buf, cap)` | Đọc chuỗi ký tự tiếng Việt đang soạn thảo (composition). |
| `vkey_engine_commit_utf16(engine, buf, cap)` | Chốt chuỗi ký tự ra ứng dụng đích và xóa bộ đệm engine. |
| `vkey_engine_reset(engine)` | Xóa sạch bộ đệm gõ ngay lập tức. |
| `vkey_engine_destroy(engine)` | Giải phóng bộ nhớ của instance engine. |
| `vkey_engine_runtime_status()` | Kiểm tra tính hợp lệ và tên file thực thi của thư viện tại runtime. |

---

## 4. Mô hình Bảo mật & Kiểm tra An toàn File (Security & Trust Model)

Vì `vkey_engine.dll` là file thư viện động được nạp vào tiến trình `VKeyApp.exe`, VKey áp dụng cơ chế bảo mật nhiều lớp (Defense-in-Depth) để chống lại các nguy cơ đánh tráo file, DLL Hijacking hoặc chỉnh sửa trái phép:

### 1. Ghim mã Hash SHA-256 khi Biên dịch (`engine.lock`)
Thông tin file DLL chính thức (kích thước file chính xác và mã băm SHA-256) được định nghĩa trong file `engine.lock` và được nhúng trực tiếp vào file thực thi `VKeyApp.exe` khi biên dịch.

### 2. Chống tranh chấp dữ liệu & Đánh tráo file (TOCTOU Protection)
Khi kiểm tra file `vkey_engine.dll`:
1. VKey mở file bằng hàm `CreateFileW` với quyền truy cập độc quyền ghi/xóa (`FILE_SHARE_READ`). Việc này khóa chặt file trên ổ cứng, ngăn các tiến trình độc hại khác sửa đổi file trong lúc VKey đang kiểm tra.
2. VKey dùng Windows CNG API (`BCryptHashData`) để tính mã băm SHA-256 của file thực tế trên đĩa.
3. Mã băm được so sánh với mã ghim sẵn trong thời gian cố định (constant-time comparison) để chống tấn công phân tích thời gian (side-channel timing attack).

### 3. Hệ quả: `VKey.exe` và `VKeyTSF.dll` phải cùng một lứa build
Mã băm được ghim **tại thời điểm biên dịch**, nên mọi binary nạp engine (`VKey.exe` cho chế độ Hook, `VKeyTSF.dll` cho chế độ TSF) đều mang bản ghim riêng của lứa build đó. Nếu một file bị bỏ lại ở bản cũ — thường gặp nhất là `VKeyTSF.dll`, vì Windows không cho ghi đè DLL đang được ứng dụng khác nạp nên bước cập nhật phải hoãn sang lần khởi động sau — thì file đó sẽ từ chối `vkey_engine.dll` mới và **tự động quay về Engine C++**, dù engine mới hoàn toàn hợp lệ.

Biểu hiện: tính năng nâng cao (ví dụ `hcaof` → `chào`) biến mất trong các ứng dụng dùng TSF, nhưng vẫn hoạt động ở chế độ Hook. Cách xử lý: **khởi động lại Windows** — lần khởi động kế tiếp sẽ áp bản DLL đang chờ và buộc các tiến trình đang giữ DLL cũ nhả ra.

Để tình trạng này không diễn ra âm thầm, `EngineController` phát cờ `TSF_ENGINE_UNTRUSTED` qua SharedState khi Kiểm tra Chính tả Nâng cao đang bật mà engine không nạp được; Cài đặt và khay hệ thống hiển thị banner đề nghị khởi động lại.

> **Kế hoạch (chưa triển khai)**: thay việc ghim mã băm bằng **xác thực chữ ký số** (ECDSA P-256, khoá công khai nhúng trong binary, chữ ký rời `vkey_engine.dll.sig` kèm số thứ tự bản phát hành để chặn hạ cấp). Chữ ký ghim *người phát hành* thay vì *một file cụ thể*, nên binary lứa cũ vẫn nạp được engine mới và ràng buộc cùng lứa build ở trên biến mất.

### 4. Nạp an toàn & Định danh File (File Identity Binding)
1. Hàm `LoadLibraryExW` chỉ nạp DLL từ thư mục ứng dụng hoặc `System32` (`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`), loại bỏ hoàn toàn nguy cơ tấn công qua đường dẫn `PATH` hoặc CWD.
2. Sau khi nạp, VKey kiểm tra chỉ số định danh file trên đĩa (`dwVolumeSerialNumber`, `nFileIndexHigh`, `nFileIndexLow`) thông qua `GetFileInformationByHandle` để đảm bảo Windows đã nạp đúng file đã được xác thực SHA-256 trước đó.

---

## 5. Quyền riêng tư & Hoạt động Ngoại tuyến (100% Offline Privacy)

Bộ gõ bàn phím là phần mềm nhạy cảm vì tiếp nhận toàn bộ phím gõ của người dùng. VKey được thiết kế tuân thủ nghiêm ngặt nguyên tắc bảo vệ quyền riêng tư:

- **100% Offline (Không kết nối mạng)**: Cả Engine C++ lẫn Engine Rust đều chạy hoàn toàn cục bộ trong bộ nhớ của tiến trình `VKeyApp.exe`. Engine Rust không chứa bất kỳ mã nguồn hay thư viện mạng nào.
- **Tương thích Tường lửa (Firewall Friendly)**: Người dùng có thể chủ động chặn toàn bộ truy cập Internet của file `VKey.exe` bằng Windows Firewall. Việc gõ tiếng Việt và kiểm tra chính tả vẫn hoạt động hoàn hảo 100%.
- **Kiểm chứng mã nguồn (Auditable)**: Toàn bộ mã nguồn tiếp nhận bàn phím, cơ chế kiểm tra SHA-256 và luồng truyền dữ liệu phím gõ của VKey đều được công khai minh bạch trên GitHub để cộng đồng tự kiểm tra.

---

## 6. Tài liệu Liên quan

- **[ENGINE_FAQ.md](ENGINE_FAQ.md)**: Giải đáp các câu hỏi thường gặp về cách sử dụng, cài đặt, gỡ bỏ và giấy phép.
- **`extern/vkey_engine/README.md`**: Tài liệu kỹ thuật chi tiết dành cho lập trình viên muốn tích hợp C ABI.
