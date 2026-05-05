// NexusKey - MainThreadWorker
// SPDX-License-Identifier: GPL-3.0-only
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

void MainThreadWorker::Run() noexcept {
    for (;;) {
        WorkHandler handler;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopRequested_ || workPending_; });
            if (stopRequested_) return;

            workPending_ = false;
            handler = workHandler_;  // copy callable so we can run unlocked
        }

        if (handler) {
            try {
                handler();
            } catch (...) {
                // Owner-provided handlers must not crash the worker. We
                // intentionally swallow — the alternative is std::terminate
                // through a noexcept boundary.
            }
        }
    }
}

}  // namespace NextKey
