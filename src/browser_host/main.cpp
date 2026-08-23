// VKey browser native-messaging host
// SPDX-License-Identifier: GPL-3.0-only

#include "NativeMessaging.h"
#include "core/ipc/BrowserContextManager.h"

#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool ReadFrame(std::string& payload) {
    std::uint32_t size = 0;
    if (!std::cin.read(reinterpret_cast<char*>(&size), sizeof(size))) return false;
    if (size == 0 || size > NextKey::BrowserHost::kMaxNativeMessageBytes) return false;
    payload.resize(size);
    return static_cast<bool>(std::cin.read(payload.data(), size));
}

void WriteFrame(std::string_view payload) {
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::cout.write(reinterpret_cast<const char*>(&size), sizeof(size));
    std::cout.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    std::cout.flush();
}

} // namespace

int main() {
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);

    NextKey::BrowserContextManager context;
    if (!context.Create()) return 2;

    const std::uint32_t processId = GetCurrentProcessId();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const std::uint64_t nonce = static_cast<std::uint64_t>(counter.QuadPart)
        ^ (static_cast<std::uint64_t>(processId) << 32);
    std::uint32_t lastModeEventSequence = 0;

    std::string payload;
    while (ReadFrame(payload)) {
        NextKey::BrowserHost::NativeMessage message;
        std::string error;
        if (!NextKey::BrowserHost::ParseNativeMessage(payload, message, error)) {
            WriteFrame("{\"ok\":false,\"error\":\"" + error + "\"}");
            continue;
        }
        bool ok = true;
        if (message.focused) {
            ok = context.Publish(processId, nonce, GetTickCount64(), true,
                                 message.route, message.mode, message.browserExe,
                                 message.hostname);
        } else {
            // An unfocused Chrome profile must not overwrite the route most
            // recently published by a focused Firefox/Edge profile. Clear is
            // owner-conditional for exactly this multi-browser heartbeat race.
            context.ClearIfOwned(processId, nonce, GetTickCount64());
        }
        if (!ok) {
            WriteFrame("{\"ok\":false,\"error\":\"publish\"}");
            continue;
        }
        NextKey::BrowserContextState state{};
        NextKey::BrowserHost::NativeModeEvent event{};
        if (message.protocol >= 2 && context.Read(state)
            && NextKey::BrowserHost::TryReadModeEvent(
                state, processId, nonce, lastModeEventSequence, event)) {
            lastModeEventSequence = event.sequence;
            WriteFrame(NextKey::BrowserHost::SerializeModeEvent(event));
        } else {
            WriteFrame("{\"ok\":true}");
        }
    }

    context.ClearIfOwned(processId, nonce, GetTickCount64());
    return 0;
}
