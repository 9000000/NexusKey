// NexusKey - Localized String Dictionary Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "Strings.h"
#include <atomic>

namespace NextKey {

static std::atomic<Language> g_language{Language::Vietnamese};

// Vietnamese string table (readable UTF-8 source literals)
static const wchar_t* const kVietnamese[] = {
    L"Bật Tiếng Việt",                               // MENU_TOGGLE_VIET
    L"Bật kiểm tra chính tả",                        // MENU_SPELL_CHECK
    L"Lưu chế độ gõ theo app",                       // MENU_SMART_SWITCH
    L"Bật gõ tắt",                                   // MENU_MACRO_TOGGLE
    L"Cấu hình gõ tắt...",                           // MENU_MACRO_CONFIG
    L"Công cụ chuyển mã...",                          // MENU_CONVERT_TOOL
    L"Chuyển mã nhanh",                               // MENU_QUICK_CONVERT
    L"Kiểu gõ",                                      // MENU_INPUT_METHOD
    L"Bảng mã",                                      // MENU_CODE_TABLE
    L"Unicode tổ hợp",                               // MENU_UNICODE_COMPOUND
    L"Bảng điều khiển...",                            // MENU_SETTINGS
    L"Giới thiệu NexusKey",                          // MENU_ABOUT
    L"Thoát",                                        // MENU_EXIT
    L"NexusKey - Tiếng Việt",                        // TIP_VIETNAMESE
    L"NexusKey - English",                            // TIP_ENGLISH
    L"Giới thiệu NexusKey",                          // ABOUT_TITLE
    L"NexusKey - Bộ gõ Tiếng Việt\n"                 // ABOUT_BODY
    L"Giải pháp gõ Tiếng Việt hiện đại cho Windows.\n\n"
    L"SPDX-License-Identifier: GPL-3.0-only",
    L"Cập nhật",                                     // UPDATE_TITLE
    L"Có phiên bản mới!",                            // UPDATE_AVAILABLE_TITLE
    L"Phiên bản mới %s đã sẵn sàng.",               // UPDATE_AVAILABLE_BODY
    L"Cập nhật ngay",                                // UPDATE_NOW
    L"Bỏ qua",                                      // UPDATE_SKIP
    L"Bạn đang dùng phiên bản mới nhất!",           // UPDATE_LATEST
    L"Không thể kiểm tra cập nhật.",                 // UPDATE_FAILED
    L"Đang tải cập nhật...",                         // UPDATE_DOWNLOADING
    L"Xem thay đổi",                                // UPDATE_CHANGELOG
    L"Cập nhật thành công!",                         // UPDATE_SUCCESS
    L"Cập nhật thất bại.",                           // UPDATE_INSTALL_FAILED
    L"Kiểm tra ngay",                                // UPDATE_CHECK_NOW
    L"Đang kiểm tra...",                             // UPDATE_CHECKING
    L"Tải cập nhật thất bại.",                       // UPDATE_DOWNLOAD_FAILED
    L"Chưa chọn file nguồn.",                       // CONVERT_NO_SOURCE_FILE
    L"Không thể đọc file nguồn.",                    // CONVERT_READ_ERROR
    L"Clipboard trống.",                             // CONVERT_CLIPBOARD_EMPTY
    L"Chưa chọn file đích.",                         // CONVERT_NO_DEST_FILE
    L"Không thể ghi file đích.",                     // CONVERT_WRITE_ERROR
    L"Không thể ghi vào clipboard.",                 // CONVERT_CLIPBOARD_WRITE_ERROR
    L"Chuyển mã thành công!",                        // CONVERT_SUCCESS
};

// English string table
static const wchar_t* const kEnglish[] = {
    L"Enable Vietnamese",                             // MENU_TOGGLE_VIET
    L"Enable spell check",                            // MENU_SPELL_CHECK
    L"Smart app exclusion",                           // MENU_SMART_SWITCH
    L"Enable macro typing",                           // MENU_MACRO_TOGGLE
    L"Macro settings...",                             // MENU_MACRO_CONFIG
    L"Convert tool...",                               // MENU_CONVERT_TOOL
    L"Quick convert",                                 // MENU_QUICK_CONVERT
    L"Input method",                                  // MENU_INPUT_METHOD
    L"Code table",                                    // MENU_CODE_TABLE
    L"Unicode Compound",                              // MENU_UNICODE_COMPOUND
    L"Settings...",                                   // MENU_SETTINGS
    L"About NexusKey",                                // MENU_ABOUT
    L"Exit",                                          // MENU_EXIT
    L"NexusKey - Vietnamese",                         // TIP_VIETNAMESE
    L"NexusKey - English",                            // TIP_ENGLISH
    L"About NexusKey",                                // ABOUT_TITLE
    L"NexusKey Vietnamese Input\n"                    // ABOUT_BODY
    L"A modern Vietnamese typing solution for Windows.\n\n"
    L"SPDX-License-Identifier: GPL-3.0-only",
    L"Update",                                       // UPDATE_TITLE
    L"Update available!",                            // UPDATE_AVAILABLE_TITLE
    L"Version %s is available.",                     // UPDATE_AVAILABLE_BODY
    L"Update now",                                   // UPDATE_NOW
    L"Skip",                                         // UPDATE_SKIP
    L"You're on the latest version!",                // UPDATE_LATEST
    L"Unable to check for updates.",                 // UPDATE_FAILED
    L"Downloading update...",                        // UPDATE_DOWNLOADING
    L"View changelog",                               // UPDATE_CHANGELOG
    L"Update successful!",                           // UPDATE_SUCCESS
    L"Update failed.",                               // UPDATE_INSTALL_FAILED
    L"Check now",                                    // UPDATE_CHECK_NOW
    L"Checking...",                                  // UPDATE_CHECKING
    L"Download failed.",                             // UPDATE_DOWNLOAD_FAILED
    L"No source file selected.",                     // CONVERT_NO_SOURCE_FILE
    L"Cannot read source file.",                     // CONVERT_READ_ERROR
    L"Clipboard is empty.",                          // CONVERT_CLIPBOARD_EMPTY
    L"No destination file selected.",                // CONVERT_NO_DEST_FILE
    L"Cannot write destination file.",               // CONVERT_WRITE_ERROR
    L"Cannot write to clipboard.",                   // CONVERT_CLIPBOARD_WRITE_ERROR
    L"Conversion successful!",                       // CONVERT_SUCCESS
};

static_assert(sizeof(kVietnamese) / sizeof(kVietnamese[0]) == static_cast<size_t>(StringId::_COUNT),
              "Vietnamese string table size mismatch");
static_assert(sizeof(kEnglish) / sizeof(kEnglish[0]) == static_cast<size_t>(StringId::_COUNT),
              "English string table size mismatch");

void SetLanguage(Language lang) noexcept {
    g_language.store(lang, std::memory_order_relaxed);
}

Language GetLanguage() noexcept {
    return g_language.load(std::memory_order_relaxed);
}

const wchar_t* S(StringId id) noexcept {
    auto index = static_cast<uint16_t>(id);
    if (index >= static_cast<uint16_t>(StringId::_COUNT)) return L"";

    return g_language.load(std::memory_order_relaxed) == Language::English
        ? kEnglish[index]
        : kVietnamese[index];
}

}  // namespace NextKey
