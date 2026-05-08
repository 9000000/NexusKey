// NexusKey - Heartbeat Publisher (Windows-only)
// SPDX-License-Identifier: GPL-3.0-only
//
// Publishes a 30s heartbeat to a named event so NexusKeyWatchdog.exe
// can detect process liveness. Also publishes a graceful-shutdown flag
// (named event in signaled state) so the watchdog distinguishes user-
// initiated quit from crash.
//
// Event names (Local\ session-scoped, kernel objects):
//   Local\NexusKeyHeartbeat         — pulsed every 30s
//   Local\NexusKeyGracefulShutdown  — signaled by SignalGracefulShutdown
//                                     before NexusKey exits via tray quit

#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <atomic>
#include <thread>

namespace NextKey {

inline constexpr const wchar_t* HEARTBEAT_EVENT_NAME = L"Local\\NexusKeyHeartbeat";
inline constexpr const wchar_t* GRACEFUL_SHUTDOWN_EVENT_NAME = L"Local\\NexusKeyGracefulShutdown";
inline constexpr DWORD HEARTBEAT_INTERVAL_MS = 30'000;

class HeartbeatPublisher {
public:
    HeartbeatPublisher() = default;
    ~HeartbeatPublisher();

    HeartbeatPublisher(const HeartbeatPublisher&) = delete;
    HeartbeatPublisher& operator=(const HeartbeatPublisher&) = delete;

    /// Open the named events and spawn the pulse thread. Returns false
    /// if event creation fails (caller treats as best-effort — NexusKey
    /// still functions, just no auto-respawn).
    [[nodiscard]] bool Start();

    /// Stop the pulse thread and close handles. Idempotent.
    void Stop();

    /// Set the graceful-shutdown flag — call before tray-quit exit so
    /// the watchdog does NOT respawn NexusKey.
    void SignalGracefulShutdown();

private:
    void Run() noexcept;

    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    HANDLE heartbeatEvent_ = nullptr;
    HANDLE gracefulShutdownEvent_ = nullptr;
};

}  // namespace NextKey

#endif  // _WIN32
