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
#include <string>
#include <string_view>

#include "ClipboardReader.h"
#include "Encoding.h"
#include "SendInputDriver.h"
#include "Telex.h"
#include "TomlLoader.h"

namespace NextKey::TestRunner {

constexpr const char* kVersion = "0.4.0-d7-corpus";

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
    std::printf("                       (auto-enables clear-first per case)\n\n");
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
bool RunSingleCase(const TestCase& tc, uint32_t postSendMs,
                   std::string& failureMessage) {
    SendInputDriver::Options drvOpts;
    drvOpts.interKeyMicros = tc.interKeyMicros;
    SendInputDriver::Driver driver(drvOpts);

    constexpr uint16_t kVkA = 0x41;
    constexpr uint16_t kVkC = 0x43;
    constexpr uint16_t kVkDelete = 0x2E;

    // Clear target first.
    if (!driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false) ||
        (Sleep(30), !driver.SendKeyCombo(kVkDelete, false, false, false))) {
        failureMessage = "clear-first SendInput failed";
        return false;
    }
    Sleep(30);

    if (!driver.SendString(tc.keys)) {
        failureMessage = "SendString failed (untypeable char or SendInput rejected)";
        return false;
    }

    Sleep(postSendMs);
    if (!driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false)) {
        failureMessage = "Ctrl+A SendInput failed";
        return false;
    }
    Sleep(50);
    if (!driver.SendKeyCombo(kVkC, /*ctrl=*/true, false, false)) {
        failureMessage = "Ctrl+C SendInput failed";
        return false;
    }
    Sleep(150);

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

int RunCorpus(const RunOptions& opt) {
    const std::string narrowPath = Encoding::Utf16ToUtf8(opt.corpusFile);
    const auto loadResult = TomlLoader::LoadFile(narrowPath);
    if (!loadResult.error.empty()) {
        std::fprintf(stderr, "[ERROR] %s\n", loadResult.error.c_str());
        return EXIT_FAILURE;
    }

    const auto& cases = loadResult.cases;
    std::printf("Loaded %zu test case(s) from %s\n", cases.size(), narrowPath.c_str());
    std::printf("Focus your target window -- starting in %u ms...\n", opt.initialDelayMs);
    std::fflush(stdout);
    Sleep(opt.initialDelayMs);

    int passed = 0;
    int failed = 0;
    const std::size_t total = cases.size();

    for (std::size_t i = 0; i < total; ++i) {
        const auto& tc = cases[i];
        std::printf("[%2zu/%zu] %-50s ", i + 1, total, tc.name.c_str());
        std::fflush(stdout);

        std::string msg;
        if (RunSingleCase(tc, opt.postSendMs, msg)) {
            std::printf("PASS\n");
            ++passed;
        } else {
            std::printf("FAIL\n        %s\n", msg.c_str());
            ++failed;
        }
    }

    std::printf("\nSummary: %d PASS, %d FAIL out of %zu\n", passed, failed, total);
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

    constexpr uint16_t kVkA = 0x41;
    constexpr uint16_t kVkC = 0x43;
    constexpr uint16_t kVkDelete = 0x2E;

    if (opt.clearFirst) {
        const bool clearOk =
            driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false);
        Sleep(30);
        const bool deleteOk =
            driver.SendKeyCombo(kVkDelete, false, false, false);
        Sleep(30);
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
    Sleep(50);
    if (!driver.SendKeyCombo(kVkC, /*ctrl=*/true, false, false)) {
        std::fprintf(stderr, "[ERROR] Verify Ctrl+C SendInput failed.\n");
        return EXIT_FAILURE;
    }
    Sleep(150);  // give the target app time to update the clipboard

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
