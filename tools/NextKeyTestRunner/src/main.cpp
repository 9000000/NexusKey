// NextKeyTestRunner — E2E stress test harness for NexusKey IME.
// Drives SendInput against a running NexusKey hook, verifies clipboard output,
// and emits per-keystroke timing for L1 (NexusKey internal) and L2 (end-to-end).
//
// Phase 0a status: D4 -- clipboard verify wired. Single-test mode only;
// multi-test loop with TOML corpus comes in D7+.
// See _bmad-output/brainstorming/brainstorming-session-2026-05-03-1201.md
// (Phase 7.3 day-by-day plan) for the full sprint definition of done.
//
// Windows-only. Entry is wmain(): we need wide argv so that Vietnamese
// values like --expected "việt" survive without being squashed to '?' by
// the ANSI codepage conversion that narrow `main(int, char**)` does.

// clang-format off
#include <Windows.h>
// clang-format on

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "CaseResult.h"
#include "ClipboardReader.h"
#include "Encoding.h"
#include "HookLogParser.h"
#include "JunitXmlWriter.h"
#include "PerfCsvWriter.h"
#include "SendInputDriver.h"
#include "Telex.h"
#include "TomlLoader.h"

namespace NextKey::TestRunner {

constexpr const char* kVersion = "0.6.0-d9-reporter";

// GetLocalTime-derived ms-since-midnight (matches NexusKey HookLog timestamp
// format, so we can slice log entries by per-case windows).
[[nodiscard]] uint64_t LocalTimeMs() noexcept {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return static_cast<uint64_t>(st.wHour)   * 3'600'000ULL +
           static_cast<uint64_t>(st.wMinute) *    60'000ULL +
           static_cast<uint64_t>(st.wSecond) *     1'000ULL +
           static_cast<uint64_t>(st.wMilliseconds);
}

struct CaseWindow {
    uint64_t startMs;  // ms-since-midnight just before SendString
    uint64_t endMs;    // ms-since-midnight after Ctrl+A+C + clipboard settle
};

// Shared constants for the send/verify flow (used by both --send and --corpus).
constexpr uint16_t kVkA       = 0x41;   // 'A' for Ctrl+A
constexpr uint16_t kVkC       = 0x43;   // 'C' for Ctrl+C
constexpr uint16_t kVkDelete  = 0x2E;   // VK_DELETE

constexpr uint32_t kClearStepDelayMs   = 30;   // between Ctrl+A and Delete
constexpr uint32_t kCtrlACtrlCDelayMs  = 50;   // between Ctrl+A and Ctrl+C
constexpr uint32_t kClipboardSettleMs  = 150;  // after Ctrl+C, before reading

void PrintUsage() {
    std::printf("NextKeyTestRunner v%s\n", kVersion);
    std::printf("E2E stress test harness for NexusKey IME (Windows-only)\n\n");
    std::printf("Usage:\n");
    std::printf("  NextKeyTestRunner.exe                    Print this help\n");
    std::printf("  NextKeyTestRunner.exe --send TEXT [opts] Drive SendInput\n\n");
    std::printf("Send options:\n");
    std::printf("  --send TEXT          Vietnamese or Telex text to type\n");
    std::printf("  --inter-key-us=N     Inter-key delay in microseconds (default 10000)\n");
    std::printf("  --delay-ms=N         Initial focus delay (default 3000)\n");
    std::printf("  --raw                Skip Telex conversion; type TEXT verbatim\n\n");
    std::printf("Verify options (auto-enables --clear-first):\n");
    std::printf("  --verify             After send, Ctrl+A+C and read clipboard\n");
    std::printf("  --expected TEXT      Expected clipboard contents (compared to actual)\n");
    std::printf("  --clear-first        Send Ctrl+A + Delete before typing\n");
    std::printf("  --post-send-ms=N     Wait after send before Ctrl+A (default 200)\n\n");
    std::printf("Corpus mode:\n");
    std::printf("  --list FILE.toml     Parse corpus file and print loaded cases\n");
    std::printf("                       (no SendInput driving; debug helper)\n");
    std::printf("  --corpus FILE.toml   Run all tests in FILE.toml; PASS/FAIL summary\n");
    std::printf("                       (auto-enables clear-first per case)\n");
    std::printf("  --hook-log PATH      Path to NexusKey_hook.log (debug build) -- when\n");
    std::printf("                       supplied, --corpus prompts after the run for the\n");
    std::printf("                       user to stop NexusKey, then post-mortem parses the\n");
    std::printf("                       log to print L1 inter-key timing per case.\n");
    std::printf("  --junit PATH         Write JUnit-style XML report to PATH\n");
    std::printf("  --perf-csv PATH      Write per-case perf metrics as CSV to PATH\n\n");
    std::printf("  --help, -h           Show this help\n\n");
    std::printf("Examples:\n");
    std::printf("  NextKeyTestRunner.exe --send vieejt --raw\n");
    std::printf("    Types v-i-e-e-j-t into the focused window. Visual smoke test.\n\n");
    std::printf("  NextKeyTestRunner.exe --send vieejt --raw --verify --expected viet-with-tone\n");
    std::printf("    Types vieejt, selects all, copies, compares clipboard to expected.\n");
    std::printf("    Exits 0 on PASS, 1 on FAIL with a diff. Vietnamese in --expected\n");
    std::printf("    requires UTF-8 capable shell (PowerShell 7 / Windows Terminal /\n");
    std::printf("    cmd after 'chcp 65001').\n");
}

// wchar_t and char16_t are both 16-bit on Windows -- layout-compatible.
std::u16string WideToU16(const wchar_t* w) {
    if (!w) return {};
    return std::u16string(reinterpret_cast<const char16_t*>(w));
}

void PrintTelexDiagnostic(std::u16string_view telex) {
    std::printf("Will send %zu chars: ", telex.size());
    for (char16_t ch : telex) {
        if (ch >= 0x20 && ch < 0x7F) {
            std::putchar(static_cast<char>(ch));
        } else {
            std::printf("[U+%04X]", static_cast<unsigned>(ch));
        }
    }
    std::printf("\n");
}

struct RunOptions {
    std::u16string sendText;
    std::u16string expected;
    std::u16string listFile;        // --list FILE.toml: print parsed cases, no driving
    std::u16string corpusFile;      // --corpus FILE.toml: drive every case + verify
    std::u16string hookLogPath;     // --hook-log: NexusKey_hook.log for L1 timing
    std::u16string junitXmlPath;    // --junit: JUnit XML report output
    std::u16string perfCsvPath;     // --perf-csv: per-case CSV output
    bool raw = false;
    bool verify = false;
    bool clearFirst = false;
    bool hasExpected = false;
    uint32_t interKeyMicros = 10'000;
    uint32_t initialDelayMs = 3'000;
    uint32_t postSendMs = 200;
};

int RunList(std::u16string_view filePath) {
    const std::string narrowPath = Encoding::Utf16ToUtf8(filePath);
    const auto result = TomlLoader::LoadFile(narrowPath);
    if (!result.error.empty()) {
        std::fprintf(stderr, "[ERROR] %s\n", result.error.c_str());
        return EXIT_FAILURE;
    }

    std::printf("Loaded %zu test case(s) from %s\n\n",
                result.cases.size(), narrowPath.c_str());
    for (std::size_t i = 0; i < result.cases.size(); ++i) {
        const auto& tc = result.cases[i];
        std::printf("[%zu] %s  (target=%s)\n",
                    i, tc.name.c_str(), tc.targetApp.c_str());
        std::printf("     keys      = ");
        for (char16_t ch : tc.keys) {
            if (ch == u'\b')      std::printf("\\b");
            else if (ch == u'\t') std::printf("\\t");
            else if (ch == u'\n') std::printf("\\n");
            else if (ch == u'\r') std::printf("\\r");
            else if (ch >= 0x20 && ch < 0x7F) std::putchar(static_cast<char>(ch));
            else                  std::printf("[U+%04X]", static_cast<unsigned>(ch));
        }
        std::printf("  (%zu chars)\n", tc.keys.size());
        std::printf("     expected  = %s\n",
                    Encoding::Utf16ToUtf8(tc.expected).c_str());
        std::printf("     timing    = inter_key=%uus  budget_p99=%uus\n\n",
                    tc.interKeyMicros, tc.budgetP99Micros);
    }
    return EXIT_SUCCESS;
}

// Runs a single TestCase end-to-end: clear, send keys, Ctrl+A+C, read
// clipboard, compare. Returns true on PASS, false on FAIL.
// `failureMessage` is filled with diff details on failure.
// `outWindow` records the wall-clock window (start = just before SendString,
// end = after the post-send/clipboard-settle wait) so the post-mortem L1
// analyzer can slice log entries belonging to this case.
bool RunSingleCase(const TestCase& tc, uint32_t postSendMs,
                   std::string& failureMessage, CaseWindow& outWindow) {
    SendInputDriver::Options drvOpts;
    drvOpts.interKeyMicros = tc.interKeyMicros;
    SendInputDriver::Driver driver(drvOpts);

    // Clear target first (Ctrl+A then Delete) -- not part of the L1 window.
    const bool clearOk = driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false);
    Sleep(kClearStepDelayMs);
    const bool deleteOk = driver.SendKeyCombo(kVkDelete, false, false, false);
    Sleep(kClearStepDelayMs);
    if (!clearOk || !deleteOk) {
        failureMessage = "clear-first SendInput failed";
        return false;
    }

    outWindow.startMs = LocalTimeMs();
    if (!driver.SendString(tc.keys)) {
        failureMessage = "SendString failed (untypeable char or SendInput rejected)";
        return false;
    }

    Sleep(postSendMs);
    outWindow.endMs = LocalTimeMs();

    if (!driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false)) {
        failureMessage = "Ctrl+A SendInput failed";
        return false;
    }
    Sleep(kCtrlACtrlCDelayMs);
    if (!driver.SendKeyCombo(kVkC, /*ctrl=*/true, false, false)) {
        failureMessage = "Ctrl+C SendInput failed";
        return false;
    }
    Sleep(kClipboardSettleMs);

    auto actual = ClipboardReader::ReadText();
    if (!actual) {
        failureMessage = "clipboard read failed (no CF_UNICODETEXT)";
        return false;
    }

    if (*actual == tc.expected) {
        return true;
    }

    failureMessage =
        "expected: " + Encoding::Utf16ToUtf8(tc.expected) +
        "\n        actual:   " + Encoding::Utf16ToUtf8(*actual);
    return false;
}

// After all cases run AND the user has stopped NexusKey (so its 8KB log
// buffer has been flushed by CloseHookLog), parse the full log and compute
// L1 inter-keystroke stats per case using the windows captured during the
// run. NexusKey's log is invisible while the process is alive (file is open
// for writing + entries sit in the in-process buffer); a post-mortem read
// avoids the heisenbug we'd hit with mid-run instrumentation.
//
// Side-effect: fills `results[i].l1Stats` for each case.
void RunPostMortemL1(std::vector<CaseResult>& results,
                     const std::vector<CaseWindow>& windows,
                     const std::filesystem::path& hookLog) {
    std::printf("\nTo compute L1 hook timing per case:\n");
    std::printf("  1. Stop NexusKey (tray -> Quit) so its log buffer flushes.\n");
    std::printf("  2. Press Enter to read the log (or Ctrl+C to skip).\n");
    std::printf("  > ");
    std::fflush(stdout);

    char dummy[16];
    if (std::fgets(dummy, sizeof(dummy), stdin) == nullptr) {
        std::printf("(skipped)\n");
        return;
    }

    const auto allEntries = HookLogParser::ParseFile(hookLog);
    if (allEntries.empty()) {
        std::printf("(no entries parsed from %s -- file missing, empty,\n"
                    " or NexusKey still has it open)\n",
                    hookLog.string().c_str());
        return;
    }

    std::printf("\nL1 hook timing (post-mortem, %zu total log entries):\n",
                allEntries.size());

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& w = windows[i];
        std::vector<HookLogParser::KeystrokeEntry> sliced;
        sliced.reserve(64);
        // Exclusive on endMs: the verify-phase Ctrl+A fires ~1us after we
        // capture endMs and lands in the same GetLocalTime ms tick, so an
        // inclusive filter would slurp its DOWN event into the window and
        // skew max/p99 by exactly postSendMs. Last typed key is logged
        // hundreds of ms before endMs, so exclusive filter is safe for it.
        for (const auto& e : allEntries) {
            if (e.timestampMs >= w.startMs && e.timestampMs < w.endMs) {
                sliced.push_back(e);
            }
        }
        results[i].l1Stats = HookLogParser::ComputeKeyDownStats(sliced);

        if (results[i].l1Stats.intervals == 0) {
            std::printf("[%2zu/%zu] %-50s (no log entries in window)\n",
                        i + 1, results.size(), results[i].name.c_str());
            continue;
        }
        std::printf("[%2zu/%zu] %-50s n=%zu mean=%llums p99=%llums max=%llums\n",
                    i + 1, results.size(), results[i].name.c_str(),
                    results[i].l1Stats.intervals,
                    static_cast<unsigned long long>(results[i].l1Stats.meanMs),
                    static_cast<unsigned long long>(results[i].l1Stats.p99Ms),
                    static_cast<unsigned long long>(results[i].l1Stats.maxMs));
    }
}

void WriteReports(const RunOptions& opt,
                  const std::vector<CaseResult>& results,
                  uint64_t totalWallClockMs) {
    if (!opt.junitXmlPath.empty()) {
        const std::string path = Encoding::Utf16ToUtf8(opt.junitXmlPath);
        std::ofstream out(path);
        if (!out) {
            std::fprintf(stderr, "[ERROR] Cannot open %s for JUnit XML write\n",
                         path.c_str());
        } else {
            JunitXmlWriter::Write(out, "chaos", results, totalWallClockMs);
            std::printf("JUnit XML written: %s\n", path.c_str());
        }
    }
    if (!opt.perfCsvPath.empty()) {
        const std::string path = Encoding::Utf16ToUtf8(opt.perfCsvPath);
        std::ofstream out(path);
        if (!out) {
            std::fprintf(stderr, "[ERROR] Cannot open %s for perf CSV write\n",
                         path.c_str());
        } else {
            PerfCsvWriter::Write(out, results);
            std::printf("Perf CSV written: %s\n", path.c_str());
        }
    }
}

int RunCorpus(const RunOptions& opt) {
    const std::string narrowPath = Encoding::Utf16ToUtf8(opt.corpusFile);
    const auto loadResult = TomlLoader::LoadFile(narrowPath);
    if (!loadResult.error.empty()) {
        std::fprintf(stderr, "[ERROR] %s\n", loadResult.error.c_str());
        return EXIT_FAILURE;
    }

    const auto& cases = loadResult.cases;
    std::printf("Loaded %zu test case(s) from %s\n", cases.size(), narrowPath.c_str());

    const std::filesystem::path hookLog =
        Encoding::Utf16ToUtf8(opt.hookLogPath);
    const bool haveHookLog = !hookLog.empty();
    if (haveHookLog) {
        std::printf("L1 timing source: %s (post-mortem analysis after run)\n",
                    hookLog.string().c_str());
    }

    std::printf("Focus your target window -- starting in %u ms...\n", opt.initialDelayMs);
    std::fflush(stdout);
    Sleep(opt.initialDelayMs);

    int passed = 0;
    int failed = 0;
    const std::size_t total = cases.size();
    std::vector<CaseWindow> windows;
    windows.reserve(total);
    std::vector<CaseResult> results;
    results.reserve(total);

    const uint64_t corpusStartMs = LocalTimeMs();

    for (std::size_t i = 0; i < total; ++i) {
        const auto& tc = cases[i];
        std::printf("[%2zu/%zu] %-50s ", i + 1, total, tc.name.c_str());
        std::fflush(stdout);

        const uint64_t caseStartMs = LocalTimeMs();
        std::string msg;
        CaseWindow window{};
        const bool casePassed = RunSingleCase(tc, opt.postSendMs, msg, window);
        const uint64_t caseEndMs = LocalTimeMs();
        windows.push_back(window);

        CaseResult r;
        r.name = tc.name;
        r.passed = casePassed;
        r.failureMessage = casePassed ? "" : msg;
        r.wallClockMs = (caseEndMs >= caseStartMs) ? (caseEndMs - caseStartMs) : 0;
        r.interKeyMicrosConfig = tc.interKeyMicros;
        results.push_back(std::move(r));

        if (casePassed) {
            std::printf("PASS\n");
            ++passed;
        } else {
            std::printf("FAIL\n        %s\n", msg.c_str());
            ++failed;
        }
    }

    const uint64_t corpusEndMs = LocalTimeMs();
    const uint64_t totalWallClockMs =
        (corpusEndMs >= corpusStartMs) ? (corpusEndMs - corpusStartMs) : 0;

    std::printf("\nSummary: %d PASS, %d FAIL out of %zu\n", passed, failed, total);

    if (haveHookLog) {
        RunPostMortemL1(results, windows, hookLog);
    }

    WriteReports(opt, results, totalWallClockMs);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int RunSend(const RunOptions& opt) {
    const std::u16string toSend =
        opt.raw ? opt.sendText : Telex::StrToTelex(opt.sendText);

    PrintTelexDiagnostic(toSend);
    if (opt.verify) {
        std::printf("Expected: %s\n",
                    Encoding::Utf16ToUtf8(opt.expected).c_str());
    }
    std::printf("Focus your target window -- sending in %u ms...\n",
                opt.initialDelayMs);
    std::fflush(stdout);
    Sleep(opt.initialDelayMs);

    SendInputDriver::Options drvOpts;
    drvOpts.interKeyMicros = opt.interKeyMicros;
    SendInputDriver::Driver driver(drvOpts);

    if (opt.clearFirst) {
        const bool clearOk =
            driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false);
        Sleep(kClearStepDelayMs);
        const bool deleteOk =
            driver.SendKeyCombo(kVkDelete, false, false, false);
        Sleep(kClearStepDelayMs);
        if (!clearOk || !deleteOk) {
            std::fprintf(stderr,
                "[WARN] Clear-first SendInput call failed (target may have leftover text).\n");
        }
    }

    if (!driver.SendString(toSend)) {
        std::fprintf(stderr,
            "[ERROR] Send failed: char untypeable on current layout, or SendInput\n"
            "        call rejected (UAC consent / locked desktop?).\n");
        return EXIT_FAILURE;
    }

    if (!opt.verify) {
        std::printf("Done. %zu chars sent.\n", toSend.size());
        return EXIT_SUCCESS;
    }

    Sleep(opt.postSendMs);
    if (!driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false)) {
        std::fprintf(stderr, "[ERROR] Verify Ctrl+A SendInput failed.\n");
        return EXIT_FAILURE;
    }
    Sleep(kCtrlACtrlCDelayMs);
    if (!driver.SendKeyCombo(kVkC, /*ctrl=*/true, false, false)) {
        std::fprintf(stderr, "[ERROR] Verify Ctrl+C SendInput failed.\n");
        return EXIT_FAILURE;
    }
    Sleep(kClipboardSettleMs);  // give the target app time to update the clipboard

    auto actual = ClipboardReader::ReadText();
    if (!actual) {
        std::fprintf(stderr, "[FAIL] Clipboard read failed (no CF_UNICODETEXT).\n");
        return EXIT_FAILURE;
    }

    if (opt.hasExpected && *actual == opt.expected) {
        std::printf("[PASS] Clipboard matches expected.\n");
        std::printf("       Got: %s\n",
                    Encoding::Utf16ToUtf8(*actual).c_str());
        return EXIT_SUCCESS;
    }

    if (!opt.hasExpected) {
        std::printf("[INFO] Clipboard contents:\n");
        std::printf("       %s\n",
                    Encoding::Utf16ToUtf8(*actual).c_str());
        return EXIT_SUCCESS;
    }

    std::fprintf(stderr, "[FAIL] Clipboard mismatch.\n");
    std::fprintf(stderr, "       Expected: %s\n",
                 Encoding::Utf16ToUtf8(opt.expected).c_str());
    std::fprintf(stderr, "       Actual:   %s\n",
                 Encoding::Utf16ToUtf8(*actual).c_str());
    return EXIT_FAILURE;
}

int Run(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return EXIT_SUCCESS;
    }

    RunOptions opt;

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if ((arg == L"--send" || arg == L"-s") && i + 1 < argc) {
            opt.sendText = WideToU16(argv[++i]);
        } else if (arg == L"--expected" && i + 1 < argc) {
            opt.expected = WideToU16(argv[++i]);
            opt.hasExpected = true;
        } else if (arg.starts_with(L"--inter-key-us=")) {
            opt.interKeyMicros = static_cast<uint32_t>(
                std::wcstoul(arg.data() + 15, nullptr, 10));
        } else if (arg.starts_with(L"--delay-ms=")) {
            opt.initialDelayMs = static_cast<uint32_t>(
                std::wcstoul(arg.data() + 11, nullptr, 10));
        } else if (arg.starts_with(L"--post-send-ms=")) {
            opt.postSendMs = static_cast<uint32_t>(
                std::wcstoul(arg.data() + 15, nullptr, 10));
        } else if (arg == L"--raw") {
            opt.raw = true;
        } else if (arg == L"--verify") {
            opt.verify = true;
            opt.clearFirst = true;  // verify always wants clean target
        } else if (arg == L"--clear-first") {
            opt.clearFirst = true;
        } else if (arg == L"--list" && i + 1 < argc) {
            opt.listFile = WideToU16(argv[++i]);
        } else if (arg == L"--corpus" && i + 1 < argc) {
            opt.corpusFile = WideToU16(argv[++i]);
        } else if (arg == L"--hook-log" && i + 1 < argc) {
            opt.hookLogPath = WideToU16(argv[++i]);
        } else if (arg == L"--junit" && i + 1 < argc) {
            opt.junitXmlPath = WideToU16(argv[++i]);
        } else if (arg == L"--perf-csv" && i + 1 < argc) {
            opt.perfCsvPath = WideToU16(argv[++i]);
        } else if (arg == L"--help" || arg == L"-h") {
            PrintUsage();
            return EXIT_SUCCESS;
        } else {
            std::fprintf(stderr, "[ERROR] Unknown arg: %s\n\n",
                         Encoding::Utf16ToUtf8(WideToU16(argv[i])).c_str());
            PrintUsage();
            return EXIT_FAILURE;
        }
    }

    if (!opt.listFile.empty()) {
        return RunList(opt.listFile);
    }

    if (!opt.corpusFile.empty()) {
        return RunCorpus(opt);
    }

    if (opt.sendText.empty()) {
        PrintUsage();
        return EXIT_SUCCESS;
    }

    return RunSend(opt);
}

}  // namespace NextKey::TestRunner

int wmain(int argc, wchar_t* argv[]) {
    // Force UTF-8 console output so Vietnamese chars in PASS/FAIL diff render
    // correctly regardless of `chcp` state. wmain (vs main) gives us wide argv
    // so Vietnamese values in --send / --expected don't go through the lossy
    // ANSI codepage conversion.
    SetConsoleOutputCP(CP_UTF8);
    return NextKey::TestRunner::Run(argc, argv);
}
