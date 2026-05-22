// tests/brain/IntentTest.cpp
#include <gtest/gtest.h>
#include <variant>
#include <string>
#include "core/brain/Intent.h"

using namespace NextKey::Brain;

TEST(Intent, BackspaceHoldsCount) {
    Intent i = Intents::Backspace{ .count = 2 };
    ASSERT_TRUE(std::holds_alternative<Intents::Backspace>(i));
    EXPECT_EQ(std::get<Intents::Backspace>(i).count, 2u);
}

TEST(Intent, TextHoldsWstring) {
    Intent i = Intents::Text{ .text = L"ê" };
    ASSERT_TRUE(std::holds_alternative<Intents::Text>(i));
    EXPECT_EQ(std::get<Intents::Text>(i).text, std::wstring{L"ê"});
}

TEST(Intent, ReinjectHoldsVk) {
    Intent i = Intents::Reinject{ .vk = 0x45 };
    ASSERT_TRUE(std::holds_alternative<Intents::Reinject>(i));
    EXPECT_EQ(std::get<Intents::Reinject>(i).vk, 0x45u);
}
