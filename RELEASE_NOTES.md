# VKey v4.3

> ⚠️ **Lưu ý quan trọng về Chữ ký số & Cập nhật tự động (Code Signing & Auto-Update)**
>
> 1. **Chưa có chữ ký số**: Phiên bản v4.3 hiện tại **chưa được ký số** do vướng một số vấn đề điều khoản ký số với phía SignPath (liên quan đến việc tích hợp thư viện Sciter mã nguồn đóng). Do đó, tệp cài đặt/chạy có thể bị Windows SmartScreen cảnh báo hoặc một số phần mềm diệt virus tự động xóa/chặn (bạn có thể chọn *More info* → *Run anyway* để tiếp tục sử dụng).
> 2. **Cần tải bản v4.3 thủ công**: Do v4.2 yêu cầu kiểm tra chữ ký số an toàn khi nâng cấp, tính năng cập nhật tự động từ v4.2 lên v4.3 sẽ không hoạt động. Người dùng đang ở phiên bản v4.2 vui lòng **tải và cài đặt bản v4.3 thủ công** từ trang phát hành GitHub Release.

Bản cập nhật này mang đến cải tiến đột phá về khả năng tự động sửa lỗi chính tả cùng các sửa lỗi quan trọng giúp nâng cao độ ổn định.

### ✨ Tính năng mới & Cải tiến nổi bật

* **Tinh chỉnh UI**  (chỉ ở giao diện hiện đại)
    * **Tab "Hệ thống"**: Gom nhóm tuỳ chọn icon để giúp UI gọn hơn
    * **Tab "Macro"**: Tách riêng các nút hành động (Lưu, Test, Xóa, Nhập) và cải thiện bố cục danh sách macro để dễ sử dụng hơn.
    * **Xóa nhiều gõ tắt cùng lúc**: Thêm ô chọn ở bảng Macro để chọn và xóa nhiều từ gõ tắt trong một lần.
* **Chế độ "Kiểm tra chính tả nâng cao"**
    * **Engine Rust hiệu năng cao**: Bổ sung engine kiểm tra chính tả mới được viết bằng Rust, tập trung vào hiệu năng và khả năng nhận diện lỗi.
    * **Tự sửa lỗi gõ nhanh**: Phát hiện và sửa các lỗi gõ nhanh hoặc đảo ký tự (ví dụ: `hcaof` → `chào`).
    * **Hoạt động theo lựa chọn của người dùng**: Tính năng mặc định được tắt và chỉ được kích hoạt khi bạn chủ động bật trong cài đặt.
    * **Tương thích với engine mã nguồn mở**: Nếu engine nâng cao không có hoặc bị gỡ bỏ, VKey sẽ tự động sử dụng engine C++ mã nguồn mở đi kèm mà không ảnh hưởng đến các chức năng gõ tiếng Việt thông thường.

> ℹ️ **Tìm hiểu thêm**
>
> Để biết thêm chi tiết về kiến trúc engine nâng cao, cơ chế phân phối, lý do thiết kế cũng như các vấn đề về mã nguồn & giấy phép (license), vui lòng tham khảo:
>
> - [Kiến trúc kỹ thuật Engine nâng cao](docs/ENGINE_ARCHITECTURE.md)
> - [Giải đáp FAQ về kiến trúc, giấy phép & nguồn gốc VKey](docs/ENGINE_FAQ.md)

* **🎮 Chế độ game (thay đổi hành vi mặc định — game thủ vui lòng đọc)**
    * **Trước đây**: cơ chế giữ phím cho game được bật ngầm cho **mọi** ứng dụng Win32 thông thường (Notepad, Word, Windows Terminal…). Mỗi lần bỏ dấu, VKey nhả ký tự gốc ra màn hình rồi mới xoá đi. Việc này gây nháy chữ và tình trạng đôi lúc không đặt được dấu đúng mong đợi.
    * **Từ bản này**: mặc định mọi ứng dụng dùng cách gõ thông thường (xoá rồi thay). Cơ chế giữ phím cho game trở thành **tuỳ chọn**, bật riêng cho từng ứng dụng.
    * **Cách bật**: chọn `Chế độ game` trong mục "Cách gửi" ở **Cấu hình từng ứng dụng**, hoặc gán một phím tắt cho `Bật / tắt chế độ game cho app đang mở` trong **Quản lý phím tắt** rồi nhấn ngay khi đang ở trong game — ứng dụng đó sẽ được thêm vào danh sách, nhấn lần nữa để gỡ.
    * Phím tắt này **không có tổ hợp mặc định**: bất cứ tổ hợp nào chọn sẵn cũng sẽ đụng phím đã gán trong game, nên bạn tự chọn.
    * Ứng dụng nào đã cấu hình sẵn "Cách gửi" khác (Clipboard, Firefox, Cloud/Remote…) không bị ảnh hưởng.
---

### 🛠 Các lỗi đã được khắc phục
*   **Sửa lỗi bỏ dấu từ ghép**: Khắc phục lỗi gõ từ `ruouwj` không bỏ dấu đúng cách để tạo thành từ `rượu`.
*   **Sửa lỗi gõ từ tiếng Anh**: Khắc phục hiện tượng gõ từ `view` bị chuyển nhầm thành `vieư`.
*   **Tối ưu hóa trạng thái hoạt động**: Sửa lỗi ứng dụng chuyển sang trạng thái chờ (idle) quá nhanh gây ảnh hưởng đến trải nghiệm người dùng.
*   **Cải tiến giao diện**: Tối ưu hóa hiệu năng hiển thị và chuyển đổi của các bộ giao diện (theme).
*   **Sửa lỗi excel online**: Khắc phục lỗi mất từ trước đó khi dùng shift để viết hoa từ tiếng Việt. 
*   **Sửa lỗi macro TSF**: Macro đã hoạt động với TSF
*   **Sửa lỗi mất dấu không hồi phục được**: Khắc phục lỗi thỉnh thoảng gõ `khoong` ra thẳng `khoong` thay vì `không`, và sau đó xoá đi gõ lại vẫn không tạo được dấu.

---

*Cảm ơn bạn đã tin tưởng và lựa chọn VKey! Mọi đóng góp của bạn đều là nguồn động lực lớn giúp bộ gõ ngày càng hoàn thiện hơn.* ❤️