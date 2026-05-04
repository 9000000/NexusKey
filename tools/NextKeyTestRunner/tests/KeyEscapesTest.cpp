#include <gtest/gtest.h>

#include "KeyEscapes.h"

namespace NextKey::TestRunner::Test {

TEST(KeyEscapesTest, EmptyInputProducesEmptyOutput) {
    auto out = KeyEscapes::Resolve(u"");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, u"");
}

TEST(KeyEscapesTest, NoEscapesPassesThrough) {
    auto out = KeyEscapes::Resolve(u"vieejt");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, u"vieejt");
}

TEST(KeyEscapesTest, BackspaceEscapeResolvesToU0008) {
    auto out = KeyEscapes::Resolve(u"a\\bb");
    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->size(), 3u);
    EXPECT_EQ((*out)[0], u'a');
    EXPECT_EQ((*out)[1], u'\b');
    EXPECT_EQ((*out)[2], u'b');
}

TEST(KeyEscapesTest, AllRecognizedEscapes) {
    auto out = KeyEscapes::Resolve(u"\\b\\t\\n\\r\\\\\\\"");
    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->size(), 6u);
    EXPECT_EQ((*out)[0], u'\b');
    EXPECT_EQ((*out)[1], u'\t');
    EXPECT_EQ((*out)[2], u'\n');
    EXPECT_EQ((*out)[3], u'\r');
    EXPECT_EQ((*out)[4], u'\\');
    EXPECT_EQ((*out)[5], u'"');
}

TEST(KeyEscapesTest, MultipleBackspaceSequence) {
    // toans + 3x BS + i  -- the chaos test #1.2 from the brainstorm corpus.
    auto out = KeyEscapes::Resolve(u"toans\\b\\b\\bi");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, std::u16string(u"toans\b\b\bi"));
}

TEST(KeyEscapesTest, TrailingBackslashIsError) {
    auto out = KeyEscapes::Resolve(u"abc\\");
    EXPECT_FALSE(out.has_value());
}

TEST(KeyEscapesTest, UnknownEscapeIsError) {
    auto out = KeyEscapes::Resolve(u"a\\xb");
    EXPECT_FALSE(out.has_value());
}

TEST(KeyEscapesTest, VietnameseCharsPassThrough) {
    auto out = KeyEscapes::Resolve(u"việt");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, std::u16string(u"việt"));
}

}  // namespace NextKey::TestRunner::Test
