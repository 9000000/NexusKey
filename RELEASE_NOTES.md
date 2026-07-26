# VKey v4.3

[![Signed by SignPath](https://img.shields.io/badge/Signed_by-SignPath-blue)](https://signpath.io)

Bản cập nhật này mang đến cải tiến đột phá về khả năng tự động sửa lỗi chính tả cùng các sửa lỗi quan trọng giúp nâng cao độ ổn định.

### ✨ Tính năng mới & Cải tiến nổi bật

* **Tinh chỉnh UI**
    * **Tab "Hệ thống"**: Gom nhóm tuỳ chọn icon để giúp UI gọn hơn (chỉ ở giao diện hiện đại)
    * **Tab "Macro"**: Tách riêng các nút hành động (Lưu, Test, Xóa, Nhập) và cải thiện bố cục danh sách macro để dễ sử dụng hơn.
* **Chế độ "Kiểm tra chính tả nâng cao"**
    * **Engine Rust hiệu năng cao**: Bổ sung engine kiểm tra chính tả mới được viết bằng Rust, tập trung vào hiệu năng và khả năng nhận diện lỗi.
    * **Tự sửa lỗi gõ nhanh**: Phát hiện và sửa các lỗi gõ nhanh hoặc đảo ký tự (ví dụ: `hcaof` → `chào`).
    * **Hoạt động theo lựa chọn của người dùng**: Tính năng mặc định được tắt và chỉ được kích hoạt khi bạn chủ động bật trong cài đặt.
    * **Tương thích với engine mã nguồn mở**: Nếu engine nâng cao không có hoặc bị gỡ bỏ, VKey sẽ tự động sử dụng engine C++ mã nguồn mở đi kèm mà không ảnh hưởng đến các chức năng gõ tiếng Việt thông thường.

> ℹ️ **Tìm hiểu thêm**
>
> Để biết thêm về kiến trúc của engine nâng cao, cách phân phối, lý do thiết kế, cũng như các vấn đề liên quan đến mã nguồn và giấy phép, vui lòng xem:
>
> - [ENGINE_ARCHITECTURE.md](docs/ENGINE_ARCHITECTURE.md)
> - [Giải thích về kiến trúc, license và nguồn gốc của VKey](docs/ENGINE_FAQ.md)
---

### 🛠 Các lỗi đã được khắc phục
*   **Sửa lỗi bỏ dấu từ ghép**: Khắc phục lỗi gõ từ `ruouwj` không bỏ dấu đúng cách để tạo thành từ `rượu`.
*   **Sửa lỗi gõ từ tiếng Anh**: Khắc phục hiện tượng gõ từ `view` bị chuyển nhầm thành `vieư`.
*   **Tối ưu hóa trạng thái hoạt động**: Sửa lỗi ứng dụng chuyển sang trạng thái chờ (idle) quá nhanh gây ảnh hưởng đến trải nghiệm người dùng.
*   **Cải tiến giao diện**: Tối ưu hóa hiệu năng hiển thị và chuyển đổi của các bộ giao diện (theme).
*   **Sửa lỗi excel online**: Khắc phục lỗi mất từ trước đó khi dùng shift để viết hoa từ tiếng Việt. 
*   **Sửa lỗi macro TSF**: Macro đã hoạt động với TSF
*   **Sửa các lỗi khác**: Sửa một số lỗi khác.
---
### 💖 Sponsors

Free code signing on Windows provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/). Thank you, SignPath! 🙏

---

*Cảm ơn bạn đã tin tưởng và lựa chọn VKey! Mọi đóng góp của bạn đều là nguồn động lực lớn giúp bộ gõ ngày càng hoàn thiện hơn.* ❤️