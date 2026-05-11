// NexusKey - Runtime-gated debug logger
// SPDX-License-Identifier: GPL-3.0-only
//
// Single backend for NEXTKEY_LOG / HOOK_LOG / TSF_LOG. Compiled unconditionally
// in every build; runtime-gated by an atomic. When disabled, Log() returns
// after an acquire atomic load — fast enough for cold paths and acceptable
// for the hot path because most call sites are off the keystroke critical
// line. In _DEBUG / NEXTKEY_DEBUG builds the same formatted line is fanned
// out to OutputDebugStringW so DebugView traces keep working.
//
// File path resolution (Win32):
//   1. <install dir>\NexusKey_<process>_<pid>.log if writable
//   2. %APPDATA%\NexusKey\logs\NexusKey_<process>_<pid>.log otherwise
//
// PID is in the filename so concurrent processes (EXE + every TSF DLL host —
// Chrome renderers, Electron helpers) get their own file. Append-mode + a
// per-line fflush still applies inside one process; PID prevents cross-process
// line tearing on NTFS where C runtime `fopen("a")` isn't atomic.
//
// Linux: only used by tests via SetLogPathForTesting().

#pragma once

#include <atomic>
#include <cstdarg>
#include <string>

namespace NextKey {

class Logger {
public:
    static void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] static bool IsEnabled() noexcept {
        return enabled_.load(std::memory_order_acquire);
    }

    /// EXE/DLL init reports the install directory (parent of NextKeyApp.exe /
    /// NextKeyTSF.dll). Logger probes writability, falls back to APPDATA.
    /// If logging is currently enabled, any open file is closed and reopened
    /// at the new path on the next Log() — calling this from DllMain after
    /// EngineController already enabled the logger still routes future writes
    /// to the install dir.
    static void SetInstallDir(const std::wstring& dir) noexcept;

    /// Resolved file path for the current process. Computed lazily on first
    /// Log() while enabled; empty until then.
    [[nodiscard]] static std::wstring GetCurrentLogPath() noexcept;

    /// Folder containing the current process's log file (for the "open folder"
    /// button in the System tab). Resolves the same way as GetCurrentLogPath
    /// but without requiring the file to exist yet.
    [[nodiscard]] static std::wstring GetCurrentLogFolder() noexcept;

    static void Log(const wchar_t* fmt, ...) noexcept;

    /// Tests inject an exact path here. Empty string clears.
    static void SetLogPathForTesting(const std::wstring& path) noexcept;

    /// Flush + close. Idempotent. Called from app shutdown.
    static void Shutdown() noexcept;

private:
    static void LogV(const wchar_t* fmt, va_list args) noexcept;
    static std::atomic<bool> enabled_;
};

}  // namespace NextKey
