// VKey - Runtime-gated debug logger
// SPDX-License-Identifier: GPL-3.0-only

#include "core/Logger.h"

#include <clocale>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>
#endif

namespace NextKey {

std::atomic<bool> Logger::enabled_{false};

namespace {

std::mutex& Mutex() {
    static std::mutex m;
    return m;
}

// Lazy state behind Mutex().
std::wstring& InstallDir() { static std::wstring s; return s; }
std::wstring& PathOverride() { static std::wstring s; return s; }
std::wstring& ResolvedPath() { static std::wstring s; return s; }
std::FILE*& File() { static std::FILE* f = nullptr; return f; }

#ifdef _WIN32

bool DirectoryWritable(const std::wstring& dir) {
    if (dir.empty()) return false;
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
    // Probe with a temp file (CREATE_ALWAYS + FILE_FLAG_DELETE_ON_CLOSE).
    wchar_t probe[MAX_PATH] = {0};
    _snwprintf_s(probe, MAX_PATH, _TRUNCATE, L"%ls\\.vkey_probe_%lu",
                 dir.c_str(), GetCurrentProcessId());
    HANDLE h = CreateFileW(probe, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, CREATE_ALWAYS,
                           FILE_FLAG_DELETE_ON_CLOSE | FILE_ATTRIBUTE_TEMPORARY,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

std::wstring AppDataLogFolder() {
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) {
        std::wstring dir = std::wstring(buf) + L"\\VKey\\logs";
        // Ensure folder exists. SHCreateDirectoryExW creates intermediate dirs.
        int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        if (r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS) {
            return dir;
        }
    }
    return L"";
}

std::wstring ProcessTag() {
    wchar_t path[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0) return L"unknown";
    std::wstring s(path);
    size_t slash = s.find_last_of(L"\\/");
    std::wstring base = (slash == std::wstring::npos) ? s : s.substr(slash + 1);
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base.resize(dot);
    return base;
}

std::wstring DefaultInstallDir() {
    // Caller-provided install dir wins (DLL sets this from its own module).
    if (!InstallDir().empty()) return InstallDir();
    // Else: directory of the host process exe.
    wchar_t path[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return L"";
    std::wstring s(path);
    size_t slash = s.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"" : s.substr(0, slash);
}

std::wstring ResolveFolderUnlocked() {
    if (!PathOverride().empty()) {
        std::wstring p = PathOverride();
        size_t slash = p.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    }
    std::wstring install = DefaultInstallDir();
    if (DirectoryWritable(install)) return install;
    return AppDataLogFolder();
}

std::wstring ResolvePathUnlocked() {
    if (!PathOverride().empty()) return PathOverride();
    std::wstring folder = ResolveFolderUnlocked();
    if (folder.empty()) return L"";
    wchar_t pidBuf[16] = {0};
    _snwprintf_s(pidBuf, 16, _TRUNCATE, L"%lu", GetCurrentProcessId());
    return folder + L"\\VKey_" + ProcessTag() + L"_" + pidBuf + L".log";
}

void OpenFileUnlocked() {
    if (File()) return;
    std::wstring path = ResolvePathUnlocked();
    if (path.empty()) return;
    ResolvedPath() = path;
    // Append mode + UTF-8 transcoding. Per-PID filename (see Logger.h) means
    // exactly one process writes to each file → append-mode races don't apply.
    // We still fflush after each line so a crash doesn't lose the tail of the
    // session — issue-#108 bug reports need the last lines before failure.
    (void)_wfopen_s(&File(), path.c_str(), L"a, ccs=UTF-8");
    if (File()) setvbuf(File(), nullptr, _IOLBF, 4096);
}

#else  // POSIX (tests only)

bool DirectoryWritable(const std::wstring& /*dir*/) { return false; }
std::wstring AppDataLogFolder() { return L""; }
std::wstring DefaultInstallDir() { return L""; }
std::wstring ProcessTag() { return L"test"; }

std::string ToNarrow(const std::wstring& w) {
    std::string s; s.reserve(w.size());
    for (wchar_t c : w) s.push_back(static_cast<char>(c & 0x7F));
    return s;
}

std::wstring ResolveFolderUnlocked() {
    if (!PathOverride().empty()) {
        std::wstring p = PathOverride();
        size_t slash = p.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    }
    return L"/tmp/vkey-logs";
}

std::wstring ResolvePathUnlocked() {
    if (!PathOverride().empty()) return PathOverride();
    return ResolveFolderUnlocked() + L"/vkey.log";
}

void OpenFileUnlocked() {
    if (File()) return;
    std::wstring path = ResolvePathUnlocked();
    if (path.empty()) return;
    // Ensure parent dir exists (mkdir -p for /tmp/vkey-logs).
    std::wstring folder = ResolveFolderUnlocked();
    if (!folder.empty()) {
        std::string fnarrow = ToNarrow(folder);
        mkdir(fnarrow.c_str(), 0755);
    }
    ResolvedPath() = path;
    std::string narrow = ToNarrow(path);
    // Default "C" locale can't transcode wide → multibyte. Switch once so
    // fwprintf(L"...") works. Only matters in test/Linux builds.
    static bool localeSet = false;
    if (!localeSet) {
        if (std::setlocale(LC_ALL, "C.UTF-8") == nullptr) {
            std::setlocale(LC_ALL, "en_US.UTF-8");
        }
        localeSet = true;
    }
    File() = std::fopen(narrow.c_str(), "a");
    if (File()) {
        // Orient stream to wide before any non-wide write happens.
        fwide(File(), 1);
        setvbuf(File(), nullptr, _IOLBF, 4096);
    }
}

#endif

void CloseFileUnlocked() {
    if (File()) {
        std::fflush(File());
        std::fclose(File());
        File() = nullptr;
    }
}

void WriteLineUnlocked(const wchar_t* fmt, va_list args) {
    if (!File()) OpenFileUnlocked();
    if (!File()) return;

    // Format the whole line into a stack buffer first — single point of
    // truth for both the file sink and (in _DEBUG) the OutputDebugStringW
    // sink. Doing the formatting twice would double-evaluate variadic args
    // that have side effects.
    wchar_t line[1024];
    int prefixLen = 0;
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    prefixLen = _snwprintf_s(line, 1024, _TRUNCATE,
                             L"[%02u:%02u:%02u.%03u] [PID:%lu] ",
                             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                             GetCurrentProcessId());
#else
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&now, &tmv);
    prefixLen = swprintf(line, 1024,
                         L"[%02d:%02d:%02d] [PID:%d] ",
                         tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                         static_cast<int>(getpid()));
#endif
    if (prefixLen < 0) prefixLen = 0;
    if (prefixLen >= 1024) prefixLen = 1023;

    int bodyLen = 0;
    const int remaining = 1024 - prefixLen - 2;  // leave room for "\n\0"
    if (remaining > 0) {
#ifdef _WIN32
        bodyLen = _vsnwprintf_s(line + prefixLen, remaining + 2, _TRUNCATE,
                                fmt, args);
#else
        bodyLen = vswprintf(line + prefixLen, remaining + 2, fmt, args);
#endif
    }
    if (bodyLen < 0) bodyLen = remaining;  // truncated — fill to room
    int totalLen = prefixLen + bodyLen;
    if (totalLen > 1022) totalLen = 1022;
    line[totalLen]     = L'\n';
    line[totalLen + 1] = L'\0';

    fputws(line, File());
    // Per-line flush. Crash before next flush loses at most one line — issue
    // #108-style bug reports need the last lines, so the cost is worth it.
    std::fflush(File());

#if defined(_DEBUG) || defined(NEXTKEY_DEBUG)
#ifdef _WIN32
    OutputDebugStringW(line);
#endif
#endif
}

}  // namespace

void Logger::SetEnabled(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(Mutex());
    bool wasEnabled = enabled_.load(std::memory_order_acquire);
    if (wasEnabled == enabled) return;
    enabled_.store(enabled, std::memory_order_release);
    if (!enabled) {
        CloseFileUnlocked();
    }
    // On enable: defer file open until first Log() (lazy).
}

void Logger::SetInstallDir(const std::wstring& dir) noexcept {
    std::lock_guard<std::mutex> lock(Mutex());
    if (InstallDir() == dir) return;
    InstallDir() = dir;
    // Always close + clear so the next Log() re-resolves at the new path.
    // Previous "skip if enabled" branch left the open handle pointing at the
    // host-process directory when the DLL was loaded with the toggle on.
    CloseFileUnlocked();
    ResolvedPath().clear();
}

void Logger::SetLogPathForTesting(const std::wstring& path) noexcept {
    std::lock_guard<std::mutex> lock(Mutex());
    PathOverride() = path;
    CloseFileUnlocked();
    ResolvedPath().clear();
}

std::wstring Logger::GetCurrentLogPath() noexcept {
    std::lock_guard<std::mutex> lock(Mutex());
    if (!ResolvedPath().empty()) return ResolvedPath();
    return ResolvePathUnlocked();
}

std::wstring Logger::GetCurrentLogFolder() noexcept {
    std::lock_guard<std::mutex> lock(Mutex());
    return ResolveFolderUnlocked();
}

void Logger::Log(const wchar_t* fmt, ...) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) return;
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
}

void Logger::LogV(const wchar_t* fmt, va_list args) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(Mutex());
    if (!enabled_.load(std::memory_order_acquire)) return;  // re-check after lock
    WriteLineUnlocked(fmt, args);
}

void Logger::Shutdown() noexcept {
    std::lock_guard<std::mutex> lock(Mutex());
    CloseFileUnlocked();
}

}  // namespace NextKey
