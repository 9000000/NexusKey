// tests/output/Win32SendInputInjectorTest.cpp
//
// Unit tests for Win32SendInputInjector. Mock SendInput via test seam
// (no GUI required); inspect captured INPUT events.
//
// Spec: docs/plans/sprint-2-output-injector.md §5.1, plan Task 1 step 2.
#include "InjectorTestBase.h"
#include "app/output/Win32SendInputInjector.h"

namespace NextKey::Output::Test {

class Win32SendInputInjectorTest : public InjectorTestBase {};

TEST_F(Win32SendInputInjectorTest, ReplaceEmptyTextSendsBackspacesOnly) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/false);
    EXPECT_TRUE(inj.Replace(3, L""));
    ASSERT_EQ(capturedInputs.size(), 6u);  // 3 BS × (down + up)
    for (size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(capturedInputs[i].ki.wVk, VK_BACK)
            << "event[" << i << "].wVk should be VK_BACK";
    }
    EXPECT_EQ(capturedInputs[0].ki.dwFlags & KEYEVENTF_KEYUP, 0u);  // first = down
    EXPECT_NE(capturedInputs[1].ki.dwFlags & KEYEVENTF_KEYUP, 0u);  // second = up
}

TEST_F(Win32SendInputInjectorTest, ReplaceWithCharsSendsBatch) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/false);
    EXPECT_TRUE(inj.Replace(2, L"vi"));
    // Expected: 2 BS down/up (4 events) + 2 chars down/up (4 events) = 8 events
    ASSERT_EQ(capturedInputs.size(), 8u);
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[2].ki.wVk, VK_BACK);
    // Char events use UNICODE flag + wScan
    EXPECT_NE(capturedInputs[4].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[4].ki.wScan, L'v');
    EXPECT_NE(capturedInputs[6].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[6].ki.wScan, L'i');
}

TEST_F(Win32SendInputInjectorTest, BaitCharPrefixWhenFlaggedAndPureBackspace) {
    // Chromium variant + pure-BS request → prepends U+202F + extra BS
    // (replicates HookEngine::SendBackspaces line ~3121).
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.Replace(2, L""));
    // Expected: 1 bait char (down + up = 2) + 3 BS down/up (= 6) = 8 events
    // (3 BS = original 2 + 1 extra to delete the bait)
    ASSERT_EQ(capturedInputs.size(), 8u);
    EXPECT_NE(capturedInputs[0].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[0].ki.wScan, 0x202F);
    EXPECT_EQ(capturedInputs[0].ki.dwFlags & KEYEVENTF_KEYUP, 0u);  // bait down
    EXPECT_NE(capturedInputs[1].ki.dwFlags & KEYEVENTF_KEYUP, 0u);  // bait up
    for (size_t i = 2; i < 8; ++i) {
        EXPECT_EQ(capturedInputs[i].ki.wVk, VK_BACK);
    }
}

TEST_F(Win32SendInputInjectorTest, BaitCharFiresEvenWhenTextNonEmpty) {
    // D3 contract change: the bait fires whenever bsCount > 0 on the
    // Chromium variant — partial-replace (BS + chars) needs autocomplete
    // dismissed too, not just pure-BS. Previously (D1) bait was gated on
    // text.empty(). HookEngine ReplaceComposition's pre-injector logic
    // (lines ~2891 / ~3026) already emitted bait in this configuration —
    // the gate is moved into the injector to centralize the channel quirk.
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.Replace(1, L"x"));
    // Expected: bait (down+up = 2) + 2 BS down/up (= 4: 1 orig + 1 extra
    // to delete the bait) + 1 char down/up (= 2) = 8 events.
    ASSERT_EQ(capturedInputs.size(), 8u);
    EXPECT_NE(capturedInputs[0].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[0].ki.wScan, 0x202F);
    EXPECT_EQ(capturedInputs[2].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[4].ki.wVk, VK_BACK);
    EXPECT_NE(capturedInputs[6].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[6].ki.wScan, L'x');
}

TEST_F(Win32SendInputInjectorTest, BaitCharSkippedWhenBsCountZero) {
    // Pure-typing (no deletions) on the Chromium variant: no bait,
    // no extra BS — just the chars. Autocomplete-dismiss isn't needed
    // when nothing is being removed.
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/true);
    EXPECT_TRUE(inj.Replace(0, L"y"));
    ASSERT_EQ(capturedInputs.size(), 2u);  // 1 char × down+up
    EXPECT_NE(capturedInputs[0].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[0].ki.wScan, L'y');
}

TEST_F(Win32SendInputInjectorTest, ReplaceReturnsFalseOnPartialSend) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/false);
    sendInputReturnOverride = 2;  // simulate partial: 2 events delivered out of 6
    EXPECT_FALSE(inj.Replace(3, L""));
}

TEST_F(Win32SendInputInjectorTest, SendKeyEmitsDownAndUpWithMarker) {
    Win32SendInputInjector inj(/*needsBaitCharPrefix=*/false);
    inj.SendKey(VK_BACK);
    ASSERT_EQ(capturedInputs.size(), 2u);
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[0].ki.dwExtraInfo, Internal::kNexusKeyExtraInfo);
    EXPECT_EQ(capturedInputs[0].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
    EXPECT_NE(capturedInputs[1].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
}

}  // namespace NextKey::Output::Test
