# Về Engine Kiểm Tra Chính Tả Nâng Cao (Rust Engine)

Từ phiên bản **v4.3**, VKey hỗ trợ chức năng **"Kiểm tra chính tả nâng cao"** (tự động nhận diện và sửa các lỗi gõ sai phím hoặc đảo ký tự thông minh, ví dụ: gõ `hcaof` tự động sửa thành `chào`). Chức năng này được vận hành bởi một engine phụ trợ độc lập viết bằng ngôn ngữ Rust, được biên dịch dưới dạng thư viện động (`vkey_engine.dll` trên Windows / `libvkey_engine.so` trên Linux) và hiện tại đang ở trạng thái nguồn đóng (closed-source).

Tài liệu này cung cấp các thông tin minh bạch, lý do kỹ thuật và quyền kiểm soát của người dùng đối với phần engine này.

---

## 1. Lý do đóng mã nguồn Engine này?

*   **Đây là dự án phi thương mại (phát triển vì sở thích):** VKey được phát triển chủ yếu phục vụ nhu cầu cá nhân và chia sẻ miễn phí cho cộng đồng trải nghiệm. Engine Rust này cũng là một dự án nghiên cứu riêng của tác giả.
*   **Hạn chế việc chia nhỏ và thiếu đóng góp tập trung:** Thay vì mã nguồn hoặc ý tưởng bị phân mảnh (fragmentation) thành quá nhiều công cụ/bản fork rời rạc mà không có sự cộng tác hay đóng góp ngược lại, việc giữ engine nâng cao này nguồn đóng giúp VKey bảo toàn một bản sắc và chất lượng đồng nhất, đồng thời khuyến khích cộng đồng tập trung phát triển chung cho một dự án thay vì phân tán tài nguyên.

---

## 2. Tính minh bạch và Bảo mật (Privacy & Security)

Tác giả hiểu rằng việc tích hợp một thư viện nguồn đóng vào một bộ gõ tiếng Việt mã nguồn mở dễ gây ra những nghi ngại từ phía người dùng (ví dụ: nguy cơ Keylogger). VKey giải quyết triệt để vấn đề này qua các cơ chế sau:

*   **Giao tiếp I/O mở hoàn toàn:** Toàn bộ phần mã nguồn điều khiển, tải thư viện động và gửi nhận dữ liệu giữa VKey (C++) và Engine (Rust) đều nằm trong phần mã nguồn mở của VKey (xem các phần tích hợp API trong thư mục `src/`). Bạn hoàn toàn có thể kiểm tra xem ứng dụng truyền tham số đầu vào là gì và nhận kết quả đầu ra là gì. Không hề có bất kỳ dữ liệu nhạy cảm hay thông tin gõ phím nào của bạn bị âm thầm lưu trữ hoặc gửi ra ngoài.
*   **Không kết nối Internet:** Engine hoạt động hoàn toàn offline dưới dạng một thư viện cục bộ (local library), không có bất kỳ quyền hạn hay logic mạng nào để giao tiếp với máy chủ bên ngoài.
*   **Ký số xác thực (Code Signing):** Ứng dụng chính VKey (`VKey.exe` và các thành phần chính) được ký số chính thức bởi **SignPath Foundation** để chứng minh tính nguyên bản và độ an toàn. Riêng file thư viện engine động (`vkey_engine.dll` / `libvkey_engine.so`) không nằm trong phạm vi ký số này; tuy nhiên, độ an toàn của nó đã được bảo chứng qua cơ chế chạy offline cục bộ và giao tiếp I/O mở hoàn toàn nêu trên.

---

## 3. Quyền kiểm soát tuyệt đối của người dùng (User Control)

VKey được thiết kế để đặt quyền lựa chọn của bạn lên hàng đầu:

*   **Mặc định Tắt:** Tính năng kiểm tra chính tả nâng cao này mặc định được tắt. Bạn chỉ sử dụng nó khi chủ động bật trong giao diện Cài đặt của VKey.
*   **Dễ dàng loại bỏ & Cơ chế tự động Fallback:** Nếu bạn hoàn toàn không tin tưởng hoặc không muốn có bất kỳ thành phần nguồn đóng nào trên máy tính của mình, bạn chỉ cần thực hiện:
    1.  Tắt tính năng trong Cài đặt.
    2.  Xóa file `vkey_engine.dll` (trên Windows) hoặc `libvkey_engine.so` (trên Linux) khỏi thư mục cài đặt VKey.
    3.  VKey sẽ phát hiện thư viện bị thiếu và **tự động chuyển hướng (fallback)** về sử dụng engine C++ mã nguồn mở truyền thống tích hợp sẵn trong ứng dụng. Mọi hoạt động cơ bản của bộ gõ vẫn diễn ra bình thường, mượt mà và an toàn.

---

## 4. Về giấy phép (Licensing)

Một số người thắc mắc: "AGPL/GPL không cho phép link với thư viện nguồn đóng, vậy đây có phải vi phạm license?" Câu trả lời ngắn gọn: **không**, vì license ràng buộc người *được cấp phép*, không ràng buộc *chủ sở hữu bản quyền*.

*   **Một tác giả, toàn quyền:** Toàn bộ mã nguồn VKey trên branch phát hành thuộc về một tác giả duy nhất — tự kiểm chứng bằng `git shortlog -sne HEAD`. AGPL-3.0 là điều kiện tác giả đặt ra cho người **nhận** code; bản thân tác giả giữ toàn quyền với code của mình, bao gồm quyền kết hợp nó với thư viện đóng cũng do chính mình viết, hoặc cấp thêm giấy phép thương mại song song.
*   **Dual-licensing là mô hình chuẩn:** AGPL + giấy phép thương mại là cách Qt, MySQL, MongoDB đã vận hành hàng chục năm. Điều kiện duy nhất để mô hình này hợp lệ là sở hữu 100% bản quyền phần mã AGPL — điều kiện VKey thỏa mãn.
*   **Engine là tác phẩm độc lập:** Engine Rust là công trình riêng của cùng tác giả, không chứa hay phái sinh từ mã GPL của bên thứ ba, và được nạp lúc chạy (runtime) qua C ABI — không link tĩnh vào phần AGPL.
*   **Với người phân phối lại (redistribute):** nếu bạn muốn tuân thủ AGPL theo cách hiểu chặt chẽ nhất, chỉ cần loại bỏ file engine khỏi bản phân phối của bạn — ứng dụng tự fallback về engine mở và hoạt động đầy đủ.

---

# About the Advanced Spell Check Engine (Rust Engine)

Starting from version **v4.3**, VKey introduces the **"Advanced Spell Check"** feature (which automatically detects and intelligently corrects typing errors or transposed letters, e.g., typing `hcaof` automatically corrects to `chào`). This feature is powered by an independent auxiliary engine written in Rust, compiled as a dynamic library (`vkey_engine.dll` on Windows / `libvkey_engine.so` on Linux), and is currently closed-source.

This document provides transparent information, technical reasons, and user control details regarding this engine.

---

## 1. Why is this Engine Closed-source?

*   **A Non-commercial Hobby Project:** VKey is primarily developed for personal use and shared for free with the community. This Rust engine is a private research project of the developer.
*   **Preventing Fragmentation and Promoting Focused Collaboration:** Rather than having logic and features fragmented across numerous separate tools or forks without active collaboration or contributions back to the core, keeping this engine closed-source helps VKey maintain a unique identity and unified quality. It encourages the community to focus on collaborating on one core project rather than dispersing resources across scattered tools.

---

## 2. Transparency & Security

We understand that shipping a closed-source library inside an open-source Vietnamese keyboard input method can raise concerns (such as keylogging risks). VKey addresses these concerns transparently:

*   **Open I/O Interface:** All the logic for loading the dynamic library and transmitting data between VKey (C++) and the Engine (Rust) is written directly in VKey's open-source codebase (refer to the API integration files under `src/`). You can audit exactly what input arguments are sent to the engine and what output results are received. No sensitive keystroke data is stored or transmitted outside.
*   **Zero Network Activity:** The engine runs entirely offline as a local library. It does not have any network capability or logic to communicate with remote servers.
*   **Code Signing:** The main VKey application (`VKey.exe` and core components) is officially signed by the **SignPath Foundation** to verify authenticity and safety. The auxiliary engine library (`vkey_engine.dll` / `libvkey_engine.so`) is not signed, but its security is guaranteed by its local offline execution and the fully auditable open I/O interface described above.

---

## 3. Absolute User Control

VKey puts your choice first:

*   **Disabled by Default:** The advanced spell check feature is disabled by default. It only loads and runs when you manually enable it in VKey's Settings.
*   **Easy Removal & Auto Fallback:** If you do not want any closed-source binaries on your computer, you can easily remove it:
    1.  Turn off the feature in VKey's Settings.
    2.  Delete the file `vkey_engine.dll` (on Windows) or `libvkey_engine.so` (on Linux) from the VKey installation directory.
    3.  VKey will detect the missing library and **automatically fall back** to using the built-in, fully open-source C++ spell check engine. All basic functionalities of VKey will remain intact, smooth, and completely secure.

---

## 4. Licensing

Some may ask: "AGPL/GPL forbids linking with closed-source libraries — isn't this a license violation?" Short answer: **no**, because a license binds the *licensee*, not the *copyright holder*.

*   **Single author, full rights:** All VKey source code on the release branch belongs to a single author — verify with `git shortlog -sne HEAD`. AGPL-3.0 is the condition the author sets for *recipients* of the code; the author retains full rights over their own work, including combining it with a closed library they also wrote, or offering a parallel commercial license.
*   **Dual-licensing is a standard model:** AGPL + commercial licensing is how Qt, MySQL, and MongoDB have operated for decades. The only requirement is owning 100% of the copyright on the AGPL-licensed code — a requirement VKey satisfies.
*   **The engine is an independent work:** The Rust engine is a separate work by the same author. It does not contain or derive from any third-party GPL code, and it is loaded at runtime via a C ABI — never statically linked into the AGPL code.
*   **For redistributors:** if you want to comply with the AGPL under its strictest reading, simply remove the engine file from your distribution — the app falls back to the open engine and remains fully functional.
