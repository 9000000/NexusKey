// NexusKey - Smart Switch Shared Memory Manager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "SmartSwitchManager.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace NextKey {

static constexpr const wchar_t* SMART_SWITCH_MEM_NAME = L"Local\\NexusKeySmartSwitch";

struct SmartSwitchManager::Impl {
#ifdef _WIN32
    HANDLE hMapping = nullptr;
    volatile SmartSwitchState* pState = nullptr;
#endif
    bool isOwner = false;
};

SmartSwitchManager::SmartSwitchManager() : pImpl_(std::make_unique<Impl>()) {}

SmartSwitchManager::~SmartSwitchManager() {
#ifdef _WIN32
    if (pImpl_->pState) {
        UnmapViewOfFile(const_cast<SmartSwitchState*>(pImpl_->pState));
    }
    if (pImpl_->hMapping) {
        CloseHandle(pImpl_->hMapping);
    }
#endif
}

bool SmartSwitchManager::Create() {
#ifdef _WIN32
    pImpl_->hMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SmartSwitchState),
        SMART_SWITCH_MEM_NAME
    );

    if (!pImpl_->hMapping) return false;

    pImpl_->pState = static_cast<volatile SmartSwitchState*>(
        MapViewOfFile(pImpl_->hMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SmartSwitchState))
    );

    if (!pImpl_->pState) {
        CloseHandle(pImpl_->hMapping);
        pImpl_->hMapping = nullptr;
        return false;
    }

    auto* p = const_cast<SmartSwitchState*>(pImpl_->pState);
    p->InitDefaults();
    pImpl_->isOwner = true;
    return true;
#else
    return false;
#endif
}

bool SmartSwitchManager::OpenReadWrite() {
#ifdef _WIN32
    pImpl_->hMapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        SMART_SWITCH_MEM_NAME
    );

    if (!pImpl_->hMapping) return false;

    pImpl_->pState = static_cast<volatile SmartSwitchState*>(
        MapViewOfFile(pImpl_->hMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SmartSwitchState))
    );

    if (!pImpl_->pState) {
        CloseHandle(pImpl_->hMapping);
        pImpl_->hMapping = nullptr;
        return false;
    }

    return pImpl_->pState->magic == SmartSwitchState::MAGIC_VALUE;
#else
    return false;
#endif
}

void SmartSwitchManager::SetAppMode(const std::wstring& exeName, bool vietnamese) noexcept {
#ifdef _WIN32
    if (!pImpl_->pState) return;

    auto* p = const_cast<SmartSwitchState*>(pImpl_->pState);
    uint32_t hash = FnvHash(exeName.c_str());

    // Look for existing entry
    for (uint32_t i = 0; i < p->count && i < SMART_SWITCH_MAX_ENTRIES; ++i) {
        if (p->entries[i].hash == hash) {
            p->entries[i].vietnamese = vietnamese ? 1 : 0;
            p->epoch++;
            return;
        }
    }

    // Add new entry (if space available)
    if (p->count < SMART_SWITCH_MAX_ENTRIES) {
        p->entries[p->count].hash = hash;
        p->entries[p->count].vietnamese = vietnamese ? 1 : 0;
        p->count++;
        p->epoch++;
    }
#endif
}

std::optional<bool> SmartSwitchManager::GetAppMode(const std::wstring& exeName) const noexcept {
#ifdef _WIN32
    if (!pImpl_->pState) return std::nullopt;

    auto* p = const_cast<const SmartSwitchState*>(pImpl_->pState);
    uint32_t hash = FnvHash(exeName.c_str());

    for (uint32_t i = 0; i < p->count && i < SMART_SWITCH_MAX_ENTRIES; ++i) {
        if (p->entries[i].hash == hash) {
            return p->entries[i].vietnamese != 0;
        }
    }
#endif
    return std::nullopt;
}

void SmartSwitchManager::LoadFromMap(const std::unordered_map<std::wstring, bool>& map) noexcept {
#ifdef _WIN32
    if (!pImpl_->pState) return;

    auto* p = const_cast<SmartSwitchState*>(pImpl_->pState);
    p->count = 0;

    for (const auto& [exeName, vietnamese] : map) {
        if (p->count >= SMART_SWITCH_MAX_ENTRIES) break;
        p->entries[p->count].hash = FnvHash(exeName.c_str());
        p->entries[p->count].vietnamese = vietnamese ? 1 : 0;
        p->count++;
    }

    p->epoch++;
#endif
}

bool SmartSwitchManager::IsConnected() const noexcept {
#ifdef _WIN32
    return pImpl_->pState != nullptr && pImpl_->pState->magic == SmartSwitchState::MAGIC_VALUE;
#else
    return false;
#endif
}

}  // namespace NextKey
