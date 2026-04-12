# NexusKey v2.1.14

**✨ Tính năng mới & Cải tiến:**
- Thêm tuỳ chọn mode mặc định khi khởi động: Nhớ mode cũ - Luôn E/V
- Cải thiện kiểm tra chính ta cho trường hợp gõ tiếng anh kết thúc bằng -ing (tham khảo tài liệu của [Gonhanh.org](https://github.com/khaphanspace/gonhanh.org?tab=readme-ov-file#-t%C3%A0i-li%E1%BB%87u-k%E1%BB%B9-thu%E1%BA%ADt) - Cảm ơn bác Kha)

**🛠 Sửa lỗi:**
- Giao diện Classic: thêm nút "Cấu hình từng ứng dụng", gộp ô nhập + danh sách app đang chạy thành combobox, gỡ nút "Đóng" thừa ở các dialog phụ
- Điều chỉnh behavior của app để hạn chế bị Windows Defender nhận diện sai (thêm trustInfo manifest, PE checksum, version info cho DLL, giữ debug symbols)

**🔒 Bảo mật & Xác minh:**
- Bản phát hành được ký bằng [Sigstore](https://sigstore.dev) và đính kèm [build attestation](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations) từ GitHub Actions
- Xác minh: `gh attestation verify NexusKey.zip --repo PhatMT97/NexusKey`

---
Cảm ơn bạn đã lựa chọn NexusKey! Mọi đóng góp của bạn đều là nguồn động lực lớn giúp bộ gõ ngày càng hoàn thiện hơn. ❤️
