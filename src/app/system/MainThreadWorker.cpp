// NexusKey - MainThreadWorker
// SPDX-License-Identifier: GPL-3.0-only
//
// See MainThreadWorker.h for the Phase C plan.

#include "app/system/MainThreadWorker.h"

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
        running_.store(true, std::memory_order_release);
    }

    thread_ = std::thread([this] { Run(); });
    return true;
}

void MainThreadWorker::Stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_acquire) && !thread_.joinable()) {
            return;
        }
        stopRequested_ = true;
    }
    cv_.notify_all();

    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
}

bool MainThreadWorker::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void MainThreadWorker::Run() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return stopRequested_; });
}

}  // namespace NextKey
