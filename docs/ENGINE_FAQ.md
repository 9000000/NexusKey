# Giải đáp Thắc mắc về VKey Engine (FAQ)

Tài liệu này giải đáp các câu hỏi thường gặp liên quan đến bộ xử lý gõ tiếng Việt (Engine), tính năng Kiểm tra Chính tả Nâng cao, bảo mật, quyền riêng tư và giấy phép của dự án **VKey**.

---

## Mục lục

1. [Tính năng & Trải nghiệm gõ](#1-tính-năng--trải-nghiệm-gõ)
2. [An toàn, Bảo mật & Quyền riêng tư](#2-an-toàn-bảo-mật--quyền-riêng-tư)
3. [Quản lý, Cài đặt & Gỡ bỏ](#3-quản-lý-cài-đặt--gỡ-bỏ)
4. [Mã nguồn & Giấy phép phát hành](#4-mã-nguồn--giấy-phép-phát-hành)

---

## 1. Tính năng & Trải nghiệm gõ

### VKey Engine là gì?
VKey Engine là "bộ não" xử lý quy tắc ngôn ngữ của bộ gõ. Khi bạn gõ các phím từ bàn phím (như `v`, `i`, `e`, `t`, `s`), Engine có nhiệm vụ phân tích và chuyển đổi chuỗi phím này thành từ tiếng Việt hoàn chỉnh có dấu (`việt`).

### VKey có mấy bộ xử lý (engine)? Sự khác biệt là gì?
VKey được thiết kế theo mô hình **Engine kép (Dual-Engine)** gồm hai bộ xử lý:

| Tiêu chí | Engine C++ (Mặc định) | Engine Rust Nâng cao (Tùy chọn) |
| :--- | :--- | :--- |
| **Mã nguồn** | Mã nguồn mở 100% tích hợp sẵn | Thư viện mở rộng động (`vkey_engine.dll`) |
| **Kiểu gõ** | Telex, VNI, Simple Telex chuẩn | Telex, VNI nâng cao + Ngữ cảnh |
| **Tính năng nổi bật** | Nhẹ, nhanh, bỏ dấu chính xác | Sửa lỗi gõ nhanh bị đảo phím (`hcaof` → `chào`), gợi ý từ vựng |
| **Cách cài đặt** | Có sẵn trong ứng dụng VKey | Tùy chọn bật trong Cài đặt |

### Tính năng "Kiểm tra Chính tả Nâng cao" giúp gì cho tôi?
Tính năng này giúp phát hiện và tự động sửa một số lỗi gõ phổ biến khi bạn thao tác nhanh trên bàn phím. 
- *Ví dụ*: Khi bạn gõ lướt phím nhanh từ `chào`, tay bạn có thể bấm nhầm thứ tự thành `hcaof`. Engine Nâng cao sẽ nhận biết được đây là lỗi đảo ký tự và tự động khôi phục đúng thành `chào`.

### Tôi có bắt buộc phải cài đặt hay dùng Engine Rust Nâng cao không?
**Không**. VKey hoạt động hoàn toàn độc lập và đáp ứng 100% nhu cầu gõ tiếng Việt thông thường chỉ với Engine C++ mặc định. Bạn có thể sử dụng VKey ngay sau khi tải về mà không cần bật hay cài đặt thêm bất kỳ thành phần nào.

### Nếu tôi bật Kiểm tra Chính tả Nâng cao mà file engine bị thiếu hoặc bị lỗi thì sao?
VKey có cơ chế **tự động chuyển đổi (Fallback)** thông minh: nếu file `vkey_engine.dll` không tồn tại hoặc bị lỗi, VKey sẽ ngay lập tức quay lại sử dụng Engine C++ mặc định. Quá trình gõ tiếng Việt của bạn sẽ không bao giờ bị ngắt quãng.

### Sau khi cập nhật VKey, tính năng nâng cao mất trong Chrome / Word nhưng vẫn chạy ở nơi khác?
Hãy **khởi động lại Windows**, tính năng sẽ trở lại.

Nguyên nhân: VKey xử lý phím trong các ứng dụng đó bằng một thư viện riêng (`VKeyTSF.dll`) nạp thẳng vào ứng dụng. Windows không cho phép ghi đè một thư viện đang được ứng dụng khác sử dụng, nên khi cập nhật, file này được hoãn thay tới lần khởi động kế tiếp. Trong lúc chờ, nó vẫn là bản cũ và chỉ tin đúng bản engine đi cùng lứa với nó, nên tạm thời quay về Engine C++ — bạn vẫn gõ tiếng Việt bình thường, chỉ thiếu phần sửa lỗi gõ nhanh.

VKey nhận biết được tình trạng này và hiển thị thông báo đề nghị khởi động lại trong cửa sổ Cài đặt cũng như menu ở khay hệ thống.
---

## 2. An toàn, Bảo mật & Quyền riêng tư

### VKey hay Engine Rust có theo dõi hoặc gửi phím gõ của tôi ra Internet không?
**Hoàn toàn KHÔNG**. 
- Cả ứng dụng VKey và thư viện Engine Rust đều chạy **100% ngoại tuyến (offline)** trực tiếp trên máy tính của bạn.
- Thư viện Engine Rust là một file DLL chạy nội bộ trong tiến trình của VKey và **không chứa bất kỳ mã lệnh kết nối mạng nào**.

### Làm sao tôi có thể tự kiểm chứng VKey không gửi dữ liệu bàn phím đi đâu?
Bạn có thể tự kiểm tra bằng 2 cách đơn giản sau:
1. **Dùng Tường lửa (Windows Firewall)**: Bạn có thể tạo quy tắc chặn hoàn toàn kết nối Internet của file `VKey.exe`. VKey và tính năng gõ tiếng Việt / kiểm tra chính tả vẫn hoạt động hoàn hảo 100%.
2. **Kiểm tra mã nguồn**: Toàn bộ mã nguồn phần tiếp nhận bàn phím, giao tiếp thư viện và hiển thị giao diện của VKey đều được mở công khai trên GitHub để bất kỳ ai cũng có thể đọc và audit.

### Vì sao VKey lại kiểm tra mã SHA-256 trước khi nạp file `vkey_engine.dll`?
Đây là cơ chế bảo vệ an toàn cho máy tính của bạn. Việc kiểm tra mã SHA-256 giúp VKey đảm bảo file `vkey_engine.dll` đúng là file chính thức phát hành bởi dự án, chưa bị mã độc hoặc vi rút chỉnh sửa hay thay thế trên máy tính của bạn.

Đánh đổi của cách làm này: mã băm được ghim cứng lúc biên dịch, nên VKey chỉ tin đúng bản engine đi cùng lứa với nó. Đó là lý do một bản cập nhật thay được `vkey_engine.dll` nhưng chưa thay được `VKeyTSF.dll` sẽ tạm mất tính năng nâng cao cho tới khi bạn khởi động lại Windows (xem câu hỏi ở mục 1). Chi tiết kỹ thuật và hướng cải tiến bằng chữ ký số nằm trong [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md#4-mô-hình-bảo-mật--kiểm-tra-an-toàn-file-security--trust-model).

---

## 3. Quản lý, Cài đặt & Gỡ bỏ

### Tại sao file `vkey_engine.dll` không được đóng gói sẵn trong file ZIP phát hành?
Để giữ cho bộ cài VKey dung lượng **nhẹ nhất có thể** (chỉ vài MB) cho những người dùng chỉ có nhu cầu gõ tiếng Việt cơ bản. Người dùng nào muốn trải nghiệm tính năng kiểm tra chính tả nâng cao mới bật tùy chọn để ứng dụng tải về.

### Làm thế nào để bật hoặc tắt Kiểm tra Chính tả Nâng cao?
1. Mở bảng điều khiển VKey từ khay hệ thống (System Tray).
2. Lựa chọn **Kiểm tra chính tả nâng cao**.

### Làm thế nào để gỡ bỏ hoàn toàn Engine Rust?
Nếu không muốn tiếp tục sử dụng, bạn chỉ cần thực hiện 1 bước đơn giản:
- Mở thư mục cài đặt VKey và xóa file `vkey_engine.dll`.

Sau khi xóa, VKey sẽ tự động quay về sử dụng Engine C++ mặc định mà không cần cấu hình gì thêm.

### Máy tính của tôi không có mạng, tôi có thể cài đặt Engine Rust thủ công không?
**Có**. Bạn có thể tải file `vkey_engine.dll` từ trang [GitHub Release](https://github.com/phatMT97/VKey/releases) thức của VKey bằng một máy tính khác, sau đó chép file này vào cùng thư mục chứa file `VKey.exe`.

---

## 4. Mã nguồn & Giấy phép phát hành

### VKey được phát hành theo giấy phép mã nguồn mở nào?
VKey được phát hành dưới giấy phép mã nguồn mở **GNU Affero General Public License v3.0 (AGPL-3.0)**.

### Phần nào của VKey là mã nguồn mở?
Toàn bộ mã nguồn cốt lõi của VKey bao gồm:
- Hạ tầng tiếp nhận sự kiện bàn phím từ hệ điều hành (Windows TSF IME & Low-Level Hook).
- Hệ thống điều phối sự kiện (Coordinator) và giao diện người dùng (Sciter UI).
- Engine xử lý tiếng Việt C++ (Built-in C++ Engine).
- Cơ chế nạp động an toàn, xác thực SHA-256 và giao diện C ABI.

Tất cả đều được công khai minh bạch tại repository GitHub của dự án.

### Tôi là lập trình viên, tôi có thể tự viết Engine riêng hoặc tích hợp VKey với bộ xử lý khác không?
**Có**. VKey định nghĩa một giao diện C ABI chuẩn (`include/vkey_engine.h`). Bất kỳ thư viện nào tuân thủ giao diện C ABI này đều có thể nạp và sử dụng cùng với VKey. Bạn có thể tham khảo thêm tài liệu [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md) để biết chi tiết kỹ thuật.

---

## Tài liệu Liên quan

- **[ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md)**: Chi tiết kiến trúc kỹ thuật, luồng xử lý phím, C ABI và mô hình bảo mật file.
