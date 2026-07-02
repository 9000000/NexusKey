// VKey - Always-on crash exception logger
// SPDX-License-Identifier: AGPL-3.0-only
//
// Appends a single-line record to `<exe_dir>\_vkey_crash.log` when an
// exception reaches a top-level callback / thread entry. Unlike NEXTKEY_LOG
// (debug-only), CrashLog is compiled in for Release builds — it's the only
// breadcrumb left when the catch handler swallows a throw.

#pragma once

// Global-scope fwd-decl (matches the Windows.h ::_EXCEPTION_POINTERS) so the
// header need not pull in Windows.h for the Linux test build.
struct _EXCEPTION_POINTERS;

namespace NextKey {

/// Append `[timestamp] context: what` to the crash log. Safe to call from
/// any thread; never throws.
void CrashLog(const wchar_t* context, const char* what) noexcept;

#ifdef _WIN32
/// Write a MiniDumpNormal of the current process next to the crash log
/// (`<exe_dir>\<tag>_<pid>_<time>.dmp`). Call from an unhandled-exception filter
/// (process is about to die) so an otherwise-invisible fatal crash leaves a
/// stack. `ep` may be null. Best-effort; never throws.
void WriteCrashDump(::_EXCEPTION_POINTERS* ep, const wchar_t* tag) noexcept;
#endif

}  // namespace NextKey
