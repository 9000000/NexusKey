// HookEngineAtomicTests.cpp
//
// Sprint 1 D5: regression test for HookEngine field migration to std::atomic
// (Rule #11.3 — atomic acquire/release pattern for hook-thread-safe primitive
// reads without stateMutex_).
//
// HookEngine.cpp is Win32-only and not linked into the cross-platform NextKeyTests
// target, so this file does not import HookEngine.h directly. It instead exercises
// the std::atomic<bool> acquire/release pattern that the migration relies on,
// providing both:
//   1. compile-time enforcement that the platform supports lock-free atomic bool,
//   2. runtime check that the acquire/release pair gives the cross-thread
//      visibility property used by the hook-callback / main-thread interaction.
//
// Behavioral verification of the actual HookEngine migration is done via the
// chaos.toml + sustained.toml corpora on Windows post-build (D4 anchor:
// docs/baselines/perf-baseline-d4-spike-{chaos,sustained}.{csv,xml,md}; D5
// must show the same delta — no new regressions).

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

namespace {

// Compile-time: HookEngine::vietnameseMode_ relies on lock-free atomic bool
// for sub-microsecond hook-callback access. Platforms without lock-free
// atomic<bool> would silently fall back to mutex-internal — defeating the
// Rule #11 hook-budget constraint.
static_assert(std::atomic<bool>::is_always_lock_free,
              "Sprint 1 D5: HookEngine::vietnameseMode_ requires lock-free atomic<bool>");

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

}  // namespace
