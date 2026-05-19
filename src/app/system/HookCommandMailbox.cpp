// VKey - Hook command mailbox impl (Phase 2a)
// SPDX-License-Identifier: GPL-3.0-only
//
// See HookCommandMailbox.h for the design contract. This .cpp is
// intentionally minimal — all the load-bearing logic is the memory-order
// choices, which are documented inline. Anything more elaborate belongs
// in the header so callers see it.

#include "app/system/HookCommandMailbox.h"

#include <cassert>

namespace NextKey {

// ────────────────────────────────────────────────────────────────────────────
// DrainScope (Phase 2d) — RAII guard, sets IsDraining() for its lifetime.
// Nested scope on the same mailbox trips a Debug assertion: if a drain
// handler is somehow re-entering DrainHookCommands, we want to crash
// immediately in dev rather than silently corrupt mailbox state.
// ────────────────────────────────────────────────────────────────────────────
HookCommandMailbox::DrainScope::DrainScope(HookCommandMailbox& mb) noexcept
    : mailbox_(mb) {
    const bool alreadyDraining =
        mailbox_.inDrain_.exchange(true, std::memory_order_acq_rel);
    (void)alreadyDraining;
    assert(!alreadyDraining &&
           "Nested DrainHookCommands — a drain handler must not call "
           "DrainHookCommands recursively. Check the call graph from the "
           "handler that ran when this assertion fired.");
}

HookCommandMailbox::DrainScope::~DrainScope() noexcept {
    mailbox_.inDrain_.store(false, std::memory_order_release);
}

void HookCommandMailbox::Post(std::uint32_t bit,
                              std::shared_ptr<const FocusClassification> cls) noexcept {
    // Publish the focus snapshot BEFORE setting its bit so a consumer
    // that observes the bit also observes a non-null pendingFocus.
    // release-store pairs with the acquire on bits.exchange in Drain
    // (transitively flushes pendingFocus through the same fence).
    if (cls) {
        pendingFocus_.store(std::move(cls), std::memory_order_release);
    }

    // fetch_or coalesces against concurrent producers — multiple bits set
    // by parallel posts collapse into a single OR'd value.
    bits_.fetch_or(bit, std::memory_order_release);

    // Fire wake exactly once per empty→non-empty transition. exchange
    // returns the old value; if it was already true, another producer
    // already fired wake and the hook thread will see our bit on the
    // same drain pass — no need to wake again.
    if (!wakePosted_.exchange(true, std::memory_order_acq_rel)) {
        if (wakeFn_) wakeFn_();
    }
}

std::uint32_t HookCommandMailbox::DrainBits() noexcept {
    // ORDERING CONTRACT (design §Critical ordering rule, see header):
    //   wakePosted_ MUST be cleared BEFORE bits_ is exchanged.
    //
    // Scenario if reversed:
    //   T0: drain runs bits.exchange(0) — empties bits.
    //   T1: producer P runs Post(kFocusChanged):
    //         - sets bits to kFocusChanged via fetch_or
    //         - reads wakePosted=true (drain hasn't cleared yet) → SKIPS wake
    //   T2: drain runs wakePosted.store(false).
    //   Result: bits has kFocusChanged, but no wake was posted; the bit
    //   stays stranded until the next unrelated keydown drains it.
    //
    // Forward order (correct):
    //   T0: drain runs wakePosted.store(false).
    //   T1: producer P runs Post(kFocusChanged):
    //         - sets bits to kFocusChanged via fetch_or
    //         - reads wakePosted=false → fires wake
    //   T2: drain runs bits.exchange(0) — drains P's bit immediately.
    //   Result: drain catches P's bit AND wake is queued for the next
    //   drain pass (idempotent — that pass will see bits=0 and return).
    wakePosted_.store(false, std::memory_order_release);
    return bits_.exchange(0, std::memory_order_acquire);
}

std::shared_ptr<const FocusClassification>
HookCommandMailbox::ConsumePendingFocus() noexcept {
    // exchange(nullptr) — second consume returns null, locks "consumed"
    // semantics for the test contract.
    return pendingFocus_.exchange(nullptr, std::memory_order_acquire);
}

}  // namespace NextKey
