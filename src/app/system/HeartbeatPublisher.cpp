// NexusKey - Heartbeat Publisher Implementation (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only

#include "HeartbeatPublisher.h"

#ifdef _WIN32

#include "core/Debug.h"

namespace NextKey {

HeartbeatPublisher::~HeartbeatPublisher() {
    Stop();
}

bool HeartbeatPublisher::Start() {
    if (heartbeatEvent_) return true;  // Idempotent

    // Manual-reset event — we pulse it via SetEvent + ResetEvent.
    heartbeatEvent_ = CreateEventW(nullptr, TRUE, FALSE, HEARTBEAT_EVENT_NAME);
    if (!heartbeatEvent_) {
        NEXTKEY_LOG(L"HeartbeatPublisher: CreateEvent heartbeat FAILED err=%lu",
                    GetLastError());
        return false;
    }

    gracefulShutdownEvent_ = CreateEventW(nullptr, TRUE, FALSE, GRACEFUL_SHUTDOWN_EVENT_NAME);
    if (!gracefulShutdownEvent_) {
        NEXTKEY_LOG(L"HeartbeatPublisher: CreateEvent graceful FAILED err=%lu",
                    GetLastError());
        CloseHandle(heartbeatEvent_);
        heartbeatEvent_ = nullptr;
        return false;
    }

    stopRequested_.store(false, std::memory_order_relaxed);
    thread_ = std::thread([this]() { Run(); });
    return true;
}

void HeartbeatPublisher::Stop() {
    if (!heartbeatEvent_ && !gracefulShutdownEvent_) return;

    stopRequested_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();

    if (heartbeatEvent_) {
        CloseHandle(heartbeatEvent_);
        heartbeatEvent_ = nullptr;
    }
    if (gracefulShutdownEvent_) {
        CloseHandle(gracefulShutdownEvent_);
        gracefulShutdownEvent_ = nullptr;
    }
}

void HeartbeatPublisher::SignalGracefulShutdown() {
    if (gracefulShutdownEvent_) {
        SetEvent(gracefulShutdownEvent_);
    }
}

void HeartbeatPublisher::Run() noexcept {
    // Pulse loop: SetEvent + ResetEvent + sleep. We use Sleep (not
    // condition_variable) because the cost of tearing down on shutdown
    // is at most one HEARTBEAT_INTERVAL_MS wait — acceptable for a 30s
    // interval. Stop() join blocks for ≤30s in worst case.
    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (heartbeatEvent_) {
            SetEvent(heartbeatEvent_);
            ResetEvent(heartbeatEvent_);  // Pulse — watchdog Wait sees signaled then auto-resets-effective
        }
        // Granular sleep so Stop() returns within ~100ms instead of 30s.
        for (DWORD waited = 0; waited < HEARTBEAT_INTERVAL_MS; waited += 100) {
            if (stopRequested_.load(std::memory_order_acquire)) return;
            Sleep(100);
        }
    }
}

}  // namespace NextKey

#endif  // _WIN32
