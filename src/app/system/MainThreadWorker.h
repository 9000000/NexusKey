// NexusKey - MainThreadWorker
// SPDX-License-Identifier: GPL-3.0-only
//
// Sprint 1 Phase C — single home for non-hot-path work that previously
// executed on the hook thread (config reload, focus poll, heartbeat,
// CJK detection). Moves these off the LL hook callback chain so Rule #11
// is honoured at the call-graph level (D7 audit only sees direct entry
// bodies; ProcessKeyDown -> QuickSyncFromSharedState transitively
// acquires stateMutex_, which Phase C eliminates).
//
// Phase C lands in three pieces:
//   D8 — scaffolding (this file): empty thread that idles until Stop.
//   D9 — wire ConfigEvent so config reload happens here, not on hook.
//   D10 — wire heartbeat + CJK layout poll, retire the SetTimer(200 ms).

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace NextKey {

class MainThreadWorker {
public:
    MainThreadWorker() = default;
    ~MainThreadWorker();

    MainThreadWorker(const MainThreadWorker&) = delete;
    MainThreadWorker& operator=(const MainThreadWorker&) = delete;
    MainThreadWorker(MainThreadWorker&&) = delete;
    MainThreadWorker& operator=(MainThreadWorker&&) = delete;

    /// Launch the worker thread. Returns false if already running
    /// (idempotent — second call while running is a no-op).
    bool Start();

    /// Signal shutdown and join the worker thread. Idempotent: safe
    /// before Start, after Stop, and from the destructor. Returns once
    /// the worker thread has finished (within the wakeup latency of the
    /// wait primitive; in D8 that is "immediate" because the cv is
    /// notified directly).
    void Stop() noexcept;

    /// True between Start() returning true and Stop() (or destruction)
    /// completing.
    [[nodiscard]] bool IsRunning() const noexcept;

private:
    void Run() noexcept;

    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopRequested_ = false;
    std::atomic<bool> running_{false};
};

}  // namespace NextKey
