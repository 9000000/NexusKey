// src/app/output/Win32SendInputInjector.cpp
//
// D1 implementation. Batch SendInput. Stack-buffer std::array (no heap
// alloc on hot path). Bait-char prefix for Chromium autocomplete-dismiss
// quirk (replicates HookEngine::SendBackspaces line ~3121).
//
// Spec: docs/plans/sprint-2-output-injector.md §2.2
#include "Win32SendInputInjector.h"

#include <array>

#include "Internal.h"

namespace NextKey::Output {

namespace {

// Stack-buffer upper bound. Worst-case Vietnamese composition replacement
// needs ~8 chars + 8 BS; 256 events (= 128 keystrokes × down/up) is 16×
// safety margin. Sized to fit a single SendInput batch comfortably.
constexpr std::size_t kMaxBatch = 256;

INPUT MakeKeyEvent(WORD vk, bool keyup,
                   ULONG_PTR extraInfo = Internal::kVKeyExtraInfo) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = keyup ? KEYEVENTF_KEYUP : 0u;
    in.ki.dwExtraInfo = extraInfo;
    return in;
}

INPUT MakeUnicodeChar(WCHAR ch, bool keyup,
                      ULONG_PTR extraInfo = Internal::kVKeyExtraInfo) noexcept {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = static_cast<WORD>(ch);
    in.ki.dwFlags = KEYEVENTF_UNICODE | (keyup ? KEYEVENTF_KEYUP : 0u);
    in.ki.dwExtraInfo = extraInfo;
    return in;
}

}  // namespace

bool Win32SendInputInjector::Replace(std::size_t bsCount,
                                     std::wstring_view text,
                                     unsigned short reinjectVk) noexcept {
    std::array<INPUT, kMaxBatch> buf{};
    std::size_t i = 0;

    // Game-compat re-inject leads the batch (see IOutputInjector::Replace).
    // Key-down only: a sustained hold keeps sending downs to the game, and
    // the physical key-up passes through the hook on release.
    const std::size_t reinjectCount = (reinjectVk != 0) ? 1u : 0u;
    if (reinjectCount != 0) {
        buf[i++] = MakeKeyEvent(static_cast<WORD>(reinjectVk), /*keyup=*/false);
    }

    // A Telex transform can run while the user is holding Shift (for example,
    // Shift+dd -> Đ). The physical Shift state otherwise turns our synthetic
    // VK_BACK events into Shift+Backspace in browser editors; Excel Web treats
    // that as a destructive selection/edit command. Keep the entire synthetic
    // replacement modifier-neutral, then restore the user's held Shift before
    // the hook resumes physical input. No backspaces means no such command and
    // no need to perturb modifier state.
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
        if (i + 1 > kMaxBatch) return false;
        buf[i++] = MakeKeyEvent(heldShifts[k], /*keyup=*/true);
    }
    // Bait-char prefix (Chromium suggest-dismiss): inserts U+202F + an
    // extra BS to delete it before the rest of the deletes/chars run.
    // Predicate in Internal::ShouldEmitBait — pure-BS only skips bait
    // when the user opts into the "BS giữ chữ khi có gợi ý" setting.
    const bool emitBait = Internal::ShouldEmitBait(
        needsBaitCharPrefix_, bsCount, text,
        suggestKeepChars_.load(std::memory_order_acquire));
    if (emitBait) {
        if (i + 2 > kMaxBatch) return false;
        buf[i++] = MakeUnicodeChar(0x202F, /*keyup=*/false);
        buf[i++] = MakeUnicodeChar(0x202F, /*keyup=*/true);
        ++bsCount;  // extra BS to delete the bait char
    }

    // Backspace events.
    for (std::size_t k = 0; k < bsCount; ++k) {
        if (i + 2 > kMaxBatch) return false;
        buf[i++] = MakeKeyEvent(VK_BACK, /*keyup=*/false);
        buf[i++] = MakeKeyEvent(VK_BACK, /*keyup=*/true);
    }

    // Char events.
    for (WCHAR ch : text) {
        if (i + 2 > kMaxBatch) return false;
        buf[i++] = MakeUnicodeChar(ch, /*keyup=*/false);
        buf[i++] = MakeUnicodeChar(ch, /*keyup=*/true);
    }
    for (std::size_t k = 0; k < heldShiftCount; ++k) {
        if (i + 1 > kMaxBatch) return false;
        buf[i++] = MakeKeyEvent(heldShifts[k], /*keyup=*/false);
    }

    if (i == 0) return true;  // nothing to do (bsCount=0, text empty)

    UINT sent = 0;
    const bool delivered = Internal::TrackedSendInput(
        buf.data(), static_cast<UINT>(i), &sent);
    if (!delivered && heldShiftCount > 0 && sent > 0) {
        std::array<INPUT, 2> restoreShifts{};
        std::size_t restoreCount = 0;
        const std::size_t restoreStart = i - heldShiftCount;
        const std::size_t sentCount = static_cast<std::size_t>(sent);
        for (std::size_t k = 0; k < heldShiftCount; ++k) {
            // Shift-releases sit at [reinjectCount, reinjectCount+heldShiftCount);
            // the re-inject key-down occupies index 0 when present. Dropping
            // the offset here would mis-read a partial send as "shift already
            // released" and strand the modifier down.
            const bool released = sentCount > reinjectCount + k;
            const bool restoredInBatch = sentCount > restoreStart + k;
            if (released && !restoredInBatch) {
                restoreShifts[restoreCount++] =
                    MakeKeyEvent(heldShifts[k], /*keyup=*/false);
            }
        }
        if (restoreCount > 0) {
            (void)Internal::TrackedSendInput(
                restoreShifts.data(), static_cast<UINT>(restoreCount));
        }
    }
    return delivered;
}

void Win32SendInputInjector::SendKey(unsigned short vkCode) noexcept {
    INPUT events[2] = {
        MakeKeyEvent(static_cast<WORD>(vkCode), /*keyup=*/false),
        MakeKeyEvent(static_cast<WORD>(vkCode), /*keyup=*/true),
    };
    // Fire-and-forget; partial-send detection logged by TrackedSendInput
    // but not actionable for SendKey (re-inject single key has no clean
    // fallback — the original use case at HookEngine line ~905 is
    // re-inject after synth-pending, not a primary delivery).
    (void)Internal::TrackedSendInput(events, 2);
}

}  // namespace NextKey::Output
