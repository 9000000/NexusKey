# VKey Browser extension

> [!WARNING]
> **Experimental feature:** the extension is not yet distributed through the
> Chrome Web Store or Firefox AMO. Install it only with a VKey build that
> includes `VKeyBrowserHost.exe`.

VKey can accept a per-domain route from the companion
[VKey Browser](https://github.com/phatMT97/VKey-Browser) extension.

The extension offers three routes:

- **Default:** retain VKey's normal per-application behavior.
- **English:** temporarily bypass Vietnamese processing for the focused domain
  without changing the user's logical V/E setting.
- **TSF compatibility:** route the domain through VKey TSF. This is intended for
  browser editors that duplicate or attach replacement text after emoji, such
  as the XenForo cases reported in issue #92. VKey's TSF application support
  must be enabled; otherwise this route safely falls back to the normal hook.

## Yêu cầu

- Windows 10/11 và một bản VKey có `VKeyBrowserHost.exe` nằm cùng thư mục với
  `VKey.exe` hoặc `VKeyClassic.exe`.
- Repo [VKey-Browser](https://github.com/phatMT97/VKey-Browser) đã được tải và
  giải nén; thư mục được chọn khi cài phải chứa `manifest.json`.
- Muốn dùng route **TSF tương thích**, bật hỗ trợ ứng dụng TSF trong VKey.

Khi khởi động, VKey tạo native-messaging manifest theo user tại
`%APPDATA%\VKey\native-messaging` và đăng ký cho Chrome, Edge, Brave, Vivaldi,
Opera và Firefox. Thao tác này idempotent và được bỏ qua nếu không tìm thấy
`VKeyBrowserHost.exe`.

## Cài trên Chrome, Edge, Brave, Vivaldi hoặc Opera

1. Thoát VKey, kiểm tra `VKeyBrowserHost.exe` nằm cạnh file chạy VKey, rồi mở
   lại VKey một lần để đăng ký native host.
2. Mở trang quản lý extension:
   - Chrome: `chrome://extensions`
   - Edge: `edge://extensions`
   - Brave: `brave://extensions`
   - Vivaldi: `vivaldi://extensions`
   - Opera: `opera://extensions`
3. Bật **Developer mode**.
4. Chọn **Load unpacked** và chọn thư mục VKey-Browser có `manifest.json`.
5. Ghim VKey Browser lên toolbar để đổi route của trang hiện tại.

## Cài tạm trên Firefox

1. Chạy VKey một lần như bước 1 phía trên.
2. Trong thư mục VKey-Browser, chạy `npm run build:firefox` để tạo manifest
   dùng `background.scripts` riêng cho Firefox.
3. Mở `about:debugging#/runtime/this-firefox`.
4. Chọn **Load Temporary Add-on** rồi chọn `dist/firefox/manifest.json`.

Không chọn `manifest.json` ở thư mục gốc cho Firefox; file đó là gói Chromium
Manifest V3 và chỉ khai báo `background.service_worker`.

Firefox sẽ gỡ temporary add-on khi khởi động lại; cần load lại cho đến khi có
bản XPI được ký qua AMO.

## Sử dụng

Extension có ba route:

- **Theo VKey:** dùng trạng thái và cấu hình VKey bình thường.
- **Luôn gõ English:** tạm bỏ xử lý tiếng Việt trên tên miền, không sửa trạng
  thái V/E gốc.
- **TSF tương thích:** chuyển tên miền qua TSF cho editor/forum bị lỗi với hook.

Công tắc **Bật điều hướng theo website** được bật mặc định và có ở cả popup lẫn
trang quản lý tên miền. Khi tắt, extension giữ nguyên danh sách rule nhưng mọi
hostname đều được gửi dưới route **Theo VKey**; bật lại sẽ áp dụng các rule đã
lưu ngay lập tức.

Rule được áp dụng cho hostname và các subdomain của nó. Extension theo dõi đổi
tab, navigation và focus cửa sổ, nên không cần mở popup lại sau mỗi lần chuyển.

Ví dụ:

1. Để trạng thái VKey gốc là V.
2. Trên `google.com`, chọn **Theo VKey**.
3. Trên `voz.vn`, chọn **Luôn gõ English**.
4. Chuyển tab Google → VOZ → Google: chế độ hiệu lực tự đổi V → E → V.

**Theo VKey** không phải route ép V. Nếu trạng thái VKey gốc là E thì Google
trong ví dụ trên vẫn là E.

## Xử lý lỗi kết nối

Nếu popup lưu rule nhưng VKey không đổi chế độ:

1. Xem trạng thái trong popup. **Chưa kết nối VKeyBrowserHost** nghĩa là trình
   duyệt chưa mở được native host.
2. Kiểm tra `VKeyBrowserHost.exe` nằm cạnh đúng file VKey đang chạy. Các bản
   VKey cũ không kèm file này sẽ không dùng được extension.
3. Thoát hoàn toàn VKey rồi mở lại để tạo manifest trong
   `%APPDATA%\VKey\native-messaging` và đăng ký registry theo user.
4. Khởi động lại browser và reload extension.
5. Với TSF, xác nhận hỗ trợ ứng dụng TSF đang bật trong VKey; nếu chưa bật,
   route TSF sẽ an toàn quay về hook bình thường.

## Privacy and failure behavior

Only these values cross the extension/native boundary: protocol version,
browser executable name, focused state, effective route, and hostname. Full
URLs, paths, query strings, page contents, and keystrokes are neither requested
nor transmitted. The native host rejects messages over 8 KiB, unknown fields,
unsupported executable names, and non-hostname characters.

The host publishes a fixed-size versioned seqlock mapping. HookEngine normally
checks one 32-bit generation value per key; JSON parsing and browser APIs stay
outside the keyboard hook. State expires after five seconds if a browser or
extension crashes. Blur/EOF clears only state owned by that native connection,
so an unfocused browser's heartbeat cannot erase the currently focused
browser's route. An app-level exclusion still has higher precedence than a
domain route.

## Address-bar limitation (#100)

Browser extension APIs do not expose text typed in the normal address bar.
Consequently the extension can apply a hostname route after navigation or tab
focus, but cannot detect `google.com` while the user is still typing it into the
omnibox. Implementations based on UI Automation were deliberately rejected:
they are browser-specific, fragile, and too expensive to place near the input
hook.

## TSF visual behavior

VKey registers its `ITfDisplayAttributeProvider` and applies its
`TF_LS_NONE` GUID atom to every live composition range. Supported hosts should
therefore not draw the usual composition underline/highlight. A host that
ignores TSF display attributes may still impose its own visual treatment.
