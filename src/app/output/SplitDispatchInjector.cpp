// src/app/output/SplitDispatchInjector.cpp
//
// D3 implementation. Split SendInput with Sleep between BS batch and
// char batch. Covers Electron (Discord/Slack/VSCode, sleepMs=6) and
// Console (CMD/PowerShell, sleepMs=5). Distinct from Win32 batched
// path because these renderers drop the second half of a single big
// batch under load — splitting + brief Sleep gives the message loop a
// chance to drain before the second wave.
//
// Spec: docs/plans/sprint-2-output-injector.md §2.4
#include "SplitDispatchInjector.h"

#include <array>

#include "Internal.h"

namespace NextKey::Output {

namespace {

constexpr std::size_t kMaxBatch = 256;

// Helpers are intentionally local so each injector owns its event construction.
INPUT MakeKey(WORD vk, bool keyup) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = keyup ? KEYEVENTF_KEYUP : 0u;
    in.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
    return in;
}

INPUT MakeUnicodeChar(WCHAR ch, bool keyup) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = static_cast<WORD>(ch);
    in.ki.dwFlags = KEYEVENTF_UNICODE | (keyup ? KEYEVENTF_KEYUP : 0u);
    in.ki.dwExtraInfo = Internal::kVKeyExtraInfo;
    return in;
}

}  // namespace

bool SplitDispatchInjector::Replace(std::size_t bsCount,
                                    std::wstring_view text,
                                    unsigned short reinjectVk) noexcept {
    // Batch 1: bait char (Chromium suggest-dismiss, when applicable) +
    // backspaces. Predicate shared with Win32SendInputInjector via
    // Internal::ShouldEmitBait — WebView2 / Electron-on-Chromium hosts
    // inherit the same selection-eat quirk as Edge's omnibox.
    std::array<INPUT, kMaxBatch> bsBuf{};
    std::size_t bi = 0;
    // Game-compat re-inject leads batch 1 so it can never be separated from
    // the backspaces that delete it. Reached only via the per-app
    // "compatibility split" override (sendMethod 2/3) on a non-Electron,
    // non-console host — auto-detected Electron/console set localSkipEmpty,
    // which zeroes reinjectVk upstream in HandleAlphaKey.
    const std::size_t reinjectCount = (reinjectVk != 0) ? 1u : 0u;
    if (reinjectCount != 0) {
        bsBuf[bi++] = MakeKey(static_cast<WORD>(reinjectVk), /*keyup=*/false);
    }
    // See Win32SendInputInjector::Replace: synthetic Backspace must not inherit
    // a physically held Shift (notably Shift+dd -> Đ in Excel Web). Restore
    // Shift in this first batch before the optional inter-batch sleep.
    std::array<WORD, 2> heldShifts{};
    std::size_t heldShiftCount = 0;
    if (bsCount > 0
        && (Internal::g_getAsyncKeyState(VK_LSHIFT) & 0x8000) != 0) {
        heldShifts[heldShiftCount++] = VK_LSHIFT;
    }
    if (bsCount > 0
        && (Internal::g_getAsyncKeyState(VK_RSHIFT) & 0x8000) != 0) {
        heldShifts[heldShiftCount++] = VK_RSHIFT;
    }
    for (std::size_t k = 0; k < heldShiftCount; ++k) {
        if (bi + 1 > kMaxBatch) return false;
        bsBuf[bi++] = MakeKey(heldShifts[k], /*keyup=*/true);
    }
    const bool emitBait = Internal::ShouldEmitBait(
        needsBaitCharPrefix_, bsCount, text,
        suggestKeepChars_.load(std::memory_order_acquire));
    if (emitBait) {
        if (bi + 2 > kMaxBatch) return false;
        bsBuf[bi++] = MakeUnicodeChar(0x202F, /*keyup=*/false);
        bsBuf[bi++] = MakeUnicodeChar(0x202F, /*keyup=*/true);
        ++bsCount;  // extra BS to delete the bait char
    }
    for (std::size_t k = 0; k < bsCount; ++k) {
        if (bi + 2 > kMaxBatch) return false;
        bsBuf[bi++] = MakeKey(VK_BACK, /*keyup=*/false);
        bsBuf[bi++] = MakeKey(VK_BACK, /*keyup=*/true);
    }
    for (std::size_t k = 0; k < heldShiftCount; ++k) {
        if (bi + 1 > kMaxBatch) return false;
        bsBuf[bi++] = MakeKey(heldShifts[k], /*keyup=*/false);
    }
    if (bi > 0) {
        UINT sent = 0;
        if (!Internal::TrackedSendInput(
                bsBuf.data(), static_cast<UINT>(bi), &sent)) {
            if (heldShiftCount > 0 && sent > 0) {
                std::array<INPUT, 2> restoreShifts{};
                std::size_t restoreCount = 0;
                const std::size_t restoreStart = bi - heldShiftCount;
                const std::size_t sentCount = static_cast<std::size_t>(sent);
                for (std::size_t k = 0; k < heldShiftCount; ++k) {
                    // Offset by the re-inject key-down at index 0 — see the
                    // matching comment in Win32SendInputInjector::Replace.
                    const bool released = sentCount > reinjectCount + k;
                    const bool restoredInBatch = sentCount > restoreStart + k;
                    if (released && !restoredInBatch) {
                        restoreShifts[restoreCount++] =
                            MakeKey(heldShifts[k], /*keyup=*/false);
                    }
                }
                if (restoreCount > 0) {
                    (void)Internal::TrackedSendInput(
                        restoreShifts.data(), static_cast<UINT>(restoreCount));
                }
            }
            return false;
        }
    }

    // Batch 2: chars (only if text non-empty). Sleep only when both
    // batches present — pure-BS or pure-text needs no inter-batch gap.
    // A lone re-inject key-down is not a deletion, so it must not buy the
    // gap on its own: that would add sleepMs_ to a path that never had it.
    if (!text.empty()) {
        if (bi > reinjectCount) {
            Internal::g_sleep(static_cast<DWORD>(sleepMs_));
        }
        std::array<INPUT, kMaxBatch> charBuf{};
        std::size_t ci = 0;
        for (WCHAR ch : text) {
            if (ci + 2 > kMaxBatch) return false;
            charBuf[ci++] = MakeUnicodeChar(ch, /*keyup=*/false);
            charBuf[ci++] = MakeUnicodeChar(ch, /*keyup=*/true);
        }
        if (ci == 0) return true;
        if (!Internal::TrackedSendInput(charBuf.data(), static_cast<UINT>(ci))) {
            return false;
        }
    }
    return true;
}

void SplitDispatchInjector::SendKey(unsigned short vkCode) noexcept {
    INPUT events[2] = {
        MakeKey(static_cast<WORD>(vkCode), /*keyup=*/false),
        MakeKey(static_cast<WORD>(vkCode), /*keyup=*/true),
    };
    (void)Internal::TrackedSendInput(events, 2);
}

}  // namespace NextKey::Output
