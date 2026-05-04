// generate_golden.js — emit tests/TelexGolden.h by running vn-str on a fixed corpus.
//
// PURPOSE: Lock our C++ Telex port (src/Telex.h) against vn-str's actual output.
// The committed TelexGolden.h is the source of truth for tests; this script is
// reference-only and rerun by hand if the corpus changes.
//
// SETUP (one-time):
//   cd tools/NextKeyTestRunner/test_data
//   npm install
//
// RUN:
//   node generate_golden.js > ../tests/TelexGolden.h
//
// Source library: vn-str (https://github.com/tronghieu60s/vn-str, MIT License)

const { strToTelex } = require('vn-str');

const PARAGRAPH = "Hôm nay mình mở máy sớm hơn thường lệ, pha một ly cà phê và ngồi trước màn hình với quyết tâm sửa dứt điểm một lỗi nhỏ nhưng cực kỳ khó chịu. Khi gõ văn bản bình thường, thỉnh thoảng sau khi nhấn phím cách, chữ cái đầu tiên của từ tiếp theo lại tự động viết hoa, dù trước đó không hề có dấu chấm câu. Điều này làm mình mất nhịp suy nghĩ, giống như đang đi đều bỗng bị vấp một viên sỏi vô hình. Mình bắt đầu ghi lại từng bước thao tác, từ việc nhập từng ký tự cho đến thời điểm nhấn phím cách. Có lúc mọi thứ hoạt động trơn tru, nhưng đôi khi chỉ cần thay đổi nhịp gõ một chút là lỗi xuất hiện. Cảm giác như hệ thống đang phản ứng chậm hơn một nhịp, và quyết định viết hoa được đưa ra muộn hơn so với thời điểm thực tế của phím bấm. Sau khi xem lại log, mình nhận ra rằng trạng thái nội bộ có thể đang bị giữ lại lâu hơn dự kiến. Một cờ đánh dấu kết thúc câu có lẽ chưa được reset đúng lúc, hoặc bị kích hoạt sai trong một tình huống hiếm gặp. Điều này khiến hệ thống hiểu nhầm rằng mình đang bắt đầu một câu mới, dù thực tế chỉ là một từ bình thường sau dấu cách. Giải pháp trước mắt là viết thêm test để tái hiện lại tình huống này một cách ổn định. Khi đã có thể tái hiện, việc sửa lỗi sẽ trở nên rõ ràng hơn rất nhiều.";

const cases = [];

// Group A: edge cases (manual)
const edge = [
  "",
  "abc",
  "Hello World",
  "123 456",
  ".,!?;:",
  "ABC XYZ",
  "Việt",
  "VIỆT",
  "việt nam",
  "Café"
];
edge.forEach(s => cases.push({ name: 'edge', input: s }));

// Group B: unique words from paragraph
const wordSet = new Set();
PARAGRAPH.split(/[\s,.!?;:]+/).forEach(w => {
  if (w.length > 0) wordSet.add(w);
});
[...wordSet].forEach(w => cases.push({ name: 'word', input: w }));

// Group C: sentences (split by . ! ?)
PARAGRAPH.split(/[.!?]+/).map(s => s.trim()).filter(s => s.length > 0).forEach(s => {
  cases.push({ name: 'sentence', input: s });
});

// Group D: full paragraph as single stress case
cases.push({ name: 'paragraph', input: PARAGRAPH });

// Apply vn-str strToTelex on every case
cases.forEach(c => {
  c.expected = strToTelex(c.input);
});

console.error(`Total cases: ${cases.length}`);
console.error(`  edge: ${cases.filter(c=>c.name==='edge').length}`);
console.error(`  word: ${cases.filter(c=>c.name==='word').length}`);
console.error(`  sentence: ${cases.filter(c=>c.name==='sentence').length}`);
console.error(`  paragraph: ${cases.filter(c=>c.name==='paragraph').length}`);

// Emit C++ header. We use char16_t (u"...") for portability — char16_t is 16-bit
// on both Windows + Linux, while wchar_t differs. All Vietnamese chars are BMP
// (U+0000..U+FFFF), so they fit char16_t with no surrogate handling needed.
const escape = (s) => s
  .replace(/\\/g, '\\\\')
  .replace(/"/g, '\\"')
  .replace(/\n/g, '\\n');

let header = `// Auto-generated from tools/NextKeyTestRunner/test_data/generate_golden.js
// Source library: vn-str (https://github.com/tronghieu60s/vn-str, MIT License)
// DO NOT EDIT MANUALLY — regenerate with:
//   node tools/NextKeyTestRunner/test_data/generate_golden.js > tools/NextKeyTestRunner/tests/TelexGolden.h
#pragma once

#include <cstddef>

namespace NextKey::TestRunner::Test {

struct TelexGoldenCase {
    const char* group;
    const char16_t* input;
    const char16_t* expected;
};

inline constexpr TelexGoldenCase kTelexGolden[] = {
`;

cases.forEach(c => {
  header += `    { "${c.name}", u"${escape(c.input)}", u"${escape(c.expected)}" },\n`;
});

header += `};

inline constexpr std::size_t kTelexGoldenCount = sizeof(kTelexGolden) / sizeof(kTelexGolden[0]);

}  // namespace NextKey::TestRunner::Test
`;

process.stdout.write(header);
