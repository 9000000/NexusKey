// NexusKey - Config Event Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ConfigEvent.h"
#include "../ipc/SecurityHelpers.h"
#include <Windows.h>

namespace NextKey {

// Named event for config change notifications
static constexpr wchar_t kEventName[] = L"Local\\NexusKeyConfigEvent";

struct ConfigEvent::Impl {
    HANDLE hEvent = nullptr;
};

ConfigEvent::ConfigEvent() : pImpl_(std::make_unique<Impl>()) {}

ConfigEvent::~ConfigEvent() {
    if (pImpl_ && pImpl_->hEvent) {
        CloseHandle(pImpl_->hEvent);
        pImpl_->hEvent = nullptr;
    }
}

ConfigEvent::ConfigEvent(ConfigEvent&& other) noexcept = default;
ConfigEvent& ConfigEvent::operator=(ConfigEvent&& other) noexcept = default;

bool ConfigEvent::Initialize() {
    if (pImpl_->hEvent) return true;  // Already initialized

    // Create or open named event
    // Auto-reset event: resets after single wait is satisfied
    auto sa = NextKey::MakeCreatorOnlySecurityAttributes();
    pImpl_->hEvent = CreateEventW(
        &sa,            // restricted DACL: SYSTEM + creator/owner only
        FALSE,          // auto-reset event
        FALSE,          // initial state: not signaled
        kEventName      // named event for cross-process
    );
    if (sa.lpSecurityDescriptor) LocalFree(sa.lpSecurityDescriptor);

    if (!pImpl_->hEvent) {
        OutputDebugStringW(L"ConfigEvent: Failed to create event\n");
        return false;
    }

    // Check if we created or opened existing
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        OutputDebugStringW(L"ConfigEvent: Opened existing event\n");
    } else {
        OutputDebugStringW(L"ConfigEvent: Created new event\n");
    }

    return true;
}

void ConfigEvent::Signal() {
    if (pImpl_->hEvent) {
        SetEvent(pImpl_->hEvent);
        OutputDebugStringW(L"ConfigEvent: Signaled\n");
    }
}

bool ConfigEvent::Wait(unsigned int timeoutMs) {
    if (!pImpl_->hEvent) return false;

    DWORD result = WaitForSingleObject(pImpl_->hEvent, timeoutMs);
    return (result == WAIT_OBJECT_0);
}

bool ConfigEvent::IsValid() const {
    return pImpl_ && pImpl_->hEvent != nullptr;
}

}  // namespace NextKey
