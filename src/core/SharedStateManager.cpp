// NexusKey - SharedStateManager Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "SharedStateManager.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace NextKey {

static constexpr const wchar_t* SHARED_MEM_NAME = L"Local\\NexusKeySharedState";

struct SharedStateManager::Impl {
#ifdef _WIN32
    HANDLE hMapping = nullptr;
    SharedState* pState = nullptr;
#endif
    bool isOwner = false;
};

SharedStateManager::SharedStateManager() : pImpl_(std::make_unique<Impl>()) {}

SharedStateManager::~SharedStateManager() {
#ifdef _WIN32
    if (pImpl_->pState) {
        UnmapViewOfFile(pImpl_->pState);
    }
    if (pImpl_->hMapping) {
        CloseHandle(pImpl_->hMapping);
    }
#endif
}

SharedStateManager::SharedStateManager(SharedStateManager&&) noexcept = default;
SharedStateManager& SharedStateManager::operator=(SharedStateManager&&) noexcept = default;

bool SharedStateManager::Create() {
#ifdef _WIN32
    pImpl_->hMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SharedState),
        SHARED_MEM_NAME
    );

    if (!pImpl_->hMapping) {
        return false;
    }

    pImpl_->pState = static_cast<SharedState*>(
        MapViewOfFile(pImpl_->hMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState))
    );

    if (!pImpl_->pState) {
        CloseHandle(pImpl_->hMapping);
        pImpl_->hMapping = nullptr;
        return false;
    }

    // Initialize with magic
    pImpl_->pState->magic = SharedState::MAGIC_VALUE;
    pImpl_->pState->epoch = 0;
    pImpl_->pState->flags = SharedFlags::ENGINE_ENABLED;
    pImpl_->pState->reserved = 0;
    pImpl_->isOwner = true;

    return true;
#else
    return false;
#endif
}

bool SharedStateManager::Open() {
#ifdef _WIN32
    pImpl_->hMapping = OpenFileMappingW(
        FILE_MAP_READ,
        FALSE,
        SHARED_MEM_NAME
    );

    if (!pImpl_->hMapping) {
        return false;
    }

    pImpl_->pState = static_cast<SharedState*>(
        MapViewOfFile(pImpl_->hMapping, FILE_MAP_READ, 0, 0, sizeof(SharedState))
    );

    if (!pImpl_->pState) {
        CloseHandle(pImpl_->hMapping);
        pImpl_->hMapping = nullptr;
        return false;
    }

    return pImpl_->pState->IsValid();
#else
    return false;
#endif
}

SharedState SharedStateManager::Read() const noexcept {
    SharedState state{};
#ifdef _WIN32
    if (pImpl_->pState && pImpl_->pState->IsValid()) {
        state = *pImpl_->pState;
    }
#endif
    return state;
}

void SharedStateManager::Write(const SharedState& state) noexcept {
#ifdef _WIN32
    if (pImpl_->pState && pImpl_->isOwner) {
        *pImpl_->pState = state;
    }
#endif
}

bool SharedStateManager::IsConnected() const noexcept {
#ifdef _WIN32
    return pImpl_->pState != nullptr && pImpl_->pState->IsValid();
#else
    return false;
#endif
}

}  // namespace NextKey
