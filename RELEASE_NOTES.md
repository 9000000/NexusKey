# NexusKey v2.1.18
**✨ Tính năng mới & Cải tiến:**
- Thêm chế độ gõ telex kết hợp vni (vẫn trong giai đoạn thử nghiệm cần thêm feedback)
- Macro xuống dòng với số lượng lớn hỗ trợ tối đa 20k kí tự, cho phép tuỳ chỉnh phím trigger macro (enter/space/tab/mũi tên <.>)
- Mở lại TSF  (lưu ý vẫn còn khá nhiều lỗi, cần test và feedback để cải thiện thêm), hiện tại đã có:
  - Cho phép sửa từ đã commit -> tested trên chrome, notepad++ sẽ lỗi không hoạt động, khuyến cáo dùng hook
  - Cho phép in hoa chữ đầu dòng 

**🛠 Sửa lỗi:**
- Sửa lỗi không gõ được ẤP (A-P-A-S) (refactor engine tự được sửa)

**ℹ️ Planning:**
- Cho phép paste macro vẫn giữ format (tạm thời sẽ giữ tab), các format khác cần nghiên cứu độ khả thi
- Sử dụng từ để tiên đoán phục hồi từ: ví dụ gõ a-s-u-s -> kết quả sẽ là aus -> engine tra từ điển tự phục hồi thành "asus" -> cần cộng đồng đóng góp từ điển
- Cải tiến thêm engine TSF nếu có người chịu test, ban đầu app được xây dựng để có thể chạy song song 2 engine cũng như tiến tới lâu dài nếu TSF được mở rộng hơn

---
Cảm ơn bạn đã lựa chọn NexusKey! Mọi đóng góp của bạn đều là nguồn động lực lớn giúp bộ gõ ngày càng hoàn thiện hơn. ❤️
