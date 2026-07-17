# VKey v4.3

[![Signed by SignPath](https://img.shields.io/badge/Signed_by-SignPath-blue)](https://signpath.io)

Bản cập nhật này mang đến cải tiến đột phá về khả năng tự động sửa lỗi chính tả cùng các sửa lỗi quan trọng giúp nâng cao độ ổn định.

### ✨ Tính năng mới & Cải tiến nổi bật
*   **Chế độ "Kiểm tra chính tả nâng cao"**:
    *   **Engine Rust mạnh mẽ**: Tích hợp công cụ kiểm tra chính tả được viết bằng Rust cho hiệu năng tối ưu và độ chính xác cao.
    *   **Tự sửa lỗi gõ sai**: Nhận diện và sửa lỗi gõ phím nhanh/đảo ký tự thông minh (ví dụ: gõ `hcaof` tự động sửa thành `chào`).
    *   **Linh hoạt & Minh bạch**: Tính năng này mặc định được tắt và chỉ hoạt động khi bạn bật thủ công. Nếu không muốn sử dụng engine đóng gói đi kèm, bạn có thể xóa file engine đó; VKey sẽ tự động chuyển đổi (fallback) về engine C++ mã nguồn mở truyền thống. Xem chi tiết lý do và tính minh bạch tại **[Tìm hiểu về Engine đóng gói (RUST_ENGINE.md)](docs/RUST_ENGINE.md)**.
---

### 🛠 Các lỗi đã được khắc phục
*   **Sửa lỗi bỏ dấu từ ghép**: Khắc phục lỗi gõ từ `ruouwj` không bỏ dấu đúng cách để tạo thành từ `rượu`.
*   **Sửa lỗi gõ từ tiếng Anh**: Khắc phục hiện tượng gõ từ `view` bị chuyển nhầm thành `vieư`.
*   **Tối ưu hóa trạng thái hoạt động**: Sửa lỗi ứng dụng chuyển sang trạng thái chờ (idle) quá nhanh gây ảnh hưởng đến trải nghiệm người dùng.
*   **Cải tiến giao diện**: Tối ưu hóa hiệu năng hiển thị và chuyển đổi của các bộ giao diện (theme).
---
### 💖 Sponsors

Free code signing on Windows provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/). Thank you, SignPath! 🙏

---

*Cảm ơn bạn đã tin tưởng và lựa chọn VKey! Mọi đóng góp của bạn đều là nguồn động lực lớn giúp bộ gõ ngày càng hoàn thiện hơn.* ❤️