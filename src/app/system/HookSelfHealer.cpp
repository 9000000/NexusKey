// NexusKey - Hook Self-Healer Implementation (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only

#include "HookSelfHealer.h"

#ifdef _WIN32

#include "core/Debug.h"

namespace NextKey {

thread_local RawInputSelfHealer* RawInputSelfHealer::tlsActive_ = nullptr;

RawInputSelfHealer::RawInputSelfHealer(HINSTANCE hInst, ReinstallHookFn reinstaller)
    : hInst_(hInst), reinstaller_(std::move(reinstaller)) {}

RawInputSelfHealer::~RawInputSelfHealer() {
    Stop();
}

bool RawInputSelfHealer::Start() {
    if (hwnd_) return true;  // Idempotent

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst_;
    wc.lpszClassName = L"NexusKey_HookSelfHealer";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    // Hidden desktop-hierarchy window (NOT HWND_MESSAGE — RIDEV_INPUTSINK
    // requires desktop hierarchy to receive WM_INPUT in background).
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, nullptr,
                             0, 0, 0, 0, 0,
                             nullptr, nullptr, hInst_, nullptr);
    if (!hwnd_) return false;

    // Bind TLS so WndProc can find this instance — matches the previous
    // s_instance pattern but per-thread (avoids cross-instance interference
    // if HookEngine ever multi-instantiates).
    tlsActive_ = this;

    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;  // Generic Desktop
    rid.usUsage     = 0x06;  // Keyboard
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd_;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        tlsActive_ = nullptr;
        return false;
    }
    return true;
}

void RawInputSelfHealer::Stop() {
    if (!hwnd_) return;

    KillTimer(hwnd_, SELF_HEAL_TIMER_ID);

    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage     = 0x06;
    rid.dwFlags     = RIDEV_REMOVE;
    rid.hwndTarget  = nullptr;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    tlsActive_ = nullptr;
    UnregisterClassW(L"NexusKey_HookSelfHealer", hInst_);
}

void RawInputSelfHealer::RecordHookFire() {
    // Same thread as WndProc — no lock, no atomic.
    lastLlHookTime_ = GetTickCount();
}

LRESULT CALLBACK RawInputSelfHealer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Rule 11.5 fix: top-level catch — mirrors LowLevelKeyboardProc pattern.
    try {
        RawInputSelfHealer* self = tlsActive_;

        if (msg == WM_TIMER && wParam == SELF_HEAL_TIMER_ID) {
            KillTimer(hwnd, SELF_HEAL_TIMER_ID);
            if (!self) return 0;

            NEXTKEY_LOG(L"  SelfHeal: timer fired — invoking reinstaller");

            const bool ok = self->reinstaller_ ? self->reinstaller_() : false;

            if (ok) {
                self->consecutiveRawMisses_ = 0;
                self->lastSelfHealTime_ = GetTickCount();
                NEXTKEY_LOG(L"  SelfHeal: reinstaller OK");
            } else {
                // Rule 3 fix: do NOT reset cooldown timestamp on failure.
                // This allows future retries instead of locking the
                // healer in an infinite-cooldown state.
                NEXTKEY_LOG(L"SelfHeal: reinstaller FAILED (err=%lu) — retry on next miss streak",
                            GetLastError());
            }
            return 0;
        }

        if (msg != WM_INPUT) return DefWindowProcW(hwnd, msg, wParam, lParam);
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

        // Extract Raw Input (stack-only, no allocation).
        UINT size = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                        RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size == 0 || size > sizeof(RAWINPUT) + 32) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        alignas(RAWINPUT) BYTE buf[sizeof(RAWINPUT) + 32];
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                            RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        auto* raw = reinterpret_cast<RAWINPUT*>(buf);

        // Filter: physical keyboard key-down only.
        if (raw->header.dwType != RIM_TYPEKEYBOARD) return DefWindowProcW(hwnd, msg, wParam, lParam);
        if (raw->header.hDevice == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);
        if (raw->data.keyboard.Flags & RI_KEY_BREAK) return DefWindowProcW(hwnd, msg, wParam, lParam);

        // Comment fix (was "< 0.5μs total" — incorrect; GetRawInputData ×2
        // alone is ~5-10μs). Hot path realistically ~5-20μs, well within
        // the 1ms hook-callback budget (Rule 11.1).
        DWORD now = GetTickCount();
        DWORD elapsed = now - self->lastLlHookTime_;

        if (elapsed < SELF_HEAL_HOOK_FRESHNESS_MS) {
            self->consecutiveRawMisses_ = 0;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        self->consecutiveRawMisses_++;
        NEXTKEY_LOG(L"  SelfHeal: LL hook miss #%u (elapsed=%ums, vk=0x%02X)",
                    self->consecutiveRawMisses_, elapsed, raw->data.keyboard.VKey);

        if (self->consecutiveRawMisses_ >= SELF_HEAL_MISS_THRESHOLD) {
            DWORD sinceLast = now - self->lastSelfHealTime_;
            if (sinceLast < SELF_HEAL_COOLDOWN_MS) {
                NEXTKEY_LOG(L"  SelfHeal: cooldown (%ums left)",
                            SELF_HEAL_COOLDOWN_MS - sinceLast);
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }

            // Random delay 10-50ms — avoid lockstep with hijacker reinstall.
            UINT delay = 10 + (GetTickCount() % 41);
            SetTimer(hwnd, SELF_HEAL_TIMER_ID, delay, nullptr);
            NEXTKEY_LOG(L"  SelfHeal: HOOK DEAD — scheduled reinstaller in %ums", delay);
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } catch (...) {
        // Rule 11.5: swallow + no rethrow. Hook-thread message pump must
        // not abort the process. State stays as-is until next message.
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace NextKey

#endif  // _WIN32
