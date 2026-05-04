// HookEngineAtomicTests.cpp
//
// Sprint 1 D5 + D5.1 + D5.2: regression test for HookEngine field migration to
// std::atomic (Rule #11.3 — atomic acquire/release pattern for hook-thread-safe
// primitive reads without stateMutex_).
//
// Fields under test (mirrored as freestanding atomics here, since HookEngine.cpp
// is Win32-only and not linked into the cross-platform NextKeyTests target):
//   - vietnameseMode_   — std::atomic<bool>     (D5)
//   - isTsfApp_         — std::atomic<bool>     (D5.1)
//   - currentMethod_    — std::atomic<InputMethod>  (D5.1, enum)
//   - 14 per-app + config bools (D5.2): isExcludedApp_, isConsoleApp_,
//     isElectronApp_, skipEmptyChar_, needBaitChar_, useClipboardPaste_,
//     useEditMsgPath_, isOutlookApp_, macroEnabled_, macroInEnglish_,
//     autoCaps_, autoCapsMacro_, tempOffMacroByEsc_, tempOffByAlt_
//   - excludedPid_      — std::atomic<DWORD>    (D5.2, 32-bit PID)
//
// This file provides:
//   1. compile-time enforcement that the platform supports lock-free atomic
//      access for both bool and enum-class (sub-microsecond hook-callback
//      access requires lock-free; otherwise Rule #11's hook-budget is violated),
//   2. runtime check that the acquire/release pair gives the cross-thread
//      visibility property used by the hook-callback / main-thread interaction,
//   3. enum-specific check that std::atomic<InputMethod> stores and loads
//      the value byte-for-byte.
//
// Behavioral verification of the actual HookEngine migration is done via the
// chaos.toml + sustained.toml corpora on Windows post-build (D4 anchor:
// docs/baselines/perf-baseline-d4-spike-{chaos,sustained}.{csv,xml,md}; D5
// and D5.1 must show the same delta — no new regressions).

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

// Mirror of NextKey::InputMethod (see core/config/TypingConfig.h). We don't
// include the real header here because TypingConfig pulls in Windows-only
// dependencies on the production tree. The values are stable enum members
// that have been part of the public config since Phase 0.
enum class InputMethod : uint8_t { Telex = 0, VNI = 1, Combined = 2 };

namespace {

// Compile-time: HookEngine::vietnameseMode_ + isTsfApp_ rely on lock-free
// atomic<bool>; currentMethod_ relies on lock-free atomic<enum>. Platforms
// without lock-free support would silently fall back to mutex-internal —
// defeating the Rule #11 hook-budget constraint.
static_assert(std::atomic<bool>::is_always_lock_free,
              "Sprint 1 D5: HookEngine::vietnameseMode_ / isTsfApp_ require lock-free atomic<bool>");
static_assert(std::atomic<InputMethod>::is_always_lock_free,
              "Sprint 1 D5.1: HookEngine::currentMethod_ requires lock-free atomic<InputMethod>");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "Sprint 1 D5.2: HookEngine::excludedPid_ (DWORD == uint32_t) requires lock-free atomic");

// Acquire/release visibility — main-thread store is observed by hook-thread
// load. Pattern matches HookEngine::ToggleVietnameseMode (writer) and
// HookEngine::ProcessKeyDown step 2c (reader).
TEST(HookEngineAtomic, AcquireReleaseVisibility) {
    constexpr int kIterations = 1000;
    for (int i = 0; i < kIterations; ++i) {
        std::atomic<bool> field{false};
        std::atomic<bool> handshake{false};

        std::thread reader([&]() {
            // Reader spins until handshake is observed via acquire-load.
            while (!handshake.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            // Per acquire/release ordering, all release-stores prior to the
            // handshake's release-store must be visible after this acquire-load.
            EXPECT_TRUE(field.load(std::memory_order_acquire))
                << "main-thread release-store of field must be visible "
                   "to hook-thread after acquire-load of handshake";
        });

        field.store(true, std::memory_order_release);
        handshake.store(true, std::memory_order_release);

        reader.join();
    }
}

// Toggle pattern — load-modify-store from a single writer thread is race-free
// for the toggle itself (no other writer competes), but readers must observe
// either the pre- or post-toggle value, never a torn intermediate.
//
// This matches HookEngine::ToggleVietnameseMode after D5 migration:
//   const bool newMode = !vietnameseMode_.load(acquire);
//   vietnameseMode_.store(newMode, release);
TEST(HookEngineAtomic, SingleWriterToggleSemantics) {
    std::atomic<bool> field{false};

    auto toggle = [&]() {
        const bool cur = field.load(std::memory_order_acquire);
        field.store(!cur, std::memory_order_release);
    };

    EXPECT_FALSE(field.load(std::memory_order_acquire));
    toggle();
    EXPECT_TRUE(field.load(std::memory_order_acquire));
    toggle();
    EXPECT_FALSE(field.load(std::memory_order_acquire));
}

// D5.1: enum-class round-trip via atomic store/load. Mirrors
// HookEngine::currentMethod_'s ApplyConfig (writer) → ProcessKeyDown VNI/Combined
// gate (reader) interaction. Each value the production code can store must
// load back byte-identical.
TEST(HookEngineAtomic, EnumStoreLoadRoundTrip) {
    std::atomic<InputMethod> field{InputMethod::Telex};

    EXPECT_EQ(field.load(std::memory_order_acquire), InputMethod::Telex);

    field.store(InputMethod::VNI, std::memory_order_release);
    EXPECT_EQ(field.load(std::memory_order_acquire), InputMethod::VNI);

    field.store(InputMethod::Combined, std::memory_order_release);
    EXPECT_EQ(field.load(std::memory_order_acquire), InputMethod::Combined);

    field.store(InputMethod::Telex, std::memory_order_release);
    EXPECT_EQ(field.load(std::memory_order_acquire), InputMethod::Telex);
}

// D5.2: snapshot-then-publish idiom used by HookEngine::OnFocusChanged.
// Multiple atomic stores must each be independently visible after their
// release-store; readers using acquire-loads see the new value of each field
// without torn-tuple ordering between fields.
//
// HookEngine pattern: OnFocusChanged classifies the foreground app into
// 7 booleans (isConsoleApp_, skipEmptyChar_, needBaitChar_, ...) using stack
// locals, then issues 7 sequential .store(release). Readers in the hook hot
// path .load(acquire) each independently — there is no atomicity requirement
// across the 7 fields (any reader interleaving is acceptable; the worst case
// is "previous app for some flags, new app for others" which is recoverable).
//
// This test verifies that each individual store/load pair sees the published
// value, even when 7 stores happen in tight succession — i.e. no compiler
// reordering or store buffer collapsing breaks the per-field acquire/release
// guarantee.
TEST(HookEngineAtomic, MultiPublishVisibility) {
    constexpr int kIterations = 500;
    for (int iter = 0; iter < kIterations; ++iter) {
        std::atomic<bool> a{false}, b{false}, c{false}, d{false},
                          e{false}, f{false}, g{false};
        std::atomic<bool> done{false};

        std::thread reader([&]() {
            // Wait for handshake, then verify each field independently.
            while (!done.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            EXPECT_TRUE(a.load(std::memory_order_acquire));
            EXPECT_TRUE(b.load(std::memory_order_acquire));
            EXPECT_TRUE(c.load(std::memory_order_acquire));
            EXPECT_TRUE(d.load(std::memory_order_acquire));
            EXPECT_TRUE(e.load(std::memory_order_acquire));
            EXPECT_TRUE(f.load(std::memory_order_acquire));
            EXPECT_TRUE(g.load(std::memory_order_acquire));
        });

        // Publish all 7 fields, then handshake. Done's release-store must
        // make all prior release-stores visible after the reader's
        // acquire-load on done.
        a.store(true, std::memory_order_release);
        b.store(true, std::memory_order_release);
        c.store(true, std::memory_order_release);
        d.store(true, std::memory_order_release);
        e.store(true, std::memory_order_release);
        f.store(true, std::memory_order_release);
        g.store(true, std::memory_order_release);
        done.store(true, std::memory_order_release);

        reader.join();
    }
}

// D5.2: std::atomic<DWORD> (== std::atomic<uint32_t>) round-trip + cross-thread
// visibility. Mirrors HookEngine::excludedPid_ — main thread writes the PID
// from OnFocusChanged when entering an excluded app; hook thread reads in
// ProcessKeyDown's fast PID equality check.
TEST(HookEngineAtomic, PidStoreLoadRoundTrip) {
    std::atomic<uint32_t> pid{0};

    EXPECT_EQ(pid.load(std::memory_order_acquire), 0u);

    pid.store(4096, std::memory_order_release);
    EXPECT_EQ(pid.load(std::memory_order_acquire), 4096u);

    pid.store(0xDEADBEEF, std::memory_order_release);
    EXPECT_EQ(pid.load(std::memory_order_acquire), 0xDEADBEEFu);

    pid.store(0, std::memory_order_release);
    EXPECT_EQ(pid.load(std::memory_order_acquire), 0u);
}

// D5.1: cross-thread visibility for std::atomic<enum>. Producer cycles through
// every enum value with release-stores; consumer observes each via acquire-load
// and checks the value falls in the legal enum set (i.e. no torn read producing
// a numeric value outside the enumerated members).
TEST(HookEngineAtomic, EnumCrossThreadVisibility) {
    constexpr int kIterations = 2000;
    std::atomic<InputMethod> field{InputMethod::Telex};
    std::atomic<bool> stop{false};

    std::thread reader([&]() {
        while (!stop.load(std::memory_order_acquire)) {
            const InputMethod observed = field.load(std::memory_order_acquire);
            // Any value observed must be one of the three legal enumerators.
            EXPECT_TRUE(observed == InputMethod::Telex ||
                        observed == InputMethod::VNI ||
                        observed == InputMethod::Combined)
                << "atomic<InputMethod> produced torn / out-of-range read: "
                << static_cast<int>(observed);
        }
    });

    for (int i = 0; i < kIterations; ++i) {
        field.store(InputMethod::Telex,    std::memory_order_release);
        field.store(InputMethod::VNI,      std::memory_order_release);
        field.store(InputMethod::Combined, std::memory_order_release);
    }
    stop.store(true, std::memory_order_release);
    reader.join();
}

}  // namespace
