# Một số giải thích về kiến trúc, license và nguồn gốc của VKey

Gần đây có một số trao đổi xoay quanh việc VKey sử dụng Rust Engine đóng nguồn, câu chuyện license và nguồn gốc của dự án. Mình viết lại ở đây để trả lời tập trung hơn, thay vì lặp lại từng ý trong nhiều bình luận rời rạc.

Điều mình mong muốn không phải là mọi người tin vào lời mình, mà là có đủ thông tin để tự kiểm chứng. Phần lớn các điểm dưới đây đều có thể đối chiếu trực tiếp từ repository, lịch sử commit hoặc tài liệu công khai.

Trước khi đi vào từng câu hỏi, cần nói rõ phạm vi của Rust Engine trong kiến trúc VKey.

VKey không phải một bộ gõ đóng nguồn hoàn toàn. Phần hạ tầng tiếp nhận và điều phối dữ liệu gõ phím vẫn được công khai, bao gồm hook, TSF, IPC, luồng xử lý keystroke, cơ chế lựa chọn và nạp engine, cùng giao diện C ABI giữa VKey và engine.

Theo mặc định, VKey sử dụng engine C++ mã nguồn mở. Rust Engine là một implementation tùy chọn, được nạp ở runtime thông qua C ABI. Nếu Rust Engine không có hoặc bị gỡ bỏ, VKey sẽ quay lại engine C++ mặc định. Khi chế độ Advanced Spell Suggest bị tắt, module này cũng không tham gia vào quá trình xử lý.

Vì vậy, khi nói đến phần đóng nguồn trong VKey, mình đang nói đến một engine xử lý tiếng Việt tùy chọn, không phải toàn bộ bộ gõ hay toàn bộ đường đi của dữ liệu gõ phím.

---

# 1. Vì sao engine bỏ dấu lại cần đóng nguồn?

Việc đóng nguồn một thành phần không đồng nghĩa với mục tiêu thương mại. Hai chuyện này không gắn trực tiếp với nhau.

Lý do chính của mình khá đơn giản: mình muốn giữ lại một phần giá trị riêng của VKey. Phần lớn mã nguồn của dự án vẫn được phát hành công khai để mọi người có thể học hỏi, kiểm chứng hoặc đóng góp. Tuy nhiên, những cải tiến mình dành nhiều thời gian nghiên cứu cũng là một phần tạo nên bản sắc của dự án.

Mã nguồn mở không nhất thiết có nghĩa là mọi thành phần của một dự án đều phải được công khai. Điều quan trọng hơn, theo mình, là người dùng có thể kiểm chứng phần mềm tiếp nhận dữ liệu gì, điều phối dữ liệu ra sao, có lựa chọn thay thế hay không, và có muốn sử dụng thành phần tùy chọn đó hay không.

Nếu nhìn theo chiều ngược lại, lập luận "engine bỏ dấu không có gì giá trị, ai cũng viết được" cũng dẫn tới một kết luận khác: nếu nó thật sự đơn giản và không có giá trị đáng kể, thì việc nó đóng hay mở nguồn cũng không tạo ra khác biệt lớn về mặt sử dụng.

Với mình, quyết định này xuất phát từ cách phát triển VKey trong dài hạn, không phải từ mong muốn thương mại hóa.

---

# 2. Nếu chỉ dùng cho bản thân thì sao không mở hết?

Đây là một câu hỏi hợp lý, nhưng cần phân biệt giữa tính minh bạch của dự án và phạm vi mà tác giả lựa chọn công khai.

Khi một phần mã nguồn được mở ra, nó không chỉ cho phép người khác đọc và học hỏi. Trong thực tế, nó thường kéo theo kỳ vọng về sửa lỗi, tiếp nhận issue, review pull request, hỗ trợ tính năng mới và duy trì tương thích lâu dài.

Không phải dự án nào cũng muốn, hoặc có đủ nguồn lực, để mở rộng phạm vi cam kết theo hướng đó.

Ngoài ra, trong quá trình phát triển VKey, mình từng thấy một số ý tưởng hoặc thành quả được mang sang repository khác nhưng hiếm khi đi kèm với pull request đóng góp ngược lại, hoặc một ghi nhận rõ ràng về nguồn tham khảo. Điều này không có nghĩa mọi người đều như vậy. Mã nguồn mở vẫn là môi trường rất tốt để học hỏi và kế thừa lẫn nhau. Nhưng tác giả cũng có quyền cân nhắc phần nào nên tiếp tục công khai, phần nào nên giữ riêng để bảo vệ công sức phát triển.

Mục tiêu của mình không phải là ngăn người khác học hỏi, mà là giới hạn phạm vi bảo trì của phần mã nguồn công khai, đồng thời giữ lại một phần đầu tư riêng cho hướng phát triển của VKey.

---

# 3. AGPL/GPL có cấm liên kết với thành phần đóng nguồn không?

Điểm này thường bị hiểu nhầm vì hai khái niệm khác nhau bị trộn lẫn:

- quyền của người nhận mã nguồn;
- quyền của chủ sở hữu copyright.

Về nguyên tắc copyright/license, cần tách quyền của người nhận mã nguồn khỏi quyền của chủ sở hữu copyright. GNU GPL FAQ nêu rõ GPL là license mà developer cấp cho người khác để sử dụng, phân phối và sửa đổi chương trình; bản thân developer/copyright holder không bị GPL ràng buộc theo cùng cách như người nhận license. GNU cũng nêu rằng copyright holder có thể phát hành cùng một mã nguồn dưới nhiều license song song, trong đó một license có thể là GPL.

Nói cách khác, license như GPL hoặc AGPL ràng buộc người nhận mã nguồn theo license đó. Nó không tước quyền của chủ sở hữu copyright đối với phần mã nguồn mà họ thật sự sở hữu.

Nếu một người nắm toàn bộ quyền tác giả của phần mã nguồn đó, họ có quyền phát hành theo AGPL, hoặc kết hợp với một thành phần đóng nguồn do chính họ sở hữu, miễn là phần mã nguồn AGPL vẫn được phân phối đúng theo điều kiện của AGPL. Trên cơ sở đó, VKey — được phát hành theo AGPL — có thể nạp một Rust Engine đóng nguồn do cùng tác giả sở hữu.

VKey từng có thêm một file LICENSE-COMMERCIAL, cung cấp một lựa chọn cấp phép thay cho AGPL khi cần tích hợp theo điều kiện khác. Trong thực tế, đến nay chưa có nhu cầu thực tế cụ thể cho lựa chọn này — người dùng bộ gõ tiếng Việt quen với việc sử dụng miễn phí hoặc ủng hộ tự nguyện, không phải mua license — nên mình đã bỏ file này. VKey hiện chỉ phát hành theo một license duy nhất: AGPL-3.0.

Điều kiện quan trọng là người phát hành phải thật sự sở hữu copyright của phần mã nguồn liên quan. Điểm này có thể kiểm chứng trực tiếp từ repository:

```bash
git clone https://github.com/phatMT97/VKey
cd VKey
git shortlog -sne Main
```

Theo dữ liệu hiện tại trên branch phát hành, phần mã nguồn liên quan do một tác giả nắm copyright, nên quyền quyết định cách cấp phép vẫn nằm ở chủ sở hữu đó.

Trong trường hợp người khác fork hoặc phân phối lại VKey, họ phải tuân thủ các điều kiện của AGPL. Ngược lại, chủ sở hữu bản quyền không bị chính license do mình phát hành hạn chế theo cùng cách đó.

Một điểm khác cũng cần lưu ý: Rust Engine được nạp động ở runtime và có thể bị loại bỏ. Nếu một nhà phân phối muốn phát hành phiên bản chỉ gồm phần AGPL, họ có thể bỏ module này; ứng dụng vẫn hoạt động với engine C++ mã nguồn mở đi kèm.

Nếu trong tương lai cộng đồng thật sự cần phân phối kèm engine đóng nguồn, chủ sở hữu copyright vẫn có thể cấp thêm quyền theo GPLv3 §7, chẳng hạn additional permission hoặc linking exception.

---

# 4. Vậy khác gì các bộ gõ đóng nguồn hoàn toàn?

Nếu chỉ nhìn ở mức "có thành phần đóng nguồn", hai mô hình có vẻ giống nhau. Nhưng khác biệt nằm ở phạm vi của phần đóng nguồn.

Ở một bộ gõ đóng nguồn hoàn toàn, gần như toàn bộ phần xử lý cốt lõi là hộp đen. Người dùng không thể kiểm chứng cách phần mềm tiếp nhận phím, điều phối dữ liệu, lựa chọn engine hoặc xử lý kết quả.

VKey được thiết kế khác. Hạ tầng xử lý phím, engine loader và giao diện trao đổi dữ liệu đều nằm trong phần mã nguồn mở. Rust Engine chỉ là một implementation tùy chọn của engine xử lý tiếng Việt. Repository cũng có parity tests để so sánh kết quả giữa các engine trên cùng chuỗi phím, nhằm đảm bảo hành vi nhất quán.

Điểm này cũng được mô tả trong `extern/vkey_engine/README.md`: Rust Engine là build artifact đóng nguồn, được nạp động qua C ABI, trong khi VKey vẫn có engine C++ mã nguồn mở làm implementation mặc định.

Nói ngắn gọn: phần không công khai là implementation bên trong Rust Engine khi người dùng chủ động bật nó. Phần có thể kiểm chứng vẫn là toàn bộ đường đi của dữ liệu gõ phím trong ứng dụng.

---

# 5. Nếu không copy thì vì sao ban đầu fork OpenKey, rồi sau đó detach fork?

Ban đầu, VKey được tạo dưới dạng fork của OpenKey. Đây là cách phát triển rất phổ biến trong mã nguồn mở: bắt đầu từ một codebase sẵn có để sửa lỗi, thử nghiệm ý tưởng hoặc bổ sung tính năng.

Theo thời gian, kiến trúc của dự án thay đổi đáng kể: chuyển sang C++20, bổ sung mô hình Hybrid Hook + TSF, thay đổi pipeline xử lý và tổ chức lại nhiều thành phần. Đến lúc đó, trạng thái "Forked from..." trên GitHub không còn phản ánh đúng quan hệ thực tế giữa hai codebase.

Vì vậy mình mở ticket nhờ GitHub detach fork để repository được hiển thị như một dự án độc lập.

Điều quan trọng là detach fork không đồng nghĩa với phủ nhận nguồn gốc. Những thông tin này vẫn được giữ công khai:

- README ghi rõ nguồn gốc ban đầu từ OpenKey của Mai Vũ Tuyên.
- README giữ lời cảm ơn và liên kết tới repository gốc.
- Lịch sử của nhánh fork cũ, chẳng hạn `feat/UI-Next`, vẫn còn trên GitHub.
- Commit history vẫn được giữ nguyên.

Nếu mục tiêu là che giấu nguồn gốc, việc giữ lại credit, lời cảm ơn và lịch sử phát triển sẽ không có nhiều ý nghĩa. Cách đánh giá khách quan hơn vẫn là so sánh trực tiếp hai codebase, thay vì chỉ dựa vào trạng thái fork trên giao diện GitHub.

---

# 6. Đổi tên biến có phải để che nguồn gốc?

Mình có thấy ý kiến cho rằng việc đổi tên biến hoặc đổi tên một số thành phần trong code nhằm làm giảm khả năng nhận ra nguồn gốc.

Lịch sử commit công khai lại cho thấy các lần đổi tên này nằm trong quá trình refactor, với commit message giải thích lý do. Ví dụ:

- `Brain` đổi thành `Coordinator` để phản ánh đúng vai trò sau khi pipeline được thiết kế lại.
- `isCustomToneKey` đổi thành `isCustomModifier` vì phạm vi hàm được mở rộng.
- `HotkeyConfig.key wchar_t` đổi thành `vk uint32_t` để thống nhất với cách biểu diễn virtual key.

Đây là các thay đổi bình thường khi kiến trúc hoặc trách nhiệm của một thành phần thay đổi. Tên biến giống hay khác nhau tự nó không chứng minh được việc sao chép, cũng không chứng minh được việc che giấu.

Muốn đánh giá mức độ kế thừa hoặc viết lại, cần nhìn vào cấu trúc tổng thể, thuật toán, luồng xử lý, lịch sử commit và mức độ thay đổi của toàn bộ codebase.

---

# 7. Vừa thêm license thương mại, vừa nói học từ UniKey, có mâu thuẫn không?

Theo mình, cần phân biệt giữa ý tưởng, quy tắc ngôn ngữ và mã nguồn triển khai.

Trong commit có ghi:

> Unikey-inspired phonology rules, learned from Unikey 1.0.4 engine analysis.

Nội dung được học ở đây là các quy tắc chính tả tiếng Việt, chẳng hạn:

- `k` chỉ đứng trước `e`, `ê`, `i`, `y`;
- `c` không đứng trước `e`, `ê`, `i`, `y`;
- `gh`, `ngh` chỉ đi với một số nguyên âm nhất định;
- `q` thường đi cùng `u`;
- các quy tắc về phụ âm cuối và cấu trúc âm tiết.

Đây là quy tắc của tiếng Việt, không phải tài sản riêng của bất kỳ bộ gõ nào. Có thể học cách một phần mềm xử lý các quy tắc đó để hiểu bài toán, nhưng điều đó không đồng nghĩa với sao chép mã nguồn.

Trong cùng commit, phần hiện thực được viết mới và bổ sung hơn 120 test case. Chính mã nguồn triển khai mới là đối tượng được bảo hộ bởi copyright, còn bản thân quy tắc ngôn ngữ thì không.

Việc tham khảo UniKey cũng chưa bao giờ được giấu. Commit message ghi rõ nguồn cảm hứng, và README cũng ghi UniKey của anh Phạm Kim Long là một trong các nguồn tham khảo. Theo mình, ghi rõ nguồn tham khảo là cách giúp người đọc hiểu quá trình hình thành dự án và thể hiện sự tôn trọng với những người đi trước.

---

# 8. Nếu đã tham khảo thì sao không ghi thẳng ra?

Các nguồn tham khảo chính của VKey đã được ghi trong README:

- OpenKey của Mai Vũ Tuyên: nguồn gốc ban đầu của dự án.
- UniKey của anh Phạm Kim Long: tham khảo quy tắc âm vị học và xử lý tiếng Việt.
- VietType: tham khảo cấu hình và cách làm việc với TSF.

Một ví dụ thường được nhắc tới là so sánh `InputScopeChecker.h` của VKey với `EditBlocked.cpp` trong VietType.

Nếu chỉ nhìn vào một vài đoạn code, có thể thấy cả hai đều gọi các API như `GetAppProperty`, `ITfInputScope` hoặc `GetInputScopes`. Điều này là bình thường, vì đây là cách sử dụng API Microsoft TSF. Khi nhiều dự án cùng triển khai một API, phần khung lệnh thường sẽ giống nhau ở mức nhất định.

Điểm cần quan tâm là logic được xây dựng phía trên các lời gọi API đó.

Trong trường hợp này, điều VKey tham khảo từ VietType là danh sách Input Scope cần chặn, chẳng hạn Password, PIN, Email và Login. Phần này đã được ghi trong README từ tháng 4/2026, kèm liên kết tới repository VietType, chứ không phải chỉ được bổ sung sau khi có tranh luận.

Ở chiều ngược lại, VKey cũng có các phần xử lý riêng như kiểm tra thêm compartment `KEYBOARD_DISABLED`, tổ chức edit session theo kiến trúc khác, cơ chế xử lý lỗi khác và tích hợp vào pipeline riêng của VKey. Các điểm này đều có thể kiểm chứng từ mã nguồn.

---

# 9. Việc chuyển một số nội dung sang repository private là gì?

Phần được chuyển sang private gồm tài liệu nội bộ, planning, development tools và các tài nguyên phục vụ quá trình phát triển.

Phần source code của ứng dụng, test và hệ thống build vẫn tiếp tục được giữ công khai. Commit message cũng mô tả rõ thư mục nào được di chuyển và mục đích của từng thay đổi.

Cùng thời điểm đó, license của dự án được chuyển từ GPL sang AGPL, tức là chặt chẽ hơn đối với việc phân phối lại mã nguồn. Toàn bộ quá trình này có thể kiểm chứng từ lịch sử commit, thay vì phải dựa trên suy đoán.

---

# 10. Một số điểm có thể tự kiểm chứng

Thay vì chỉ dựa vào nhận định của mình, mọi người có thể tự kiểm tra một số điểm sau.

## Bảng mã TCVN3/VNI

Các mapping trong `CodeTableConverter.cpp` không phải dữ liệu do VKey tự đặt ra. Chúng xuất phát từ chuẩn quốc gia TCVN 5712:1993, nên các bộ gõ hỗ trợ TCVN3 sẽ có nhiều giá trị giống nhau.

Có thể kiểm chứng trên Linux:

```bash
zcat /usr/share/i18n/charmaps/TCVN5712-1.gz | grep -E "<U00C2>|<U0102>|<U0110>"
```

Kết quả sẽ cho thấy các giá trị như:

- Ă → `0xA1`
- Â → `0xA2`
- Đ → `0xA7`

Đây là dữ liệu thuộc một tiêu chuẩn công khai, không phải nội dung riêng của VKey hay của một bộ gõ cụ thể.

## Mức độ viết lại của codebase

Việc một dự án được viết lại đến mức nào không nên kết luận bằng cảm tính. Cách khách quan hơn là nhìn vào lịch sử Git và so sánh trực tiếp nội dung repository.

Ví dụ:

```bash
git merge-base Main origin/feat/UI-Next
```

Kết quả cho thấy branch `Main` hiện tại không được tạo trực tiếp từ branch fork cũ.

Khi so sánh toàn bộ hai cây source theo blob hash, chỉ có 39 file trùng hoàn toàn. Các file này đều là thư viện hoặc tài nguyên của bên thứ ba, chẳng hạn Sciter SDK headers, `toml++` hoặc ảnh screenshot. Không có file source first-party nào trùng tuyệt đối.

Nếu tiếp tục so sánh các file cùng tên giữa hai codebase, có khoảng 23 cặp. Phần lớn là các file do cùng một tác giả phát triển qua nhiều giai đoạn, nên việc có sự kế thừa là bình thường.

Với các file từng có đóng góp từ contributor trên nhánh fork cũ như `ConvertToolDialog`, `AboutDialog`, `main.cpp`, `resource.h` hoặc `stdafx.h`, tỷ lệ nội dung giống nhau chỉ khoảng 0-19%, và phần lớn là các dòng cú pháp C++ không thể hiện logic riêng của chương trình.

Ví dụ, `ConvertToolDialog.cpp` trên nhánh fork cũ có khoảng 247 dòng, còn phiên bản hiện tại có khoảng 529 dòng với cấu trúc và cách triển khai khác đáng kể.

Những số liệu này không nhằm nói rằng VKey "không liên quan" tới OpenKey. Ngược lại, nguồn gốc đó đã được ghi nhận công khai. Chúng chỉ cho thấy codebase hiện tại đã trải qua quá trình phát triển và tái cấu trúc ở quy mô lớn.

## Contributor có thể yêu cầu mở toàn bộ engine không?

VKey phát triển tốt hơn nhờ nhiều phản hồi từ người dùng: báo lỗi, đề xuất tính năng, góp ý trải nghiệm và thảo luận kỹ thuật. Mình luôn trân trọng những đóng góp đó.

Tuy nhiên, về mặt bản quyền phần mềm, cần phân biệt giữa đóng góp ý tưởng và quyền tác giả đối với mã nguồn được phát hành. Một ý tưởng, một phản hồi hoặc một bug report không mặc nhiên tạo ra quyền sở hữu đối với phần mã nguồn hiện thực hóa ý tưởng đó.

Quyền quyết định license của một phần mã nguồn phụ thuộc vào việc ai sở hữu copyright của chính phần mã đó. Có thể kiểm chứng bằng:

```bash
git log --author="maivutuyen" --oneline Main | wc -l
git shortlog -sne Main
```

Việc cộng đồng từng giúp dự án tốt hơn và việc ai có quyền quyết định license của phần mã nguồn hiện tại là hai vấn đề khác nhau. Cả hai đều quan trọng, nhưng không nên đánh đồng.

---

# Kết

Mình không kỳ vọng mọi người phải đồng ý với mọi quyết định của VKey. Mỗi người có quan điểm riêng về việc nên mở hay đóng nguồn một thành phần nào đó, và đó là chuyện bình thường.

Điều mình mong muốn là các trao đổi về license, copyright, nguồn gốc dự án hoặc phạm vi tham khảo nên dựa trên thông tin có thể kiểm chứng: repository, lịch sử commit, tài liệu công khai và các phép so sánh cụ thể.

VKey là dự án mình phát triển từ nhu cầu cá nhân trước khi chia sẻ cho cộng đồng. Mình không thu phí người dùng, không chạy quảng cáo, không kêu gọi tài trợ. Việc sử dụng VKey hoàn toàn do mỗi người tự quyết định. Các bản phát hành công khai của VKey hiện chỉ được phát hành theo giấy phép AGPL-3.0.

Nếu VKey phù hợp với nhu cầu của bạn, mình rất vui vì dự án có thể mang lại giá trị. Nếu không phù hợp, vẫn còn nhiều bộ gõ chất lượng khác để lựa chọn.

Với mình, điều quan trọng nhất là VKey có thể giữ được một bản sắc riêng, đồng thời vẫn cho phép người dùng kiểm chứng những phần liên quan đến dữ liệu gõ phím, quyền riêng tư và cách tích hợp engine. Đó là điểm cân bằng mình chọn cho dự án ở thời điểm hiện tại.
