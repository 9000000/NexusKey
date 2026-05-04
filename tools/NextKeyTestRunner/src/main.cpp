// NextKeyTestRunner — E2E stress test harness for NexusKey IME.
// Drives SendInput against a running NexusKey hook, verifies clipboard output,
// and emits per-keystroke timing for L1 (NexusKey internal) and L2 (end-to-end).
//
// Phase 0a status: D3 — SendInput driver wired. No verify yet (D4).
// See _bmad-output/brainstorming/brainstorming-session-2026-05-03-1201.md
// (Phase 7.3 day-by-day plan) for the full sprint definition of done.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#ifdef _WIN32
// clang-format off
#include <Windows.h>
// clang-format on
#include "SendInputDriver.h"
#endif

#include "Telex.h"

namespace NextKey::TestRunner {

constexpr const char* kVersion = "0.2.0-d3-driver";

void PrintUsage() {
    std::printf("NextKeyTestRunner v%s\n", kVersion);
    std::printf("E2E stress test harness for NexusKey IME (Windows-only)\n\n");
    std::printf("Usage:\n");
    std::printf("  NextKeyTestRunner.exe                    Print this help\n");
    std::printf("  NextKeyTestRunner.exe --send TEXT [opts] Drive SendInput\n\n");
    std::printf("Options:\n");
    std::printf("  --send TEXT          Vietnamese or Telex text to type\n");
    std::printf("  --inter-key-us=N     Inter-key delay in microseconds (default 10000)\n");
    std::printf("  --delay-ms=N         Initial delay before sending (default 3000)\n");
    std::printf("  --raw                Skip Telex conversion; type TEXT verbatim\n");
    std::printf("  --help, -h           Show this help\n\n");
    std::printf("Example:\n");
    std::printf("  NextKeyTestRunner.exe --send vieejt --raw\n");
    std::printf("    Types raw Telex keystrokes v-i-e-e-j-t. With NexusKey hook\n");
    std::printf("    active, the target window will display the Vietnamese for\n");
    std::printf("    'viet' with the dot-below tone mark.\n\n");
    std::printf("  NextKeyTestRunner.exe --send <vietnamese-text>\n");
    std::printf("    (run 'chcp 65001' first for UTF-8 console.)\n");
    std::printf("    Telex.h converts the input back to raw keystrokes,\n");
    std::printf("    then types them.\n");
}

#ifdef _WIN32

// CLI args arrive as char* in the active console codepage. We widen via
// CP_UTF8 — works when the user runs `chcp 65001` first or uses Windows
// Terminal (UTF-8 by default). ASCII-only args work regardless.
std::u16string Utf8ToU16(const char* input) {
    if (!input || !*input) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, input, -1, nullptr, 0);
    if (needed <= 1) return {};  // <= 1 means just the null terminator
    std::u16string out(static_cast<size_t>(needed - 1), 0);
    MultiByteToWideChar(
        CP_UTF8, 0, input, -1,
        reinterpret_cast<LPWSTR>(out.data()), needed);
    return out;
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

int RunSend(std::u16string_view text, bool raw,
            uint32_t interKeyMicros, uint32_t initialDelayMs) {
    const std::u16string toSend =
        raw ? std::u16string(text) : Telex::StrToTelex(text);

    PrintTelexDiagnostic(toSend);
    std::printf("Focus your target window -- sending in %u ms...\n", initialDelayMs);
    std::fflush(stdout);
    Sleep(initialDelayMs);

    SendInputDriver::Options opts;
    opts.interKeyMicros = interKeyMicros;
    SendInputDriver::Driver driver(opts);

    if (!driver.SendString(toSend)) {
        std::fprintf(stderr,
            "[ERROR] One or more chars cannot be typed on current layout.\n"
            "        (uppercase Vietnamese passthrough is a known D3 limitation)\n");
        return EXIT_FAILURE;
    }

    std::printf("Done. %zu chars sent.\n", toSend.size());
    return EXIT_SUCCESS;
}

#endif  // _WIN32

int Run(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return EXIT_SUCCESS;
    }

#ifdef _WIN32
    std::u16string sendText;
    bool raw = false;
    uint32_t interKeyMicros = 10'000;
    uint32_t initialDelayMs = 3'000;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if ((arg == "--send" || arg == "-s") && i + 1 < argc) {
            sendText = Utf8ToU16(argv[++i]);
        } else if (arg.starts_with("--inter-key-us=")) {
            interKeyMicros = static_cast<uint32_t>(
                std::strtoul(arg.data() + 15, nullptr, 10));
        } else if (arg.starts_with("--delay-ms=")) {
            initialDelayMs = static_cast<uint32_t>(
                std::strtoul(arg.data() + 11, nullptr, 10));
        } else if (arg == "--raw") {
            raw = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return EXIT_SUCCESS;
        } else {
            std::fprintf(stderr, "[ERROR] Unknown arg: %s\n\n", argv[i]);
            PrintUsage();
            return EXIT_FAILURE;
        }
    }

    if (sendText.empty()) {
        PrintUsage();
        return EXIT_SUCCESS;
    }

    return RunSend(sendText, raw, interKeyMicros, initialDelayMs);
#else
    (void)argv;
    std::fprintf(stderr, "[ERROR] Send mode requires Windows.\n");
    return EXIT_FAILURE;
#endif
}

}  // namespace NextKey::TestRunner

int main(int argc, char* argv[]) {
    return NextKey::TestRunner::Run(argc, argv);
}
