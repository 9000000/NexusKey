# VKey và phần mềm diệt virus (Antivirus false positives)

> **TL;DR:** VKey là bộ gõ mã nguồn mở, **có ký số** (Authenticode bởi SignPath
> Foundation). Một số phần mềm diệt virus (Kaspersky, Bitdefender, Avast…) đôi khi
> cảnh báo VKey theo **hành vi** (behavior detection) chứ không phải chữ ký virus —
> vì bộ gõ nào cũng phải hook bàn phím + gửi phím, trông "giống keylogger". Đây là
> **cảnh báo nhầm (false positive)**. Dưới đây là cách khôi phục và loại trừ.

---

## Vì sao bị cảnh báo dù đã ký số?

Ký số (code signing) giúp Windows SmartScreen và các bộ quét **theo chữ ký** tin
tưởng file. Nhưng phần mềm diệt virus còn có lớp **phát hiện theo hành vi** —
chấm điểm những gì tiến trình *làm khi chạy*, không quan tâm ai ký. Bộ gõ tiếng
Việt về bản chất phải:

- Cài **global keyboard hook** (đọc phím để bỏ dấu)
- **Gửi phím tổng hợp** (`SendInput`) để thay chữ
- **Tự khởi động cùng Windows** (autorun)
- Chạy **watchdog** để bật lại nếu bị tắt

Cụm hành vi này trùng khớp hồ sơ "keylogger" trong heuristic của AV → cảnh báo
nhầm. VKey **không** gửi phím của bạn đi đâu cả — toàn bộ xử lý là cục bộ, mã nguồn
công khai để kiểm chứng.

Từ bản này, VKey đã giảm bớt tín hiệu gây nhầm (watchdog không còn bật lại vô hạn
sau khi bị AV tắt; không ghi log nội dung phím ở bản Release; không ghi autorun
lặp lại).

## Kiểm chứng VKey là bản thật (khuyên dùng trước khi loại trừ)

Chạy PowerShell trên chính file bạn tải:

```powershell
(Get-AuthenticodeSignature "C:\path\to\VKey.exe").Status          # → Valid
(Get-AuthenticodeSignature "C:\path\to\VKey.exe").SignerCertificate.Subject
# → CN=SignPath Foundation, O=SignPath Foundation, L=Lewes, S=Delaware, C=US
```

Nếu `Status` không phải `Valid` hoặc publisher khác dòng trên → **không** dùng file
đó, tải lại từ trang phát hành chính thức.

## Khôi phục khi VKey bị cách ly (quarantine)

### Kaspersky
1. Mở Kaspersky → **Bảo mật** (Security) → **Báo cáo / Cách ly** (Reports / Quarantine).
2. Tìm mục VKey.exe, chọn **Khôi phục** (Restore).
3. Thêm loại trừ: **Cài đặt → Mối đe dọa & Loại trừ → Quản lý loại trừ → Thêm** →
   chọn thư mục cài VKey (ví dụ `D:\Softwares\VKey`).
4. Nếu Kaspersky đã xóa cả khóa autorun: mở lại VKey → **Cài đặt → Khởi động cùng
   Windows** để bật lại.

### Windows Defender
1. **Bảo mật Windows → Bảo vệ khỏi virus & mối đe dọa → Lịch sử bảo vệ**.
2. Tìm mục VKey → **Tác vụ → Khôi phục**.
3. Loại trừ: **Cài đặt bảo vệ khỏi virus & mối đe dọa → Loại trừ → Thêm** → thư mục VKey.

### Chung (Bitdefender / Avast / AVG…)
Vào phần **Quarantine / Cách ly**, khôi phục VKey, rồi thêm **Exclusion / Ngoại lệ**
cho thư mục cài đặt.

## Báo cáo cảnh báo nhầm (giúp mọi người dùng)

Gửi mẫu cho hãng AV để họ gỡ cảnh báo — vài ngày là có hiệu lực cho tất cả người dùng:

- **Kaspersky:** https://opentip.kaspersky.com/ (chọn *False detection* / gửi file).
- **Microsoft:** https://www.microsoft.com/wdsi/filesubmission
- **Bitdefender:** https://www.bitdefender.com/submit/
- **Avast/AVG:** https://www.avast.com/false-positive-file-form.php

Hoặc mở issue tại kho GitHub của VKey kèm **tên phát hiện** (ví dụ `PDM:Trojan.Win32…`)
và tên phần mềm AV — tác giả sẽ nộp allowlist giúp.

---

## English (short)

VKey is an open-source, **code-signed** Vietnamese IME. Some antivirus products
flag it by **behavior detection** (not signature) because every IME must hook the
keyboard, synthesize keystrokes, autostart, and self-supervise — a cluster that
looks keylogger-shaped. It is a **false positive**; VKey processes everything
locally and never sends your keystrokes anywhere.

- **Verify authenticity:** `(Get-AuthenticodeSignature VKey.exe).SignerCertificate.Subject`
  → should be `CN=SignPath Foundation, …`.
- **Restore + exclude:** open your AV's Quarantine, restore VKey, add the install
  folder to Exclusions. Re-enable "Run at startup" in VKey settings if the autorun
  key was removed.
- **Report the false positive:** Kaspersky OpenTIP, Microsoft WDSI, Bitdefender,
  Avast/AVG submission forms (links above), or file a GitHub issue with the exact
  detection name.
