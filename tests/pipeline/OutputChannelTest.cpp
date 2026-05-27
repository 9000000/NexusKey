// tests/pipeline/OutputChannelTest.cpp
#include <gtest/gtest.h>
#include "core/pipeline/OutputChannel.h"

using namespace NextKey::Pipeline;

TEST(OutputChannel, EmitAccumulatesInOrder) {
    OutputChannel ch;
    ch.Emit(Intents::Backspace{ .count = 2 });
    ch.Emit(Intents::Text{ .text = L"ê" });
    ch.Emit(Intents::Reinject{ .vk = 0x45 });

    auto batch = ch.TakeBatch();
    ASSERT_EQ(batch.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<Intents::Backspace>(batch[0]));
    EXPECT_EQ(std::get<Intents::Backspace>(batch[0]).count, 2u);
    EXPECT_TRUE(std::holds_alternative<Intents::Text>(batch[1]));
    EXPECT_EQ(std::get<Intents::Text>(batch[1]).text, std::wstring{L"ê"});
    EXPECT_TRUE(std::holds_alternative<Intents::Reinject>(batch[2]));
    EXPECT_EQ(std::get<Intents::Reinject>(batch[2]).vk, 0x45u);
}

TEST(OutputChannel, TakeBatchEmptiesInternalBuffer) {
    OutputChannel ch;
    ch.Emit(Intents::Backspace{ .count = 1 });
    auto first = ch.TakeBatch();
    auto second = ch.TakeBatch();
    EXPECT_EQ(first.size(), 1u);
    EXPECT_EQ(second.size(), 0u);
}
