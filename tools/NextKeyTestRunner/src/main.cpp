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

namespace NextKey::TestRunner {

constexpr const char* kVersion = "0.3.0-d4-verify";

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
    bool raw = false;
    bool verify = false;
    bool clearFirst = false;
    bool hasExpected = false;
    uint32_t interKeyMicros = 10'000;
    uint32_t initialDelayMs = 3'000;
    uint32_t postSendMs = 200;
};

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
        driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false);
        Sleep(30);
        driver.SendKeyCombo(kVkDelete, false, false, false);
        Sleep(30);
    }

    if (!driver.SendString(toSend)) {
        std::fprintf(stderr,
            "[ERROR] One or more chars cannot be typed on current layout.\n"
            "        (uppercase Vietnamese passthrough is a known limitation)\n");
        return EXIT_FAILURE;
    }

    if (!opt.verify) {
        std::printf("Done. %zu chars sent.\n", toSend.size());
        return EXIT_SUCCESS;
    }

    Sleep(opt.postSendMs);
    driver.SendKeyCombo(kVkA, /*ctrl=*/true, false, false);
    Sleep(50);
    driver.SendKeyCombo(kVkC, /*ctrl=*/true, false, false);
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
