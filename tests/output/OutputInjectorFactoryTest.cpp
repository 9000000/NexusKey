// tests/output/OutputInjectorFactoryTest.cpp
//
// Unit tests for OutputInjectorFactory::Create dispatch. Covers the
// 4 classification branches (RichEditD2DPT, Electron, Console, Win32
// default). dynamic_cast asserts the right concrete impl is returned.
//
// Why this exists (Gotcha G3 from D2 handoff): a previous attempt
// shipped Create() still hardcoded to Win32SendInputInjector while
// the chaos sweep passed by coincidence. A factory smoke test catches
// that class of bug before chaos.
//
// Spec: docs/plans/sprint-2-output-injector.md §5.2
#include "app/output/OutputInjectorFactory.h"
#include "app/output/Win32SendInputInjector.h"
#include "app/output/RichEditEmReplaceSelInjector.h"
#include "app/output/SplitDispatchInjector.h"

#include <gtest/gtest.h>

namespace NextKey::Output::Test {

TEST(OutputInjectorFactoryTest, DefaultClassificationReturnsWin32) {
    WindowClassification c{};  // all flags false
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_NE(dynamic_cast<Win32SendInputInjector*>(inj.get()), nullptr);
}

TEST(OutputInjectorFactoryTest, ChromiumFlagStillWin32ButCarriesBaitHint) {
    WindowClassification c{};
    c.isChromium = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_NE(dynamic_cast<Win32SendInputInjector*>(inj.get()), nullptr);
    // (Bait-char prefix behavior verified separately in
    // Win32SendInputInjectorTest.BaitCharFiresOnReplaceWithText.)
}

TEST(OutputInjectorFactoryTest, RichEditD2DPTReturnsRichEditImpl) {
    WindowClassification c{};
    c.isRichEditD2DPT = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_NE(dynamic_cast<RichEditEmReplaceSelInjector*>(inj.get()), nullptr);
}

TEST(OutputInjectorFactoryTest, ElectronReturnsSplitDispatch) {
    WindowClassification c{};
    c.isElectron = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_NE(dynamic_cast<SplitDispatchInjector*>(inj.get()), nullptr);
}

TEST(OutputInjectorFactoryTest, ConsoleReturnsSplitDispatch) {
    WindowClassification c{};
    c.isConsole = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_NE(dynamic_cast<SplitDispatchInjector*>(inj.get()), nullptr);
}

TEST(OutputInjectorFactoryTest, RichEditWinsOverElectronAndConsole) {
    // Priority order: RichEdit > Electron > Console > Win32. A pathological
    // classification with all three flags must dispatch to RichEdit.
    WindowClassification c{};
    c.isRichEditD2DPT = true;
    c.isElectron = true;
    c.isConsole = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_NE(dynamic_cast<RichEditEmReplaceSelInjector*>(inj.get()), nullptr);
}

TEST(OutputInjectorFactoryTest, ElectronWinsOverConsole) {
    WindowClassification c{};
    c.isElectron = true;
    c.isConsole = true;
    auto inj = Create(c);
    ASSERT_NE(inj, nullptr);
    EXPECT_NE(dynamic_cast<SplitDispatchInjector*>(inj.get()), nullptr);
    // Could additionally check sleepMs by exposing a getter, but the
    // dispatch type is the contract — sleepMs is internal.
}

TEST(OutputInjectorFactoryTest, SettleBudgetReflectsImpl) {
    using namespace std::chrono_literals;

    WindowClassification c{};
    EXPECT_EQ(Create(c)->SettleBudget(), 30ms);  // Win32 default

    c = {};
    c.isRichEditD2DPT = true;
    EXPECT_EQ(Create(c)->SettleBudget(), 0ms);  // RichEdit (sent-message, drains synchronously)

    c = {};
    c.isElectron = true;
    EXPECT_EQ(Create(c)->SettleBudget(), 100ms);  // Electron event-loop margin

    c = {};
    c.isConsole = true;
    EXPECT_EQ(Create(c)->SettleBudget(), 100ms);  // Console (split path inherits same budget)
}

}  // namespace NextKey::Output::Test
