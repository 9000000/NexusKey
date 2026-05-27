// VKey - MainThreadWorker
// SPDX-License-Identifier: AGPL-3.0-only
//
// See MainThreadWorker.h for the Phase C plan.

#include "app/system/MainThreadWorker.h"

#include <utility>

namespace NextKey {

MainThreadWorker::~MainThreadWorker() {
    Stop();
}

bool MainThreadWorker::Start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_.load(std::memory_order_acquire)) {
            return false;
        }
        stopRequested_ = false;
        // Note: workPending_ is intentionally NOT cleared here — a Signal
        // received before Start latches and dispatches on first wake.
        running_.store(true, std::memory_order_release);
    }

    thread_ = std::thread([this] { Run(); });
    return true;
}

void MainThreadWorker::Stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_acquire) && !thread_.joinable()) {
            // Stop without ever Start — drop any latched pre-Start signal.
            workPending_ = false;
            return;
        }
        stopRequested_ = true;
    }
    cv_.notify_all();

    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);

    // Clear any signal that arrived after the worker drained its last
    // dispatch — Signal()-after-Stop must not run the handler later.
    std::lock_guard<std::mutex> lock(mutex_);
    workPending_ = false;
}

bool MainThreadWorker::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void MainThreadWorker::SetWorkHandler(WorkHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    workHandler_ = std::move(handler);
}

void MainThreadWorker::Signal() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        workPending_ = true;
    }
    cv_.notify_all();
}

void MainThreadWorker::SetTickHandler(WorkHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    tickHandler_ = std::move(handler);
}

void MainThreadWorker::SetTickInterval(std::chrono::milliseconds interval) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tickInterval_ = interval;
    }
    // Wake any in-flight wait so the new interval is picked up at the
    // next loop iteration. Without this, lengthening or shortening the
    // interval while the worker is mid-wait would leave the old timeout
    // running until it expires.
    cv_.notify_all();
}

void MainThreadWorker::Run() noexcept {
    auto invoke = [](const WorkHandler& h) noexcept {
        if (!h) return;
        try {
            h();
        } catch (...) {
            // Owner-provided handlers must not crash the worker. We
            // intentionally swallow — the alternative is std::terminate
            // through a noexcept boundary.
        }
    };

    for (;;) {
        WorkHandler workCopy;
        WorkHandler tickCopy;
        bool runWork = false;
        bool runTick = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto interval = tickInterval_;
            const auto predicate = [this] { return stopRequested_ || workPending_; };

            if (interval.count() > 0) {
                // Timeout-bounded wait: returns false on timeout, true if
                // predicate became true before the timeout. Distinguishes
                // tick (timeout) from work-signal / stop (predicate).
                const bool predicateMet = cv_.wait_for(lock, interval, predicate);
                if (stopRequested_) return;
                if (predicateMet) {
                    workPending_ = false;
                    runWork = true;
                    workCopy = workHandler_;
                } else {
                    runTick = true;
                    tickCopy = tickHandler_;
                }
            } else {
                cv_.wait(lock, predicate);
                if (stopRequested_) return;
                workPending_ = false;
                runWork = true;
                workCopy = workHandler_;
            }
        }

        if (runWork) invoke(workCopy);
        if (runTick) invoke(tickCopy);
    }
}

}  // namespace NextKey
