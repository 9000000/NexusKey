// VKey - OutputDispatcher retry-budget regression tests
// SPDX-License-Identifier: GPL-3.0-only

#include "InjectorTestBase.h"

#include "app/output/IOutputInjector.h"
#include "app/system/FocusOwner.h"
#include "app/system/OutputDispatcher.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <windows.h>

namespace NextKey::Output::Test {
namespace {

class MessageInjectorBase : public IOutputInjector {
public:
    void SendKey(unsigned short) noexcept override {}

    [[nodiscard]] std::chrono::milliseconds SettleBudget() const noexcept override {
        return std::chrono::milliseconds{0};
    }

    [[nodiscard]] bool IsMessageBasedReplace() const noexcept override {
        return true;
    }

    [[nodiscard]] bool IsForced() const noexcept override {
        return true;
    }

    std::uint32_t replaceCalls = 0;
};

class SlowFailingMessageInjector final : public MessageInjectorBase {
public:
    [[nodiscard]] bool Replace(std::size_t,
                               std::wstring_view,
                               unsigned short = 0) noexcept override {
        ++replaceCalls;
        ::Sleep(50);
        return false;
    }
};

class SlowSuccessfulMessageInjector final : public MessageInjectorBase {
public:
    [[nodiscard]] bool Replace(std::size_t,
                               std::wstring_view,
                               unsigned short = 0) noexcept override {
        ++replaceCalls;
        ::Sleep(50);
        return true;
    }
};

class FailOnceMessageInjector final : public MessageInjectorBase {
public:
    [[nodiscard]] bool Replace(std::size_t,
                               std::wstring_view,
                               unsigned short = 0) noexcept override {
        ++replaceCalls;
        return replaceCalls >= 2;
    }
};

class OutputDispatcherRetryBudgetTest : public InjectorTestBase {
protected:
    void SetUp() override {
        InjectorTestBase::SetUp();

        parentWindow_ = CreateWindowExW(
            0, L"STATIC", L"VKey dispatcher retry test", WS_OVERLAPPED,
            0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ASSERT_NE(parentWindow_, nullptr);

        editWindow_ = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | WS_TABSTOP,
            0, 0, 80, 20, parentWindow_, nullptr, GetModuleHandleW(nullptr), nullptr);
        ASSERT_NE(editWindow_, nullptr);

        previousFocus_ = SetFocus(editWindow_);
        ASSERT_EQ(GetFocus(), editWindow_);

        focus_.RefreshFocusCache(parentWindow_);
        ASSERT_EQ(focus_.CachedFocusedHwnd(), editWindow_);
        ASSERT_EQ(focus_.CachedFocusedClass(), L"Edit");
    }

    void TearDown() override {
        if (previousFocus_ && IsWindow(previousFocus_)) {
            SetFocus(previousFocus_);
        }
        if (editWindow_) DestroyWindow(editWindow_);
        if (parentWindow_) DestroyWindow(parentWindow_);
        InjectorTestBase::TearDown();
    }

    FocusOwner focus_;
    HWND parentWindow_ = nullptr;
    HWND editWindow_ = nullptr;
    HWND previousFocus_ = nullptr;
};

TEST_F(OutputDispatcherRetryBudgetTest, SlowFailureIsNotRetriedAfterWallClockBudget) {
    auto injector = std::make_shared<SlowFailingMessageInjector>();
    OutputDispatcher dispatcher(focus_);
    dispatcher.SetInjector(injector);

    dispatcher.ReplaceUnicode(1, L"x", 0);

    EXPECT_EQ(injector->replaceCalls, 1u);
    ASSERT_EQ(capturedInputs.size(), 4u)
        << "deadline exhaustion must preserve exactly one SendInput fallback batch";
    EXPECT_EQ(capturedInputs[0].ki.wVk, VK_BACK);
    EXPECT_EQ(capturedInputs[0].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
    EXPECT_EQ(capturedInputs[1].ki.wVk, VK_BACK);
    EXPECT_NE(capturedInputs[1].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
    EXPECT_EQ(capturedInputs[2].ki.wScan, L'x');
    EXPECT_NE(capturedInputs[2].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_EQ(capturedInputs[2].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
    EXPECT_EQ(capturedInputs[3].ki.wScan, L'x');
    EXPECT_NE(capturedInputs[3].ki.dwFlags & KEYEVENTF_UNICODE, 0u);
    EXPECT_NE(capturedInputs[3].ki.dwFlags & KEYEVENTF_KEYUP, 0u);
    for (const auto& input : capturedInputs) {
        EXPECT_EQ(input.ki.dwExtraInfo, Internal::kVKeyExtraInfo);
    }
    EXPECT_EQ(synthCounterDeltas, (std::vector<int>{4}));
    EXPECT_FALSE(dispatcher.IsSending());
}

TEST_F(OutputDispatcherRetryBudgetTest, FastTransientFailureStillRetriesBeforeFallback) {
    auto injector = std::make_shared<FailOnceMessageInjector>();
    OutputDispatcher dispatcher(focus_);
    dispatcher.SetInjector(injector);

    dispatcher.ReplaceUnicode(1, L"x", 0);

    EXPECT_EQ(injector->replaceCalls, 2u);
    EXPECT_TRUE(capturedInputs.empty()) << "successful retry must not also run SendInput fallback";
    EXPECT_TRUE(synthCounterDeltas.empty());
    EXPECT_FALSE(dispatcher.IsSending());
}

TEST_F(OutputDispatcherRetryBudgetTest, SlowSuccessIsAcceptedWithoutFallback) {
    auto injector = std::make_shared<SlowSuccessfulMessageInjector>();
    OutputDispatcher dispatcher(focus_);
    dispatcher.SetInjector(injector);

    dispatcher.ReplaceUnicode(1, L"x", 0);

    EXPECT_EQ(injector->replaceCalls, 1u);
    EXPECT_TRUE(capturedInputs.empty());
    EXPECT_TRUE(synthCounterDeltas.empty());
    EXPECT_FALSE(dispatcher.IsSending());
}

}  // namespace
}  // namespace NextKey::Output::Test
