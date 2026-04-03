# NexusKey - Bộ gõ tiếng Việt hiện đại cho Windows

[![Build](https://github.com/phatMT97/NextKey/actions/workflows/build.yml/badge.svg)](https://github.com/phatMT97/NextKey/actions/workflows/build.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Release](https://img.shields.io/github/v/release/phatMT97/NextKey)](https://github.com/phatMT97/NextKey/releases)

<p align="center">
  <img src="docs/images/nextkey-compact.png" alt="NexusKey Compact View" width="250">
  <img src="docs/images/nextkey-expanded.png" alt="NexusKey Expanded View" width="550">
</p>
<p align="center"><em>Giao diện NexusKey: Chế độ thu gọn (trái) và Cài đặt mở rộng (phải)</em></p>

<p align="center">
  <img src="docs/images/nextkey-full-UI.png" alt="NexusKey Full UI" width="800">
</p>
<p align="center"><em>Giao diện đầy đủ NexusKey</em></p>

---

**[Giới thiệu](#giới-thiệu)** | **[Tính năng](#tính-năng)** | **[Cài đặt](#cài-đặt)** | **[Build](#build-từ-mã-nguồn)** | **[Kiến trúc](#kiến-trúc)** | **[English](#english-version)** | **[Credits](#credits)**

---

## Giới thiệu

**NexusKey** là bộ gõ tiếng Việt mã nguồn mở cho Windows, được viết lại hoàn toàn từ [NextKey](https://github.com/phatMT97/NextKey/tree/master) (dựa trên [OpenKey](https://github.com/tuyenvm/OpenKey) của Mai Vũ Tuyên).

Engine mới, kiến trúc mới, C++20, hiệu năng cao, giao diện Glassmorphism.

<a name="privacy-policy"></a>
### Chính sách bảo mật
* **Không ghi phím:** NexusKey không thu thập, lưu trữ, hay gửi phím bạn gõ đi đâu.
* **Không thu thập dữ liệu:** Không có dữ liệu cá nhân nào được gửi lên server.
* **Hoạt động offline:** Phần mềm hoạt động hoàn toàn cục bộ trên máy bạn.
* **Mã nguồn mở:** Bạn có thể tự kiểm chứng bằng cách đọc mã nguồn.

---

## Tính năng

### Phương thức gõ & Bảng mã
- **Kiểu gõ:** Telex, VNI, Simple Telex
- **Bảng mã:** Unicode, TCVN3, VNI Windows, Unicode tổ hợp, Vietnamese Locale
- **Vị trí dấu:** Kiểu mới (oà, uý) và kiểu cũ (òa, úy) tùy chọn

### Kiểm tra chính tả & Phát hiện tiếng Anh
- **Kiểm tra chính tả** — Kiểm tra từ tiếng Việt hợp lệ, giảm thiểu đặt dấu nhầm
- **Gõ tự do** — Cho phép đặt dấu tự do, bỏ qua kiểm tra tiếng Anh (vd: `yes` → `ýe`)
- **Phím tắt tạm tắt** — Solo Ctrl tạm tắt spell check, Double-Alt tạm tắt tiếng Việt cho từ đang gõ

### Chuyển đổi thông minh
- **Smart Switch** — Tự động nhớ chế độ V/E cho từng ứng dụng
- **Loại trừ ứng dụng (Hard)** — Ép English, chặn toggle hoàn toàn cho app chỉ định
- **Loại trừ ứng dụng (Soft)** — Luôn reset về English khi mở/switch sang app, nhưng vẫn cho phép bật V bằng hotkey — lý tưởng cho game fullscreen
- **Tự động tắt khi CJK** — Disable gõ tiếng Việt khi keyboard layout là CJK (Trung, Nhật, Hàn)
- **Cấu hình từng ứng dụng** — Bảng mã và kiểu gõ riêng cho từng app
- **Import/Export** — Nạp và xuất danh sách loại trừ từ file

### Macro & Gõ nhanh
- **Macro** — Gõ tắt tự mở rộng (vd: `addr` → địa chỉ đầy đủ), hỗ trợ xuống hàng
- **Macro trong English mode** — Cho phép macro hoạt động khi đang ở chế độ English
- **Phụ âm nhanh** — cc→ch, gg→gi, nn→ng, và thêm phụ âm đầu/cuối nhanh
- **Tự viết hoa** — Tự động viết hoa chữ đầu câu, hỗ trợ cả macro

### Công cụ chuyển mã (Convert Tool)
- **Bôi đen chuyển mã nhanh** — Bôi đen văn bản rồi nhấn hotkey để chuyển mã tức thì
- **Chuyển mã tuần tự** — Tự động chuyển qua lại giữa các tùy chọn (HOA, thường, Hoa đầu câu, Bỏ dấu...) rồi quay về ban đầu. Chỉ hoạt động khi "Tự động dán + bôi đen" được bật
- **Hỗ trợ đa bảng mã** — Chuyển đổi giữa Unicode, TCVN3, VNI Windows

### Giao diện
- **Glassmorphism UI** — Giao diện trong suốt, blur phía sau, bo góc kiểu Windows 11
- **Auto Theme Sync** — Tự động chuyển Light/Dark đồng bộ real-time với Windows, không cần khởi động lại
- **Màu sắc Icon** — Tùy chọn đổi màu biểu tượng V/E trên system tray theo ý thích
- **Song ngữ** — Giao diện hỗ trợ tiếng Việt và tiếng Anh

### Hệ thống
- **TSF Engine** — Tích hợp Text Services Framework cho ứng dụng hiện đại, hỗ trợ chọn app dùng TSF thay hook
- **Tự cập nhật** — Kiểm tra và cài đặt bản cập nhật tự động
- **Tối ưu bảo mật** — Code được optimize để hạn chế các lỗ hổng bảo mật
- **Hiệu năng cao** — Tối ưu xử lý hệ thống.

---

## Cài đặt

1. Tải phiên bản mới nhất tại **[Releases](https://github.com/phatMT97/NextKey/releases)**.
2. Giải nén và chạy `NexusKey.exe`.
3. *(Khuyến nghị)* Tắt các bộ gõ khác (Unikey, EVKey) để tránh xung đột.

---

## Build từ mã nguồn

### Yêu cầu

- **Windows 10/11**
- **Visual Studio 2022** (workload "Desktop development with C++" + ATL)
- **CMake 3.20+**

Các thư viện phụ thuộc (Sciter SDK, Google Test, toml++) đã có sẵn trong `extern/`.

### Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target NextKeyApp
```

### Chạy test

```powershell
cmake --build build --config Release --target NextKeyTests
ctest --test-dir build --build-config Release --output-on-failure
```

---

## Kiến trúc

NexusKey gồm ba lớp:

| Lớp | Target | Mô tả |
|-----|--------|-------|
| **NextKeyEngine** | Static lib | Engine gõ tiếng Việt thuần C++20 (Telex, VNI), kiểm tra chính tả, chuyển bảng mã. Không phụ thuộc platform. |
| **NextKeyCore** | Static lib | Lớp platform — shared memory IPC, quản lý config, smart switch. |
| **NextKeyApp** | Win32 EXE | Ứng dụng GUI với Sciter.JS, hook engine, tray icon, tự cập nhật. |
| **NextKeyTSF** | DLL | Tích hợp Text Services Framework — đăng ký như Windows input method. |

---

## English Version

<details>
<summary>Click to expand English version</summary>

### About

**NexusKey** is an open-source Vietnamese Input Method Editor (IME) for Windows, completely rewritten from [NextKey](https://github.com/phatMT97/NextKey/tree/master) (based on [OpenKey](https://github.com/tuyenvm/OpenKey) by Mai Vu Tuyen).

New engine, new architecture, C++20, high performance, Glassmorphism UI.

### Privacy Policy
* **No Keylogging:** NexusKey does not collect, store, or transmit your keystrokes.
* **No Data Collection:** No personal data is sent to any server.
* **Offline First:** The software operates entirely locally on your machine.
* **Open Source:** You can verify this behavior by reviewing our source code.

### Features

**Input Methods & Code Tables**
- **Input methods:** Telex, VNI, Simple Telex
- **Code tables:** Unicode, TCVN3, VNI Windows, Unicode Compound, Vietnamese Locale
- **Tone placement:** Modern (oà, uý) and classic (òa, úy) options

**Spell Check & English Detection**
- **Spell checker** — Vietnamese word validation, reduces accidental tone placement on English words
- **Free typing mode** — Bypass English detection, allows free tone placement (e.g., `yes` → `ýe`)
- **Quick-disable shortcuts** — Solo Ctrl temporarily disables spell check; Double-Alt temporarily disables Vietnamese for current word

**Smart Switching**
- **Smart Switch** — Automatically remembers V/E mode per application
- **Exclude Apps (Hard)** — Force English and block toggle completely for specified apps
- **Exclude Apps (Soft)** — Always reset to English when opening/switching to an app, but still allows toggling to Vietnamese via hotkey — ideal for fullscreen games
- **Auto-disable on CJK** — Disables Vietnamese input when keyboard layout is CJK (Chinese, Japanese, Korean)
- **Per-app configuration** — Code table and input method overrides per application
- **Import/Export** — Import and export excluded apps lists from file

**Macros & Quick Typing**
- **Macros** — Text expansion shortcuts (e.g., `addr` → full address), supports newlines
- **Macros in English mode** — Allows macro expansion while in English mode
- **Quick consonants** — cc→ch, gg→gi, nn→ng, plus quick start/end consonant shortcuts
- **Auto-capitalize** — Automatically capitalizes first letter after sentence-ending punctuation, including macro output

**Convert Tool**
- **Quick convert via selection** — Select text and press hotkey to convert instantly
- **Sequential conversion** — Automatically cycles through conversion options (UPPERCASE, lowercase, Title Case, Remove accents...) then returns to original. Only active when "Auto paste + select" is enabled
- **Multi-encoding support** — Convert between Unicode, TCVN3, VNI Windows

**User Interface**
- **Glassmorphism UI** — Transparent, backdrop blur, rounded corners in Windows 11 style
- **Auto Theme Sync** — Automatically switches Light/Dark in real-time with Windows, no restart needed
- **Icon Color** — Customizable V/E icon colors on the system tray
- **Bilingual** — UI supports both Vietnamese and English

**System**
- **TSF Engine** — Text Services Framework integration for modern applications, with per-app TSF selection
- **Auto-update** — Built-in update checker and installer
- **Security hardened** — Code optimized to minimize security vulnerabilities
- **High performance** — Optimized system processing

### Installation

1. Download the latest version from **[Releases](https://github.com/phatMT97/NextKey/releases)**.
2. Extract and run `NexusKey.exe`.
3. *(Recommended)* Disable other IMEs (Unikey, EVKey) to avoid conflicts.

### Building

**Prerequisites:** Windows 10/11, Visual Studio 2022 (Desktop C++ + ATL), CMake 3.20+

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target NextKeyApp
```

### Architecture

| Layer | Target | Description |
|-------|--------|-------------|
| **NextKeyEngine** | Static lib | Pure C++20 Vietnamese input engines (Telex, VNI), spell checker, code table converter. Zero platform dependencies. |
| **NextKeyCore** | Static lib | Platform layer — shared memory IPC, configuration management, smart switch. |
| **NextKeyApp** | Win32 EXE | GUI application with Sciter.JS UI, hook engine, tray icon, auto-update. |
| **NextKeyTSF** | DLL | Text Services Framework integration — registers as a Windows input method. |

</details>

---

## Credits

- Kế thừa từ [NextKey](https://github.com/phatMT97/NextKey/tree/master), lấy cảm hứng từ [OpenKey](https://github.com/tuyenvm/OpenKey) của Mai Vũ Tuyên
- Giao diện bởi [Sciter.JS](https://sciter.com/)
- Đọc config bởi [toml++](https://github.com/marzer/tomlplusplus)
- Testing bởi [Google Test](https://github.com/google/googletest)

### Top Testers
Cảm ơn các thành viên cộng đồng đã test và góp ý:
- Zenfas
- huntersun
- haihv8x
- os-hoanghv

## License

Dự án sử dụng [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html) — xem file [LICENSE](LICENSE).
