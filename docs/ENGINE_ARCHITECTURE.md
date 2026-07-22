# Giới thiệu về Kiểm tra Chính tả Nâng cao

Từ **VKey v4.3**, VKey có thêm **Kiểm tra Chính tả Nâng cao**. Đây là tính năng tùy chọn, dùng để phát hiện và sửa một số lỗi gõ phổ biến, chẳng hạn gõ nhanh bị đảo chữ cái: `hcaof` có thể được sửa thành `chào`.

Tính năng này được hỗ trợ bởi một engine phụ trợ viết bằng Rust, phân phối dưới dạng thư viện động: `vkey_engine.dll` trên Windows hoặc `libvkey_engine.so` trên Linux.

Engine này:

- tắt mặc định;
- chạy hoàn toàn trên máy người dùng;
- không có chức năng mạng;
- có thể gỡ bỏ bất cứ lúc nào;
- không ảnh hưởng đến chức năng gõ tiếng Việt cốt lõi của VKey.

Nếu engine Rust không khả dụng, VKey sẽ tự động quay lại engine C++ mã nguồn mở tích hợp sẵn.

Với các câu hỏi về license, copyright hoặc lý do thiết kế chi tiết hơn, xem **[ENGINE_FAQ.md](ENGINE_FAQ.md)**.

---

# Trả lời nhanh

## Tính năng này có bắt buộc không?

Không. VKey hoạt động bình thường nếu không dùng Kiểm tra Chính tả Nâng cao.

## Tôi có thể gỡ engine này không?

Có. Chỉ cần xóa `vkey_engine.dll` trên Windows hoặc `libvkey_engine.so` trên Linux khỏi thư mục cài đặt.

## Engine này có gửi phím gõ của tôi đi đâu không?

Không. Engine chạy cục bộ và không có chức năng mạng.

## Nếu gỡ engine, VKey còn gõ tiếng Việt được không?

Có. Việc gỡ engine chỉ tắt Kiểm tra Chính tả Nâng cao. Các chức năng gõ tiếng Việt thông thường vẫn tiếp tục hoạt động qua engine C++ mã nguồn mở.

---

# Vì sao engine được phân phối riêng?

Advanced Spell Check Engine được phát triển như một thành phần độc lập với ứng dụng VKey cốt lõi.

Cách tách riêng này giúp engine có thể phát triển theo nhịp riêng, trong khi đường đi nhập liệu chính của VKey vẫn là mã nguồn mở và có thể kiểm tra độc lập. Phần mã nguồn mở của VKey vẫn chịu trách nhiệm nạp thư viện, truyền dữ liệu vào engine, nhận kết quả trả về và quyết định khi nào dùng engine nào.

Việc phân phối riêng cũng giúp VKey giữ lại một phần giá trị phát triển của dự án, hạn chế các bản phân phối đổi tên sơ sài, đồng thời cho phép ứng dụng chính tiếp tục được cung cấp miễn phí cho mọi người.

---

# Rust Engine khác gì engine C++ mặc định?

Engine C++ là engine mã nguồn mở tích hợp sẵn của VKey. Đây là engine mặc định, xử lý các chức năng gõ tiếng Việt thông thường và vẫn hoạt động đầy đủ nếu Rust Engine không có mặt.

Rust Engine không thay thế toàn bộ engine C++. Nó chỉ được dùng khi người dùng bật Kiểm tra Chính tả Nâng cao, với mục tiêu xử lý thêm các lỗi gõ mà engine mặc định không cố sửa, chẳng hạn lỗi gõ nhanh, đảo vị trí chữ cái hoặc một số trường hợp cần kiểm tra chính tả kỹ hơn.

Nói ngắn gọn:

- nếu bạn chỉ cần gõ tiếng Việt bình thường, engine C++ là đủ;
- nếu bạn muốn VKey tự phát hiện và sửa thêm một số lỗi đánh máy, có thể bật Kiểm tra Chính tả Nâng cao để dùng Rust Engine;
- nếu tắt hoặc gỡ Rust Engine, VKey vẫn quay lại engine C++ và tiếp tục gõ tiếng Việt bình thường.

---

# Minh bạch, quyền riêng tư và bảo mật

Vì bộ gõ xử lý dữ liệu nhập từ bàn phím, phần tích hợp giữa VKey và engine được thiết kế để có thể kiểm tra trực tiếp.

Các phần sau nằm trong mã nguồn mở của VKey:

- nạp thư viện động;
- giao tiếp với engine;
- truyền dữ liệu đầu vào;
- nhận kết quả đầu ra;
- tự động quay lại engine C++ khi engine Rust không khả dụng.

Nhờ đó, người dùng có thể kiểm tra VKey gửi dữ liệu gì vào engine và nhận lại dữ liệu gì.

Engine Rust hoạt động như một thư viện cục bộ. Nó không giao tiếp với máy chủ bên ngoài. Ứng dụng VKey chính cũng được ký mã thông qua SignPath Open Source Signing Program, giúp người dùng xác minh tính xác thực của các bản phát hành chính thức.

---

# Cách tắt hoặc gỡ bỏ

Người dùng luôn có quyền quyết định có dùng Kiểm tra Chính tả Nâng cao hay không.

Để không dùng engine tùy chọn:

1. Tắt Kiểm tra Chính tả Nâng cao trong Settings.
2. Xóa `vkey_engine.dll` trên Windows hoặc `libvkey_engine.so` trên Linux.

Sau đó, VKey sẽ tự động dùng engine C++ mã nguồn mở. Không cần cấu hình thêm.

---

# Tìm hiểu thêm

Nếu bạn quan tâm đến lý do engine hiện chưa mở nguồn, dual licensing, copyright, khả năng tương thích với AGPL, lịch sử dự án hoặc các lý do kỹ thuật khác, vui lòng xem:

- **[ENGINE_FAQ.md](ENGINE_FAQ.md)**

---

# About Advanced Spell Check

Starting with **VKey v4.3**, VKey includes **Advanced Spell Check**. This optional feature detects and corrects some common typing mistakes, such as transposed letters from fast typing: `hcaof` can become `chào`.

The feature is powered by an auxiliary Rust engine distributed as a dynamic library: `vkey_engine.dll` on Windows or `libvkey_engine.so` on Linux.

The engine is:

- disabled by default;
- fully local;
- built without networking functionality;
- removable at any time;
- not required for VKey's core Vietnamese input functionality.

If the Rust engine is unavailable, VKey automatically falls back to the built-in open-source C++ engine.

For licensing, copyright, and broader design rationale, see **[ENGINE_FAQ.md](ENGINE_FAQ.md)**.

---

# Quick Answers

## Is this feature required?

No. VKey works normally without Advanced Spell Check.

## Can I remove the engine?

Yes. Delete `vkey_engine.dll` on Windows or `libvkey_engine.so` on Linux from the installation directory.

## Does it send my keystrokes anywhere?

No. The engine runs locally and contains no networking functionality.

## Will Vietnamese input still work if I remove it?

Yes. Removing the engine only disables Advanced Spell Check. Standard Vietnamese input features continue to work through the open-source C++ engine.

---

# Why is the engine distributed separately?

The Advanced Spell Check Engine is developed as an independent component from the core VKey application.

Keeping it separate lets the engine evolve independently while VKey's main input path remains open source and independently auditable. VKey's open-source code is still responsible for loading the library, passing input to the engine, receiving output, and deciding which engine to use.

Separate distribution also helps VKey preserve part of the project's own development value, reduce low-effort rebranded redistributions, and keep the main application freely available to everyone.

---

# How is the Rust engine different from the default C++ engine?

The C++ engine is VKey's built-in open-source engine. It is the default engine, handles normal Vietnamese input, and continues to work fully when the Rust engine is not present.

The Rust engine does not replace the entire C++ engine. It is only used when Advanced Spell Check is enabled, and its role is to handle additional typo-correction cases that the default engine does not try to fix, such as fast typing mistakes, transposed letters, or cases that need stricter spelling checks.

In short:

- if you only need normal Vietnamese input, the C++ engine is enough;
- if you want VKey to detect and fix some typing mistakes automatically, you can enable Advanced Spell Check to use the Rust engine;
- if the Rust engine is disabled or removed, VKey falls back to the C++ engine and normal Vietnamese input continues to work.

---

# Transparency, Privacy, and Security

Because keyboard input software handles sensitive user input, the integration between VKey and the engine is designed to be directly inspectable.

The following parts are implemented in VKey's open-source codebase:

- loading the dynamic library;
- communicating with the engine;
- passing input data;
- receiving output;
- falling back to the C++ engine when the Rust engine is unavailable.

This lets users inspect what data VKey passes to the engine and what data it receives back.

The Rust engine runs as a local library. It does not communicate with external servers. The main VKey application is also code signed through the SignPath Open Source Signing Program to help users verify the authenticity of official releases.

---

# How to Disable or Remove It

Users remain in control of whether Advanced Spell Check is used.

To stop using the optional engine:

1. Disable Advanced Spell Check in Settings.
2. Delete `vkey_engine.dll` on Windows or `libvkey_engine.so` on Linux.

VKey will then automatically use the built-in open-source C++ engine. No additional configuration is required.

---

# Learn More

For more context on why the engine is currently closed source, dual licensing, copyright ownership, AGPL compatibility, project history, and other technical rationale, see:

- **[ENGINE_FAQ.md](ENGINE_FAQ.md)**
