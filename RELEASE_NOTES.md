# VKey v3.0.0 - VKey
Rebranch lần nữa cho thân thiện hơn, giờ nó sẽ là VKey. Mọi người đang dùng version cũ cần tắt khởi động cùng windows trước khi cài version mới (xem hướng dẫn: [Gỡ Auto-Start NexusKey](docs/uninstall-autostart.md)). Version này cần tải thủ công lần đầu.

**✨ Tính năng mới & Cải tiến:**
- Refactor lại hook engine, tối ưu lại hiệu suất gõ và tính ổn định của app
- Cho phép config dùng clipboard cho app - nằm trong "Cấu hình từng ứng dụng". Sẽ không bị đè clipboard nếu copy text, còn hình thì vẫn bị mất 😶‍🌫️.
- Thêm tính năng tự self-healing cho hook tránh tình trạng bị ghi đè hoặc mất nếu treo máy dài ngày
- Cải thiện hiệu suất cho tính năng convert
- Cho phép viết tắt các từ có ký tự đ. Ví dụ hđ, sđt, tđn... (không cần thêm thủ công vào danh sách loại trừ, các trường hợp khác vẫn cần)
- Thêm tuỳ chọn export/import danh sách loại trừ chính tả cho classic version
- Thêm tính năng watchdog - tự chạy lại app nếu tiến trình bị kill không mong đợi (nằm ở menu chuột phải - tự chạy lại trong 60s)
- Thêm tính năng tuỳ chỉnh kiểu gõ
- Tách tự động phát hiện layout CJK thành tùy chọn (mặc định off nên cần bật lại thủ công nếu đang có nhiều layout)
- Thêm tùy chọn tắt gõ tiếng Việt tạm thời: Ctrl/Dup Alt
- Cho phép trả lại raw key bằng phím ESC. Ví dụ: gõ v-i-r-u-s + ESC -> virus thay vì víu (lưu ý ESC trước khi space mới có tác dụng)

**🛠 Sửa lỗi:**
- Thêm thông tin về việc tương thích cho icon nổi (không tương thích với game - gây drop FPS 🐧 nhưng không sao, telex có thể chơi game mà không cần tắt)
- Sửa lỗi VNI tiếp tục đặt dấu sau ký tự số. Ví dụ E747 (bị thành Ẽ77)
- Sửa lỗi khi bật kiểm tra chính tả không sửa lại dấu đã đặt. Ví dụ c-a-f-c-s -> càcs thay vì các
- Sửa lỗi không tự chuyển từ V->E khi đổi layout Eng -> Japan trong MS Teams