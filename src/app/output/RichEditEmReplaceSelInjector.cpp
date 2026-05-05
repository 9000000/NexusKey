// src/app/output/RichEditEmReplaceSelInjector.cpp
//
// D2 implementation. Port of HookEngine::TryEditMessagePaste — the
// sent-message channel that Sprint 1 D12 proved is required for Win11
// New Notepad RichEditD2DPT under chaos pressure. Posted-message
// channel (raw SendInput VK_BACK + Unicode chars) interleaves with
// already-queued messages and gets pre-empted by the next sent
// EM_REPLACESEL — chaos 5.3 verdict.
//
// Hang protection: SendMessageTimeoutW with SMTO_ABORTIFHUNG and a
// 50 ms timeout — if the target window is hung, we bail rather than
// block the hook callback indefinitely (LowLevelHooksTimeout default
// 300 ms; we stay well under).
//
// SendKey delegates to the Win32 SendInput physical channel because
// re-inject semantics (HookEngine::InjectKey use case) require the
// event to land AFTER any pending posted synth — sent messages would
// land BEFORE.
//
// Spec: docs/plans/sprint-2-output-injector.md §2.3
#include "RichEditEmReplaceSelInjector.h"

#include <richedit.h>

#include <string>

#include "Internal.h"
#include "Win32SendInputInjector.h"

namespace NextKey::Output {

namespace {

constexpr UINT kEditMsgFlags     = SMTO_ABORTIFHUNG | SMTO_NORMAL;
constexpr UINT kEditMsgTimeoutMs = 50;

// Class-compat check ported from HookEngine.cpp:1901. Accepts plain
// Edit, RichEdit*, and ThunderRT6 (VB6) variants. Sprint 1 D12 fix
// targets RichEditD2DPT specifically but the broader compat list is
// preserved so EM_REPLACESEL works on any Edit-compatible host.
bool IsEditCompatibleClass(const wchar_t* cls) noexcept {
    if (!cls || !*cls) return false;
    if (_wcsnicmp(cls, L"ThunderRT6TextBox", 17) == 0) return true;
    if (_wcsnicmp(cls, L"ThunderRT6RichText", 18) == 0) return true;
    if (_wcsicmp(cls, L"Edit") == 0) return true;
    if (_wcsnicmp(cls, L"RichEdit", 8) == 0) return true;
    return false;
}

// Resolve focused HWND inside the foreground process. Mirrors
// HookEngine::RefreshFocusCache logic but self-contained — no HookEngine
// member access. Slightly heavier per Replace call than the cached
// version (one AttachThreadInput cycle), but RichEditD2DPT is ~5% of
// host classes, so the cost is amortized acceptably.
HWND ResolveFocusedHwnd() noexcept {
    HWND fg = ::GetForegroundWindow();
    if (!fg) return nullptr;
    DWORD tid = ::GetWindowThreadProcessId(fg, nullptr);
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (::GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) return gti.hwndFocus;
    return fg;
}

}  // namespace

bool RichEditEmReplaceSelInjector::Replace(std::size_t bsCount,
                                           std::wstring_view text) noexcept {
    if (bsCount == 0 && text.empty()) return true;  // nothing to do

    HWND hwnd = ResolveFocusedHwnd();
    if (!hwnd) return false;

    wchar_t cls[64] = {};
    if (::GetClassNameW(hwnd, cls, _countof(cls)) == 0) return false;
    if (!IsEditCompatibleClass(cls)) return false;

    DWORD_PTR dummy = 0;
    DWORD     newStart = 0, selEnd = 0;

    // EM_GETSEL — query caret. Safe to call before redraw suppression.
    if (bsCount > 0) {
        DWORD selStart = 0;
        if (!Internal::g_sendMessageTimeoutW(hwnd, EM_GETSEL,
                reinterpret_cast<WPARAM>(&selStart),
                reinterpret_cast<LPARAM>(&selEnd),
                kEditMsgFlags, kEditMsgTimeoutMs, &dummy)) {
            return false;  // timed out / no result
        }
        if (static_cast<DWORD>(bsCount) > selEnd) {
            // BS would cross before the start of buffer — bail.
            return false;
        }
        newStart = selEnd - static_cast<DWORD>(bsCount);
    }

    // Suppress redraw between EM_SETSEL (highlights selection) and
    // EM_REPLACESEL — otherwise the user sees a brief blue selection flash.
    // erase=FALSE because text controls paint their own background; TRUE
    // would cause a background-color flash before text redraws.
    bool redrawSuppressed = false;
    if (bsCount > 0) {
        redrawSuppressed = Internal::g_sendMessageTimeoutW(hwnd, WM_SETREDRAW,
            FALSE, 0, kEditMsgFlags, kEditMsgTimeoutMs, &dummy) != 0;

        if (!Internal::g_sendMessageTimeoutW(hwnd, EM_SETSEL,
                static_cast<WPARAM>(newStart), static_cast<LPARAM>(selEnd),
                kEditMsgFlags, kEditMsgTimeoutMs, &dummy)) {
            if (redrawSuppressed) {
                Internal::g_sendMessageTimeoutW(hwnd, WM_SETREDRAW, TRUE, 0,
                    kEditMsgFlags, kEditMsgTimeoutMs, &dummy);
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            return false;
        }
    }

    // wParam=TRUE → goes on the undo stack so Ctrl+Z still works.
    // text needs to be null-terminated for EM_REPLACESEL — the std::wstring
    // copy is on the cold-ish RichEdit path (~5% of cases) so the alloc is
    // acceptable. Stack-array would need a max-size cap; std::wstring is
    // simpler and small-string-optimized for typical Vietnamese words.
    std::wstring zterm(text);
    BOOL replaceOk = Internal::g_sendMessageTimeoutW(hwnd, EM_REPLACESEL,
        static_cast<WPARAM>(TRUE), reinterpret_cast<LPARAM>(zterm.c_str()),
        kEditMsgFlags, kEditMsgTimeoutMs, &dummy) != 0;

    if (redrawSuppressed) {
        Internal::g_sendMessageTimeoutW(hwnd, WM_SETREDRAW, TRUE, 0,
            kEditMsgFlags, kEditMsgTimeoutMs, &dummy);
        ::InvalidateRect(hwnd, nullptr, FALSE);
    }

    return replaceOk != FALSE;
}

void RichEditEmReplaceSelInjector::SendKey(unsigned short vkCode) noexcept {
    // Re-inject semantics: physical SendInput so the event lands AFTER
    // any pending posted synth in the OS input queue. Sent-message
    // delivery would land BEFORE (different queue), breaking InjectKey's
    // contract at HookEngine line ~905.
    Win32SendInputInjector(/*needsBaitCharPrefix=*/false).SendKey(vkCode);
}

}  // namespace NextKey::Output
