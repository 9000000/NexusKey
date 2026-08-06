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
VKey có cơ chế **tự động chuyển đổi (Fallback)** thông minh: nếu file `vkey_engine.dll` không tồn tại, bị lỗi, hoặc thiếu file chữ ký `vkey_engine.dll.sig` đi kèm, VKey sẽ ngay lập tức quay lại sử dụng Engine C++ mặc định. Quá trình gõ tiếng Việt của bạn sẽ không bao giờ bị ngắt quãng.

### Sau khi cập nhật VKey, tính năng nâng cao mất trong Chrome / Word nhưng vẫn chạy ở nơi khác?
Hãy **khởi động lại Windows**, tính năng sẽ trở lại.

Nguyên nhân: trong các ứng dụng đó, VKey xử lý phím bằng một thư viện riêng (`VKeyTSF.dll`) nạp thẳng vào ứng dụng. Windows không cho ghi đè một thư viện đang được ứng dụng khác sử dụng, nên khi cập nhật, file này được hoãn thay tới lần khởi động kế tiếp — và những ứng dụng đang mở vẫn giữ bản cũ trong bộ nhớ cho tới khi chúng đóng lại.

Từ bản dùng chữ ký số, bản `VKeyTSF.dll` cũ vẫn nạp được engine mới, nên trường hợp này hiếm đi nhiều. Nếu vẫn xảy ra, thường là thiếu file `vkey_engine.dll.sig` bên cạnh engine. Dù thế nào bạn vẫn gõ tiếng Việt bình thường, chỉ thiếu phần sửa lỗi gõ nhanh, và VKey sẽ hiển thị thông báo đề nghị khởi động lại trong cửa sổ Cài đặt cũng như menu khay hệ thống.

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

### Vì sao VKey phải kiểm tra `vkey_engine.dll` trước khi nạp?
Vì file này chạy **bên trong** ứng dụng bạn đang gõ. Nếu nạp bừa, ai đặt được một file cùng tên vào thư mục cài VKey là chạy được mã của họ trong mọi ô nhập liệu của bạn. Nên VKey chỉ nạp engine khi chứng minh được nó đúng là bản do dự án phát hành.

### VKey kiểm bằng cách nào?
Bằng **chữ ký số**. Mỗi bản engine phát hành đi kèm một file nhỏ `vkey_engine.dll.sig` (72 byte) — chữ ký ECDSA P-256 do dự án ký. Khoá công khai để kiểm chữ ký đó được nhúng sẵn trong `VKey.exe` và `VKeyTSF.dll` lúc biên dịch, nên không ai thay được từ bên ngoài. Trước khi nạp, VKey tự tính lại mã băm của engine trên đĩa rồi đối chiếu với chữ ký; sai một byte là từ chối.

Vì vậy **`vkey_engine.dll.sig` phải luôn nằm cạnh `vkey_engine.dll`** — chép engine đi đâu thì chép cả file này theo, thiếu nó VKey sẽ quay về Engine C++.

### Trước đây nghe nói kiểm SHA-256, giờ khác gì?
Cách cũ ghim mã băm của đúng một file, nên `VKey.exe` và `VKeyTSF.dll` buộc phải cùng lứa build với engine — chỉ cần một file bị bỏ lại trong lúc cập nhật là tính năng nâng cao tắt ngấm. Chữ ký ghim *người phát hành* thay vì một file cụ thể, nên bản cũ vẫn nạp được engine mới và tình trạng đó không còn.

Kèm theo là số thứ tự bản phát hành nằm trong phần được ký, nên không thể lấy một bản engine cũ (dù chữ ký thật) để đắp ngược lại.

### Chữ ký này bảo vệ được tới đâu?
Nó chặn mọi cách tráo **riêng file engine**: thay bằng file khác, tự ký bằng khoá lạ, sửa số hiệu trong file chữ ký, hay đắp lại bản cũ — đều bị từ chối.

Nó **không** bảo vệ được khi máy bạn đã bị chiếm quyền ghi vào thư mục cài VKey: kẻ sửa được `VKeyTSF.dll` thì cũng sửa được chính đoạn mã đi kiểm tra. Không cơ chế nào ở tầng này ngăn được điều đó — hãy giữ máy sạch và chỉ tải VKey từ trang phát hành chính thức.

## 3. Quản lý, Cài đặt & Gỡ bỏ

### Tại sao file `vkey_engine.dll` không được đóng gói sẵn trong file ZIP phát hành?
Để giữ cho bộ cài VKey dung lượng **nhẹ nhất có thể** (chỉ vài MB) cho những người dùng chỉ có nhu cầu gõ tiếng Việt cơ bản. Người dùng nào muốn trải nghiệm tính năng kiểm tra chính tả nâng cao mới bật tùy chọn để ứng dụng tải về.

### Làm thế nào để bật hoặc tắt Kiểm tra Chính tả Nâng cao?
1. Mở bảng điều khiển VKey từ khay hệ thống (System Tray).
2. Lựa chọn **Kiểm tra chính tả nâng cao**.

### Làm thế nào để gỡ bỏ hoàn toàn Engine Rust?
Mở thư mục cài đặt VKey và xóa hai file: `vkey_engine.dll` và `vkey_engine.dll.sig`.

Sau khi xóa, VKey sẽ tự động quay về sử dụng Engine C++ mặc định mà không cần cấu hình gì thêm.

### Máy tính của tôi không có mạng, tôi có thể cài đặt Engine Rust thủ công không?
**Có**. Tải **cả hai** file `vkey_engine.dll` và `vkey_engine.dll.sig` từ trang [GitHub Release](https://github.com/phatMT97/VKey/releases) chính thức bằng một máy khác, rồi chép cả hai vào cùng thư mục chứa `VKey.exe`. Thiếu file `.sig` thì VKey không xác thực được engine và sẽ dùng Engine C++.

---

## 4. Mã nguồn & Giấy phép phát hành

### VKey được phát hành theo giấy phép mã nguồn mở nào?
VKey được phát hành dưới giấy phép mã nguồn mở **GNU Affero General Public License v3.0 (AGPL-3.0)**.

### Phần nào của VKey là mã nguồn mở?
Toàn bộ mã nguồn cốt lõi của VKey bao gồm:
- Hạ tầng tiếp nhận sự kiện bàn phím từ hệ điều hành (Windows TSF IME & Low-Level Hook).
- Hệ thống điều phối sự kiện (Coordinator) và giao diện người dùng (Sciter UI).
- Engine xử lý tiếng Việt C++ (Built-in C++ Engine).
- Cơ chế nạp động an toàn, xác thực chữ ký số ECDSA P-256 và giao diện C ABI.

Tất cả đều được công khai minh bạch tại repository GitHub của dự án.

### Tôi là lập trình viên, tôi có thể tự viết Engine riêng hoặc tích hợp VKey với bộ xử lý khác không?
**Có, nhưng phải tự build VKey.** Giao diện C ABI là chuẩn mở (`include/vkey_engine.h`), viết engine tuân thủ nó là đủ về mặt kỹ thuật.

Tuy nhiên **bản VKey phát hành sẵn sẽ không nạp engine của bạn**: nó chỉ chấp nhận engine mang chữ ký của dự án, và đó chính là thứ ngăn người khác đặt một DLL tuỳ ý vào thư mục cài trên máy người dùng. Để chạy engine của mình, bạn build VKey từ mã nguồn với khoá công khai của chính bạn (`extern/vkey_engine/engine.pub`), hoặc dùng bản Debug — bản Debug còn chấp nhận engine khớp mã băm khai trong `engine.lock`.

Chi tiết kỹ thuật trong [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md).

---

## Tài liệu Liên quan

- **[ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md)**: Chi tiết kiến trúc kỹ thuật, luồng xử lý phím, C ABI và mô hình bảo mật file.
