# VKey v4.0.0

**✨ Tính năng mới & Cải tiến:**
- Refactor kiến trúc engine gõ tiếng Việt
- Thêm watchdog cho phiên bản classic
- Gộp các phím tắt vào 1 nơi cho dễ config và quản lý
- Cho phép hotkey nhiều phím hơn cho công cụ chuyển mã
- Bổ sung kiểu gõ ở menu traybar
- Cho phép đồng bộ với gợi ý của trình duyệt: backspace sẽ chỉ xoá gợi ý thay vì xoá ký tự.
- Thêm TSF cho phiên bản classic
- Tối ưu thuật toán và kiến trúc hook
- Bổ sung hướng dẫn sử dụng và FAQ cho người dùng có thể nhanh chóng hiểu và sử dụng được ứng dụng.

**🛠 Sửa lỗi:**
- Sửa lỗi c-h-u-y-e-n-j-e không thành chuyện
- Sửa lỗi "Tự định nghĩa": gán `;` (hoặc dấu khác `'` `,` `.` `/` `\` `` ` `` `-` `=` `[` `]`) làm dấu nặng/sắc/huyền/hỏi/ngã không ăn.
- Sửa lỗi "Tự định nghĩa" hoạt động không đúng với w
- Sữa lỗi tự viết hoa đầu câu: ".zip Này" thay vì ".zip này"
- Sửa lỗi không gõ được tiếng việt trong dialog phiên bản classic 
- Sửa lỗi space -> BS -> ESC không tra lại raw key
- Sửa lỗi dialog không căn giữa màn hình khi bật advanced setting
- Sửa lỗi đôi lúc không gõ được tiếng việt
- Sửa lỗi [] 2 lần không trả lại phím raw
- Sửa một số lỗi gõ tiếng Việt