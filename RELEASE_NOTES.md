# VKey v4.0.1

**✨ Tính năng mới & Cải tiến:**
- Tăng thời gian delay kiểm tra cập nhật lúc khởi động từ 3 giây lên 30 giây để đảm bảo kết nối mạng và Windows ổn định

**🛠 Sửa lỗi:**
- Sửa lỗi không gõ được tiếng Việt cho từ đầu sau khi nhấn hotkey
- Sửa lỗi nuốt chữ (không gõ được) và tối ưu hóa triệt để hiện tượng lag khi gõ trong ô tìm kiếm (Search/Find dialog) của Notepad trên Windows 11
- Sửa lỗi tự động cập nhật thất bại và không tự chạy lại sau khi cập nhật khi ứng dụng khởi chạy cùng hệ thống (do giới hạn Job Object ngăn cản breakaway)
- Bảo vệ và tránh ghi đè file cấu hình `config.toml` của người dùng khi cập nhật phiên bản mới