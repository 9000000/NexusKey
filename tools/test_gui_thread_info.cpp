// Diagnostic tool: monitors GetGUIThreadInfo() + tracks Enter/Esc for chat-toggle detection.
// Tests whether a game (e.g. LoL) exposes caret/focus info, and simulates Enter-based
// chat state tracking as a fallback for games that don't.
//
// Build (MSVC): cl /EHsc /std:c++20 test_gui_thread_info.cpp /link user32.lib
// Usage: Run this, switch to the target app/game. Press Enter to open/close chat.
//        Press Ctrl+C to stop.
//
// Results from LoL testing (2026-04-05):
//   - hwndCaret always NULL (DirectX overlay, no Win32 caret)
//   - hwndFocus = main game window (RiotWindowClass), doesn't change on chat open
//   - Conclusion: GetGUIThreadInfo() cannot detect LoL chat → need Enter-tracking fallback

#include <windows.h>
#include <cstdio>
#include <string>

static std::string GetWindowTitle(HWND hwnd) {
    if (!hwnd) return "(null)";
    char buf[256]{};
    GetWindowTextA(hwnd, buf, sizeof(buf));
    return buf[0] ? buf : "(no title)";
}

static std::string GetWindowClass(HWND hwnd) {
    if (!hwnd) return "(null)";
    char buf[256]{};
    GetClassNameA(hwnd, buf, sizeof(buf));
    return buf[0] ? buf : "(no class)";
}

// --- Enter-tracking simulation ---
// Simulates the proposed fallback: track Enter/Esc to detect chat open/close.
// In a real IME, this would live in the keyboard hook and toggle Vietnamese processing.
static bool g_chatOpen = false;

static HHOOK g_hook = nullptr;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (kb->vkCode == VK_RETURN) {
            g_chatOpen = !g_chatOpen;
            printf("[ENTER-TRACK] chatOpen = %s\n", g_chatOpen ? "TRUE (Vietnamese ON)" : "FALSE (pass-through)");
        } else if (kb->vkCode == VK_ESCAPE && g_chatOpen) {
            g_chatOpen = false;
            printf("[ENTER-TRACK] chatOpen = FALSE (Esc close)\n");
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

int main() {
    printf("=== GetGUIThreadInfo + Enter-Tracking Diagnostic ===\n");
    printf("Switch to the target app/game.\n");
    printf("  - Monitors hwndCaret/hwndFocus (polls every 500ms)\n");
    printf("  - Tracks Enter/Esc for chat-toggle simulation (real-time hook)\n");
    printf("Press Ctrl+C to stop.\n\n");

    // Install low-level keyboard hook for Enter-tracking
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (!g_hook) {
        printf("WARNING: Failed to install keyboard hook (err=%lu). Enter-tracking disabled.\n\n", GetLastError());
    }

    HWND lastFg = nullptr;
    HWND lastCaret = nullptr;
    HWND lastFocus = nullptr;

    while (true) {
        // Pump messages so the hook callback fires
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        HWND fg = GetForegroundWindow();
        DWORD tid = GetWindowThreadProcessId(fg, nullptr);

        GUITHREADINFO gti = { sizeof(gti) };
        BOOL ok = GetGUIThreadInfo(tid, &gti);

        // Only print when state changes
        if (fg != lastFg || gti.hwndCaret != lastCaret || gti.hwndFocus != lastFocus) {
            lastFg = fg;
            lastCaret = gti.hwndCaret;
            lastFocus = gti.hwndFocus;

            printf("---\n");
            printf("Foreground : 0x%p  [%s] class=%s\n",
                   (void*)fg, GetWindowTitle(fg).c_str(), GetWindowClass(fg).c_str());

            if (ok) {
                printf("  hwndFocus : 0x%p  [%s] class=%s\n",
                       (void*)gti.hwndFocus,
                       GetWindowTitle(gti.hwndFocus).c_str(),
                       GetWindowClass(gti.hwndFocus).c_str());
                printf("  hwndCaret : 0x%p  [%s] class=%s\n",
                       (void*)gti.hwndCaret,
                       GetWindowTitle(gti.hwndCaret).c_str(),
                       GetWindowClass(gti.hwndCaret).c_str());
                printf("  flags     : 0x%08lX", gti.flags);
                if (gti.flags & GUI_CARETBLINKING) printf(" CARET_BLINKING");
                if (gti.flags & GUI_INMOVESIZE)    printf(" IN_MOVESIZE");
                if (gti.flags & GUI_INMENUMODE)    printf(" IN_MENUMODE");
                if (gti.flags & GUI_SYSTEMMENUMODE) printf(" SYS_MENU");
                printf("\n");
            } else {
                printf("  GetGUIThreadInfo FAILED (err=%lu)\n", GetLastError());
            }
        }

        Sleep(100);  // Faster poll for smoother Enter-tracking display
    }

    if (g_hook) UnhookWindowsHookEx(g_hook);
    return 0;
}
