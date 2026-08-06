// FocusInterleavingTest.cpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Phase 2b — two-phase focus. See
// docs/plans/2026-05-19-architecture-review-design.md §Phase 2.
//
// Locks the drain-dispatch contract for the four mailbox bits:
//   1. Drain order is kConfigApply → kFocusChanged → kTickPoll → kToggleVN.
//      Rationale: kConfigApply may rebuild engine_, so it must run before
//      the focus handler picks an injector / resets composition. kFocusChanged
//      resets composition state — it must run before kTickPoll (which assumes
//      a consistent state) and before kToggleVN (which may snapshot the
//      current composition for commit).
//   2. ConsumePendingFocus is called exactly once per drained kFocusChanged
//      bit. The "latest wins" coalesce is locked at the data-structure level
//      in HookCommandMailboxTest; here we verify the integration contract.
//   3. Mid-drain Post — if a producer fires kFocusChanged DURING dispatch
//      (e.g. nested callback re-entry), the new bit is NOT consumed in the
//      current drain pass. It lands in the next drain (next keystroke or
//      pump tick). This prevents unbounded recursion + matches the design's
//      "drain handlers MUST NOT call composition-mutating APIs" contract.
//
// Linux-portable: exercises HookCommandMailbox + a test harness that
// mirrors HookEngine::DrainHookCommands's dispatch logic. Production code
// in HookEngine.cpp must follow the same order (verified by code review +
// chaos integration on Windows).

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/system/HookCommandMailbox.h"
#include "core/FocusApplyDecision.h"

namespace NextKey {
namespace {

using ::NextKey::HookCommand::kFocusChanged;
using ::NextKey::HookCommand::kConfigApply;
using ::NextKey::HookCommand::kTickPoll;
using ::NextKey::HookCommand::kToggleVN;

// Test-side mirror of HookEngine::DrainHookCommands. Production must keep
// the SAME dispatch order; that's the invariant this suite locks. Any
// reorder in HookEngine.cpp without updating the matching test here is a
// review red flag.
struct DrainHarness {
    HookCommandMailbox& mailbox;
    std::vector<std::string> log;            // ordered sequence of handler hits
    std::shared_ptr<const FocusClassification> lastConsumedFocus;
    int focusHits = 0;
    int configHits = 0;
    int tickHits = 0;
    int toggleHits = 0;

    void Drain() {
        const std::uint32_t bits = mailbox.DrainBits();
        if (!bits) return;
        if (bits & kConfigApply) {
            log.emplace_back("config");
            ++configHits;
        }
        if (bits & kFocusChanged) {
            lastConsumedFocus = mailbox.ConsumePendingFocus();
            log.emplace_back("focus");
            ++focusHits;
        }
        if (bits & kTickPoll) {
            log.emplace_back("tick");
            ++tickHits;
        }
        if (bits & kToggleVN) {
            log.emplace_back("toggle");
            ++toggleHits;
        }
    }
};

// Executable interleaving model for the focus-reset portion of the real
// producer → worker → mailbox → hook pipeline:
//
//   OnFocusChanged() records a focus request
//   worker Classify() posts the immutable snapshot
//   LowLevelKeyboardProc drains, then marks the current physical key-down
//   RouteFocusOnHookThread drops, defers or applies the typing transaction
//
// The harness uses the same pure decision as production. It models only the
// composition lifetime and deferred slot; app/injector/SmartSwitch side
// effects stay in HookEngine's Windows-only apply body.
struct FocusResetInterleavingHarness {
    HookCommandMailbox& mailbox;
    std::uint64_t inputEpoch{0};
    std::uint64_t focusRequestedAtInputEpoch{0};
    std::uint64_t latestFocusRequestSerial{0};
    std::uint64_t focusRequestSerial{0};
    std::wstring composition;
    std::shared_ptr<const FocusClassification> deferredFocus;
    int appliedFocusCount{0};
    int droppedFocusCount{0};
    int processedKeyCount{0};
    std::uint64_t lastAppliedRequestSerial{0};

    void RequestFocusClassification() {
        focusRequestSerial = ++latestFocusRequestSerial;
        focusRequestedAtInputEpoch = inputEpoch;
    }

    void PostFocusClassification() {
        FocusClassification cls{};
        cls.hwndOpaque = 0x1234;
        cls.requestSerial = focusRequestSerial;
        cls.inputEpochAtRequest = focusRequestedAtInputEpoch;
        mailbox.Post(kFocusChanged,
                     std::make_shared<const FocusClassification>(std::move(cls)));
    }

    void TypeNewComposition(std::wstring text) {
        ++inputEpoch;
        composition = std::move(text);
    }

    void BeginNextKey() {
        // Production drains before advancing the physical-input epoch. The
        // current key therefore cannot make an otherwise fresh result look
        // stale; only earlier physical key-downs can overtake it.
        Drain();
        ++inputEpoch;
    }

    void BackspaceNextKey() {
        // Mirrors the production order: route pending commands before
        // dispatching the physical key. The focus route itself never owns or
        // consumes that key; Backspace still reaches the live composition.
        Drain();
        ++inputEpoch;
        ++processedKeyCount;
        if (!composition.empty()) composition.pop_back();
    }

    void Route(std::shared_ptr<const FocusClassification> cls) {
        if (!cls || !cls->hwndOpaque) return;
        const FocusApplyInputs in{
            .snapshotRequestSerial = cls->requestSerial,
            .latestRequestSerial = latestFocusRequestSerial,
            .snapshotInputEpoch = cls->inputEpochAtRequest,
            .currentInputEpoch = inputEpoch,
            .hasComposition = !composition.empty(),
        };
        switch (DecideFocusApply(in)) {
            case FocusApplyDisposition::DropStale:
                ++droppedFocusCount;
                return;
            case FocusApplyDisposition::DeferUntilBoundary:
                deferredFocus = std::move(cls);
                return;
            case FocusApplyDisposition::ApplyNow:
                deferredFocus.reset();
                composition.clear();
                ++appliedFocusCount;
                lastAppliedRequestSerial = cls->requestSerial;
                return;
        }
    }

    void Drain() {
        const std::uint32_t bits = mailbox.DrainBits();
        if (bits & kFocusChanged) {
            Route(mailbox.ConsumePendingFocus());
        }
        if (deferredFocus) {
            const auto pending = deferredFocus;
            const FocusApplyInputs in{
                .snapshotRequestSerial = pending->requestSerial,
                .latestRequestSerial = latestFocusRequestSerial,
                .snapshotInputEpoch = pending->inputEpochAtRequest,
                .currentInputEpoch = inputEpoch,
                .hasComposition = !composition.empty(),
            };
            const auto disposition = DecideFocusApply(in);
            if (disposition == FocusApplyDisposition::DropStale) {
                deferredFocus.reset();
                ++droppedFocusCount;
            } else if (disposition == FocusApplyDisposition::ApplyNow) {
                lastAppliedRequestSerial = pending->requestSerial;
                deferredFocus.reset();
                composition.clear();
                ++appliedFocusCount;
            }
        }
    }
};

class FocusInterleavingTest : public ::testing::Test {
protected:
    HookCommandMailbox mailbox_;
    DrainHarness       harness_{mailbox_, {}, nullptr, 0, 0, 0, 0};
};

// ──────────────────────────────────────────────────────────────────────────
// Drain order contract — kConfigApply first, then kFocusChanged, then
// kTickPoll, then kToggleVN. The test posts in REVERSE order to confirm
// dispatch order is determined by the drain code, not by post order.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, DrainOrderConfigFirstFocusSecondTickThirdToggleFourth) {
    mailbox_.Post(kToggleVN);
    mailbox_.Post(kTickPoll);
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    mailbox_.Post(kConfigApply);
    harness_.Drain();

    ASSERT_EQ(harness_.log.size(), 4u);
    EXPECT_EQ(harness_.log[0], "config")
        << "config must dispatch first — engine_ may be rebuilt before focus runs";
    EXPECT_EQ(harness_.log[1], "focus")
        << "focus second — ResetComposition + state writes after engine is fresh";
    EXPECT_EQ(harness_.log[2], "tick");
    EXPECT_EQ(harness_.log[3], "toggle");
}

TEST_F(FocusInterleavingTest, DrainOrderHoldsForSingleBitAsWell) {
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    harness_.Drain();
    ASSERT_EQ(harness_.log.size(), 1u);
    EXPECT_EQ(harness_.log[0], "focus");
    EXPECT_EQ(harness_.focusHits, 1);
    EXPECT_EQ(harness_.configHits, 0);
}

// ──────────────────────────────────────────────────────────────────────────
// pendingFocus consumption — exactly once per drained kFocusChanged bit
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, FocusConsumedExactlyOncePerDrainedBit) {
    auto cls = std::make_shared<const FocusClassification>();
    auto raw = cls.get();
    mailbox_.Post(kFocusChanged, std::move(cls));
    harness_.Drain();

    ASSERT_TRUE(harness_.lastConsumedFocus);
    EXPECT_EQ(harness_.lastConsumedFocus.get(), raw);
    // After drain, mailbox has nothing pending.
    EXPECT_FALSE(mailbox_.ConsumePendingFocus())
        << "drain consumed the pending focus — second consume sees null";
}

TEST_F(FocusInterleavingTest, FocusCoalesceLatestWinsAcrossPosts) {
    auto cls1 = std::make_shared<const FocusClassification>();
    auto cls2 = std::make_shared<const FocusClassification>();
    // Pre-publish mutation via const_cast — the snapshot is held by only us
    // here so there's no race. WinEventProc will follow the same pattern:
    // build the struct, then post-as-const so the hook-thread reader gets
    // an immutable view.
    const_cast<FocusClassification*>(cls1.get())->hwndOpaque = 0x1111;
    const_cast<FocusClassification*>(cls2.get())->hwndOpaque = 0x2222;

    mailbox_.Post(kFocusChanged, std::move(cls1));
    mailbox_.Post(kFocusChanged, std::move(cls2));
    harness_.Drain();

    ASSERT_TRUE(harness_.lastConsumedFocus);
    EXPECT_EQ(harness_.lastConsumedFocus->hwndOpaque, std::uintptr_t{0x2222})
        << "two focus posts with no drain in between collapse to the LATEST "
           "snapshot — older transient HWNDs (taskbar/JumpList) are correctly "
           "discarded";
    // Wake fires once for the burst (data-structure contract from
    // HookCommandMailboxTest), not twice.
}

// ──────────────────────────────────────────────────────────────────────────
// Mid-drain Post — drain takes a snapshot then dispatches. If another
// thread Posts during dispatch, the new bit is NOT visible in the current
// drain's dispatched bits; it lands in the next DrainBits() call.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, MidDrainPostDoesNotInfluenceCurrentPass) {
    // First pass: only kFocusChanged. The "mid-drain Post" simulation: we
    // post kConfigApply AFTER harness_.Drain has already snapshot-ed bits.
    // Since DrainBits is atomic exchange(0), the new post writes to a fresh
    // bit field — must surface on a SECOND drain, not the first.
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    harness_.Drain();
    ASSERT_EQ(harness_.log.size(), 1u);
    EXPECT_EQ(harness_.log[0], "focus");

    // Second pass: a producer post that arrived after the first drain.
    mailbox_.Post(kConfigApply);
    harness_.Drain();
    ASSERT_EQ(harness_.log.size(), 2u);
    EXPECT_EQ(harness_.log[1], "config")
        << "post after a drain must land in the next drain — never lost, "
           "never merged retroactively into the previous pass";
}

// ──────────────────────────────────────────────────────────────────────────
// Empty drain is a no-op — no handlers fire, no focus consumed
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, EmptyMailboxDrainNoOps) {
    harness_.Drain();
    EXPECT_EQ(harness_.log.size(), 0u);
    EXPECT_EQ(harness_.focusHits, 0);
    EXPECT_FALSE(harness_.lastConsumedFocus);
}

// ──────────────────────────────────────────────────────────────────────────
// Repeated drain passes — typical hook-callback pattern, one drain per
// keystroke. Bits accumulated between keystrokes get dispatched on the
// next keystroke's drain.
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, RepeatedDrainPassesEachDispatchesItsAccumulation) {
    // Keystroke 1 — focus changed
    mailbox_.Post(kFocusChanged, std::make_shared<const FocusClassification>());
    harness_.Drain();
    EXPECT_EQ(harness_.focusHits, 1);

    // Keystroke 2 — config save fired between keystrokes
    mailbox_.Post(kConfigApply);
    harness_.Drain();
    EXPECT_EQ(harness_.configHits, 1);

    // Keystroke 3 — nothing happened; drain is a cheap no-op
    harness_.Drain();
    EXPECT_EQ(harness_.focusHits, 1);  // unchanged
    EXPECT_EQ(harness_.configHits, 1); // unchanged

    // Keystroke 4 — burst: tick + toggle
    mailbox_.Post(kTickPoll);
    mailbox_.Post(kToggleVN);
    harness_.Drain();
    EXPECT_EQ(harness_.tickHits, 1);
    EXPECT_EQ(harness_.toggleHits, 1);
}

// ──────────────────────────────────────────────────────────────────────────
// kFocusChanged without a payload — defensive contract.
// Producer convention: every kFocusChanged Post should carry a non-null
// FocusClassification. But if some path posts the bit alone, ApplyFocus
// must handle ConsumePendingFocus returning null gracefully (no crash).
// ──────────────────────────────────────────────────────────────────────────
TEST_F(FocusInterleavingTest, FocusBitWithoutPayloadIsSafe) {
    mailbox_.Post(kFocusChanged);  // no shared_ptr
    harness_.Drain();
    EXPECT_EQ(harness_.focusHits, 1);
    EXPECT_FALSE(harness_.lastConsumedFocus)
        << "missing payload yields null — ApplyFocusOnHookThread MUST tolerate "
           "this (it should early-return on null cls).";
}

// A focus result that is consumed before any newer input still represents the
// current editing-context transition and must reset the old composition.
TEST_F(FocusInterleavingTest, FreshFocusResultResetsPreviousComposition) {
    FocusResetInterleavingHarness focus{mailbox_};
    focus.composition = L"old-context";
    focus.RequestFocusClassification();
    focus.PostFocusClassification();
    focus.Drain();

    EXPECT_TRUE(focus.composition.empty());
}

// Regression probe for the reported intermittent "visible Vietnamese word,
// but tone + in-word Backspace stop working" symptom. The worker result began
// before this word existed; the first new key marks activity, then drains that
// stale result. Resetting here erases only the engine-side composition while
// the host UI keeps the already displayed text.
TEST_F(FocusInterleavingTest, FocusResultOvertakenByNewInputMustNotResetLiveComposition) {
    FocusResetInterleavingHarness focus{mailbox_};
    focus.RequestFocusClassification();
    // Worker is still classifying while earlier keys build a live word.
    focus.TypeNewComposition(L"viet");
    // The result arrives only after that word is visible. The next tone/BS key
    // marks activity, then drains the delayed result before processing itself.
    focus.PostFocusClassification();
    ASSERT_GT(focus.inputEpoch, focus.focusRequestedAtInputEpoch);

    focus.Drain();

    EXPECT_EQ(focus.composition, L"viet")
        << "a focus snapshot older than the latest real input must not erase "
           "the live composition created after that snapshot began";
    EXPECT_TRUE(focus.deferredFocus);
    EXPECT_EQ(focus.appliedFocusCount, 0);
}

TEST_F(FocusInterleavingTest, DeferredLatestFocusAppliesAfterWordBoundary) {
    FocusResetInterleavingHarness focus{mailbox_};
    focus.RequestFocusClassification();
    focus.TypeNewComposition(L"viet");
    focus.PostFocusClassification();
    focus.Drain();
    ASSERT_TRUE(focus.deferredFocus);

    // Space/commit has completed: the next natural drain observes an empty
    // engine and applies the retained latest snapshot before the next word.
    focus.composition.clear();
    focus.Drain();

    EXPECT_FALSE(focus.deferredFocus);
    EXPECT_EQ(focus.appliedFocusCount, 1);
}

TEST_F(FocusInterleavingTest, NewerFocusRequestInvalidatesDeferredOlderSnapshot) {
    FocusResetInterleavingHarness focus{mailbox_};
    focus.RequestFocusClassification();  // request #1
    focus.TypeNewComposition(L"viet");
    focus.PostFocusClassification();
    focus.Drain();
    ASSERT_TRUE(focus.deferredFocus);

    focus.RequestFocusClassification();  // request #2 supersedes #1
    focus.Drain();

    EXPECT_FALSE(focus.deferredFocus);
    EXPECT_EQ(focus.appliedFocusCount, 0);
    EXPECT_EQ(focus.droppedFocusCount, 1);
    EXPECT_EQ(focus.composition, L"viet");
}

TEST_F(FocusInterleavingTest, CurrentKeyDoesNotFalselyOvertakeFreshFocus) {
    FocusResetInterleavingHarness focus{mailbox_};
    focus.composition = L"old-context";
    focus.RequestFocusClassification();
    focus.PostFocusClassification();

    focus.BeginNextKey();

    EXPECT_TRUE(focus.composition.empty())
        << "drain must reset the old context before advancing the epoch for "
           "the first key in the new context";
    EXPECT_EQ(focus.appliedFocusCount, 1);
    EXPECT_EQ(focus.inputEpoch, 1u);
}

TEST_F(FocusInterleavingTest, DelayedFocusCannotConsumeOrDisableInWordBackspace) {
    FocusResetInterleavingHarness focus{mailbox_};
    focus.RequestFocusClassification();
    focus.TypeNewComposition(L"viet");
    focus.PostFocusClassification();

    focus.BackspaceNextKey();

    EXPECT_EQ(focus.composition, L"vie");
    EXPECT_EQ(focus.processedKeyCount, 1);
    EXPECT_EQ(focus.appliedFocusCount, 0);
    ASSERT_TRUE(focus.deferredFocus);
    EXPECT_EQ(focus.deferredFocus->requestSerial, 1u);
}

TEST_F(FocusInterleavingTest, LatestDelayedSnapshotReplacesDeferredSnapshot) {
    FocusResetInterleavingHarness focus{mailbox_};
    focus.RequestFocusClassification();  // request #1 at epoch 0
    focus.TypeNewComposition(L"vi");      // epoch 1 overtakes #1
    focus.PostFocusClassification();
    focus.Drain();
    ASSERT_TRUE(focus.deferredFocus);
    ASSERT_EQ(focus.deferredFocus->requestSerial, 1u);

    focus.RequestFocusClassification();  // request #2 at epoch 1
    focus.TypeNewComposition(L"viet");    // epoch 2 overtakes #2
    focus.PostFocusClassification();
    focus.Drain();

    ASSERT_TRUE(focus.deferredFocus);
    EXPECT_EQ(focus.deferredFocus->requestSerial, 2u)
        << "the latest valid transaction must replace the older deferred one";
    EXPECT_EQ(focus.composition, L"viet");
    EXPECT_EQ(focus.appliedFocusCount, 0);

    focus.composition.clear();
    focus.Drain();
    EXPECT_EQ(focus.appliedFocusCount, 1);
    EXPECT_EQ(focus.lastAppliedRequestSerial, 2u);
}

TEST_F(FocusInterleavingTest, OlderResultArrivingAfterNewerApplyIsDropped) {
    FocusResetInterleavingHarness focus{mailbox_};
    focus.RequestFocusClassification();  // capture request #1
    const auto olderSerial = focus.focusRequestSerial;
    const auto olderEpoch = focus.focusRequestedAtInputEpoch;

    focus.RequestFocusClassification();  // request #2 finishes first
    focus.PostFocusClassification();
    focus.Drain();
    ASSERT_EQ(focus.lastAppliedRequestSerial, 2u);

    FocusClassification older{};
    older.hwndOpaque = 0x1234;
    older.requestSerial = olderSerial;
    older.inputEpochAtRequest = olderEpoch;
    mailbox_.Post(
        kFocusChanged,
        std::make_shared<const FocusClassification>(std::move(older)));
    focus.Drain();

    EXPECT_EQ(focus.appliedFocusCount, 1)
        << "the late older result must not re-apply or reset state";
    EXPECT_EQ(focus.droppedFocusCount, 1);
    EXPECT_EQ(focus.lastAppliedRequestSerial, 2u);
}

}  // namespace
}  // namespace NextKey
