# Gỡ Auto-Start NexusKey bằng Command Line

Khi bạn từng bật "Khởi động cùng Windows" trong NexusKey, app sẽ đăng ký auto-start ở **một trong hai** vị trí (không phải cả hai):

| Chế độ lúc bật | Đăng ký ở đâu |
|---|---|
| Thường (không admin) | Registry `HKCU\...\Run` |
| Run as Admin | Task Scheduler |

Nếu UI Settings không gỡ được (app đã xoá / file lỗi / config bị reset), dùng command line dưới đây.

> **Lưu ý:** Mở `cmd` hoặc PowerShell **Run as Administrator** nếu bạn từng bật ở chế độ admin. Mode thường thì cmd user bình thường là đủ. Tắt NexusKey trước khi chạy để đảm bảo app không tự thêm vào lại

---

## 1. Gỡ ở Registry (chế độ thường)

### Kiểm tra có không

```cmd
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v NexusKey
```

- Nếu in ra dòng `NexusKey  REG_SZ  "C:\...\NexusKey.exe"` → có, sang bước xoá.
- Nếu báo `ERROR: The system was unable to find the specified registry key or value.` → không có ở đây, sang **mục 2**.

### Xoá

```cmd
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v NexusKey /f
```

Output mong đợi: `The operation completed successfully.`

### Verify

```cmd
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v NexusKey
```

Phải báo `ERROR: ... unable to find ...` → xong.

---

## 2. Gỡ ở Task Scheduler (chế độ Run as Admin)

NexusKey ở mode admin tạo **2 task** — cần xoá cả hai:

| Task | Vị trí | Vai trò |
|---|---|---|
| `NexusKey` | Root của Task Scheduler Library | Khởi động app chính khi logon |
| `\NexusKey\Watchdog` | Folder con `NexusKey` | Tự khởi động lại nếu app bị crash |

> Mở **cmd Run as Administrator** cho phần này — task có `RunLevel Highest` cần quyền admin để xoá.

### Kiểm tra có không

```cmd
schtasks /query /tn "NexusKey"
schtasks /query /tn "\NexusKey\Watchdog"
```

- Có task → in ra bảng tên + status `Ready`/`Running`.
- Không có → `ERROR: The system cannot find the file specified.`

### Xoá

```cmd
schtasks /delete /tn "NexusKey" /f
schtasks /delete /tn "\NexusKey\Watchdog" /f
```

Output mong đợi mỗi dòng: `SUCCESS: The scheduled task "..." was successfully deleted.`

### Verify

Chạy lại 2 lệnh `schtasks /query` ở trên — cả hai phải báo `cannot find the file`.

> Sau khi xoá task `\NexusKey\Watchdog`, folder `\NexusKey\` trong Task Scheduler có thể còn lại nhưng rỗng — vô hại, MMC sẽ tự dọn lần next.

---

## 3. One-liner — chạy hết một lần (an toàn dù không tồn tại)

Copy nguyên block này vào **cmd Run as Administrator**:

```cmd
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v NexusKey /f 2>nul
schtasks /delete /tn "NexusKey" /f 2>nul
schtasks /delete /tn "\NexusKey\Watchdog" /f 2>nul
echo Done.
```

`2>nul` nuốt lỗi "not found" → an toàn dù entry không tồn tại. Chạy xong reboot để chắc chắn không còn process NexusKey nào tự khởi động.

---

## 4. Bonus — kiểm tra Startup folder (nếu bạn từng tạo shortcut tay)

NexusKey không tự tạo shortcut ở đây, nhưng nếu bạn (hoặc installer cũ) từng làm:

```cmd
dir "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\NexusKey*"
del  "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\NexusKey.lnk"
```
