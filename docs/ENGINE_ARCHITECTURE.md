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

`vkey_engine.dll` được nạp thẳng vào tiến trình đang gõ (VKey.exe ở chế độ Hook, và cả Chrome/Word... ở chế độ TSF, vì `VKeyTSF.dll` chạy bên trong ứng dụng đó). Một file như vậy mà nạp bừa thì ai đặt được file cùng tên vào thư mục cài là chạy được mã của họ trong mọi ứng dụng anh gõ. Nên trước khi nạp, VKey phải trả lời được: **file này có đúng do dự án phát hành không?**

### 1. Câu trả lời: chữ ký số rời, không phải mã băm cố định

Mỗi bản engine phát hành đi kèm một file `vkey_engine.dll.sig` dài đúng **72 byte**:

```text
abi      u32 little-endian   (4 byte)   phiên bản C ABI
counter  u32 little-endian   (4 byte)   số thứ tự bản phát hành
sig      r || s              (64 byte)  chữ ký ECDSA P-256
```

Chữ ký ký lên chuỗi:

```text
"VKEYENG1"  ||  sha256(vkey_engine.dll)  ||  abi  ||  counter
```

Khoá riêng do dự án giữ (chỉ workflow phát hành của VKey-rs dùng tới). Khoá công khai tương ứng nằm ở `extern/vkey_engine/engine.pub` và được **nhúng thẳng vào `VKey.exe` / `VKeyTSF.dll` lúc biên dịch**.

### 2. Năm bước khi nạp engine

```text
1. Mở vkey_engine.dll với FILE_SHARE_READ   -> khoá không cho ai ghi/xoá trong lúc kiểm
2. Tự tính SHA-256 từ chính handle đang mở  -> không tin bất kỳ con số nào file .sig khai
3. Đọc .sig (phải đúng 72 byte)             -> sai độ dài là loại, không đọc tiếp byte nào
4. abi >= abi của bản build, counter >= floor -> chặn engine cũ hơn
5. ECDSA verify chuỗi ở mục 1 bằng khoá nhúng -> chỉ khoá của dự án mới ký được
```

Sau đó `LoadLibraryExW` nạp file với `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32` (không đi qua `PATH`/CWD), rồi đối chiếu `dwVolumeSerialNumber` + `nFileIndexHigh/Low` để chắc chắn Windows nạp **đúng file vừa được xác thực**, không phải file khác vừa bị tráo vào.

### 3. Vì sao là chữ ký chứ không phải mã băm

Cách cũ ghim mã băm của **một file cụ thể** vào lúc biên dịch. Hệ quả: `VKey.exe` và `VKeyTSF.dll` buộc phải cùng lứa build với engine. Mà Windows không cho ghi đè một DLL đang được ứng dụng khác nạp, nên khi cập nhật, `VKeyTSF.dll` thường bị hoãn thay tới lần khởi động sau — trong lúc đó nó ôm mã băm cũ, từ chối engine mới hoàn toàn hợp lệ, và âm thầm quay về Engine C++. Biểu hiện: `hcaof` → `chào` mất trong Chrome/Word nhưng vẫn chạy ở chế độ Hook.

Chữ ký ghim **người phát hành** thay vì một file, nên một `VKeyTSF.dll` cũ vẫn nạp được engine mới. Ràng buộc cùng lứa build biến mất, và người dùng không phải khởi động lại chỉ để lấy lại tính năng.

Đổi lại, chữ ký thì có giá trị mãi mãi — nên `counter` tồn tại: mỗi bản phát hành tăng một, bản build ghim mức sàn, và engine cũ hơn mức sàn bị từ chối dù chữ ký vẫn hợp lệ. Đó là thứ mà cách ghim mã băm có sẵn miễn phí.

`abi` chuyển từ `==` sang `>=` cũng vì lý do đó: một binary cũ phải nạp được engine mới. An toàn vì mọi symbol nó cần đều được phân giải theo tên lúc nạp, và C ABI của engine chỉ thêm chứ không đổi nghĩa symbol cũ.

### 4. Chặn được gì, không chặn được gì

| Tình huống | Kết quả |
| :--- | :--- |
| Thay engine bằng file khác, tự ký bằng khoá lạ | Từ chối — khoá công khai nhúng sẵn không khớp |
| Giữ chữ ký thật, tráo engine | Từ chối — mã băm trong chuỗi ký không khớp |
| Sửa `abi`/`counter` trong file `.sig` cho qua mức sàn | Từ chối — hai trường đó nằm trong chuỗi đã ký |
| Đắp lại bản engine cũ (chữ ký thật) | Từ chối nếu `counter` dưới mức sàn |
| Tráo file giữa lúc kiểm và lúc nạp | Từ chối — khoá ghi/xoá khi mở, và đối chiếu định danh file sau khi nạp |
| **Sửa thẳng `VKeyTSF.dll` / `VKey.exe`** | **Không chặn được** |
| **Lộ khoá riêng** | **Không chặn được** cho tới khi phát hành khoá công khai mới |

Hai dòng cuối là giới hạn thật của mô hình: kẻ ghi được vào thư mục cài cũng ghi được lên chính phần đi kiểm tra. Cách ghim mã băm trước đây cũng vậy, nên đây không phải bước lùi.

### 5. `engine.lock` giờ còn dùng để làm gì

Vẫn còn, nhưng hạ vai trò:

- **Bản build Debug** chấp nhận thêm engine khớp mã băm trong lock. Bản engine dựng tại máy không có chữ ký (khoá nằm ở workflow phát hành), nên nếu không có đường này thì không dev được. Nhánh này **không được biên dịch vào bản Release** — trên máy người dùng, chữ ký là đường duy nhất.
- **`tools/fetch_engine.py`** đối chiếu file tải về với lock trước khi ghi vào đĩa.
- **`counter` trong lock** chính là mức sàn được nhúng vào bản build.

### 6. Khi engine không nạp được

Không im lặng nữa. `EngineController` phát cờ `TSF_ENGINE_UNTRUSTED` qua SharedState khi Kiểm tra Chính tả Nâng cao đang bật mà engine không dùng được; Cài đặt và menu khay hệ thống hiển thị banner đề nghị khởi động lại Windows. Lý do bật cờ nằm trong log (`Logger`), gồm cả trường hợp thiếu `.sig`, sai chữ ký, hay engine cũ hơn mức sàn.

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
