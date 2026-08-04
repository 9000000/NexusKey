// MacroTableDecisionTest.cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// Regression coverage for the macro-table refresh rule behind #227 / #231 /
// #209: a settings save bumps configGeneration, and the TSF DLL must pick the
// new table up from a key callback — Chromium hosts fire OnSetFocus exactly
// once (at activation), so "wait for focus" is not a recovery path there.
//
// The two invariants under test:
//   1. Key-path I/O is bounded to ONE read attempt per generation.
//   2. The table is never dropped — a stale table beats no table.

#include <gtest/gtest.h>

#include "core/MacroTableDecision.h"

using namespace NextKey;

namespace {
constexpr MacroTableAction KeyPath(bool loaded, uint8_t loadedGen, uint8_t stateGen) {
    return DecideMacroTable({.loaded = loaded, .loadedGen = loadedGen,
                             .stateGen = stateGen, .allowDiskRead = false});
}
constexpr MacroTableAction FocusPath(bool loaded, uint8_t loadedGen, uint8_t stateGen) {
    return DecideMacroTable({.loaded = loaded, .loadedGen = loadedGen,
                             .stateGen = stateGen, .allowDiskRead = true});
}
}  // namespace

TEST(MacroTableDecisionTest, CurrentGenerationNeedsNothing) {
    EXPECT_EQ(MacroTableAction::None, FocusPath(true, 7, 7));
    // The hot case: every keystroke after a successful load touches no file.
    EXPECT_EQ(MacroTableAction::None, KeyPath(true, 7, 7));
}

TEST(MacroTableDecisionTest, GenerationBumpReloadsFromEitherPath) {
    EXPECT_EQ(MacroTableAction::Reload, FocusPath(true, 7, 8));
    // A settings save must reach the user mid-typing without a focus event.
    EXPECT_EQ(MacroTableAction::Reload, KeyPath(true, 7, 8));
}

// Bounded key-path I/O: the generation moved, we attempted the read, it failed
// (so nothing latched) — do NOT stat the file again on every subsequent
// keystroke. Keep serving what we have and leave the retry to focus/init.
TEST(MacroTableDecisionTest, FailedKeyPathReadIsNotRetriedPerKeystroke) {
    EXPECT_EQ(MacroTableAction::KeepStale, KeyPath(false, 9, 9));
}

// ...but focus/init keeps retrying a generation it has never read.
TEST(MacroTableDecisionTest, FocusPathRetriesFailedReadAtSameGeneration) {
    EXPECT_EQ(MacroTableAction::Reload, FocusPath(false, 9, 9));
}

// Walks the whole reported failure sequence: load OK → settings save →
// keystroke → the table must be refreshed, never dropped, and then go quiet.
TEST(MacroTableDecisionTest, SettingsSaveMidTypingReloadsThenSettles) {
    bool loaded = true;
    uint8_t loadedGen = 3;
    const uint8_t savedGen = 4;  // EXE bumped configGeneration

    ASSERT_EQ(MacroTableAction::Reload, KeyPath(loaded, loadedGen, savedGen));
    // ReloadMacros succeeds → latches (loaded, gen).
    loaded = true;
    loadedGen = savedGen;
    // Every later keystroke at this generation is free.
    EXPECT_EQ(MacroTableAction::None, KeyPath(loaded, loadedGen, savedGen));
    EXPECT_EQ(MacroTableAction::None, KeyPath(loaded, loadedGen, savedGen));
}

TEST(MacroTableDecisionTest, FreshInstanceLoadsOnFirstTick) {
    EXPECT_EQ(MacroTableAction::Reload, FocusPath(false, 0, 0));
    // A brand-new instance has latched nothing, so even the key path reads —
    // loadedGen == stateGen == 0 would otherwise strand it, hence the retry
    // belongs to focus/init, which construction always runs.
    EXPECT_EQ(MacroTableAction::KeepStale, KeyPath(false, 0, 0));
}

// configGeneration is a uint8_t and wraps (SharedState.h: "Wraps at 255 — use
// != comparison, not >"). A wrap landing back on the latched value reads as
// unchanged; the next real change moves it off again.
TEST(MacroTableDecisionTest, GenerationWrapUsesInequality) {
    EXPECT_EQ(MacroTableAction::None, KeyPath(true, 255, 255));
    EXPECT_EQ(MacroTableAction::Reload, KeyPath(true, 255, 0));
}
