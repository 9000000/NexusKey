// VKey - Browser extension routing shared-memory manager
// SPDX-License-Identifier: GPL-3.0-only

#include "BrowserContextManager.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <atomic>
#include <cstring>
#include <new>

namespace NextKey {

namespace {
constexpr wchar_t kMappingName[] = L"Local\\VKeyBrowserContext-v1";
constexpr wchar_t kMutexName[] = L"Local\\VKeyBrowserContextWrite-v1";
}

struct BrowserContextManager::Impl {
#ifdef _WIN32
    HANDLE mapping{nullptr};
    HANDLE writeMutex{nullptr};
    volatile BrowserContextState* state{nullptr};
#endif
};

BrowserContextManager::BrowserContextManager() : impl_(std::make_unique<Impl>()) {}

BrowserContextManager::~BrowserContextManager() {
#ifdef _WIN32
    if (impl_->state) UnmapViewOfFile(const_cast<BrowserContextState*>(impl_->state));
    if (impl_->mapping) CloseHandle(impl_->mapping);
    if (impl_->writeMutex) CloseHandle(impl_->writeMutex);
#endif
}

bool BrowserContextManager::Create() {
#ifdef _WIN32
    if (impl_->state) return true;
    impl_->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        0, sizeof(BrowserContextState), kMappingName);
    if (!impl_->mapping) return false;
    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    impl_->state = static_cast<volatile BrowserContextState*>(
        MapViewOfFile(impl_->mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                      sizeof(BrowserContextState)));
    impl_->writeMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!impl_->state || !impl_->writeMutex) return false;
    if (created) {
        auto* state = const_cast<BrowserContextState*>(impl_->state);
        new (state) BrowserContextState{};
    }
    return true;
#else
    return false;
#endif
}

bool BrowserContextManager::Read(BrowserContextState& out) const noexcept {
#ifdef _WIN32
    return ReadBrowserContextSeqlock(impl_->state, out);
#else
    (void)out;
    return false;
#endif
}

std::uint32_t BrowserContextManager::ReadGeneration() const noexcept {
#ifdef _WIN32
    return impl_->state ? impl_->state->generation : 0;
#else
    return 0;
#endif
}

bool BrowserContextManager::Publish(
        std::uint32_t ownerProcessId, std::uint64_t ownerNonce,
        std::uint64_t updatedTickMs, bool focused, BrowserRoute route,
        std::string_view browserExe, std::string_view hostname) noexcept {
#ifdef _WIN32
    if (!impl_->state || !impl_->writeMutex || ownerProcessId == 0 || ownerNonce == 0
        || browserExe.empty() || browserExe.size() >= 32
        || hostname.size() >= 256)
        return false;
    const DWORD wait = WaitForSingleObject(impl_->writeMutex, 1000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return false;

    auto* state = const_cast<BrowserContextState*>(impl_->state);
    auto* generation = reinterpret_cast<volatile LONG*>(&state->generation);
    InterlockedIncrement(generation);
    state->magic = kBrowserContextMagic;
    state->version = kBrowserContextVersion;
    state->structSize = sizeof(BrowserContextState);
    state->ownerProcessId = ownerProcessId;
    state->ownerNonce = ownerNonce;
    state->updatedTickMs = updatedTickMs;
    state->focused = focused ? 1 : 0;
    state->route = focused ? route : BrowserRoute::Default;
    std::memset(state->browserExe, 0, sizeof(state->browserExe));
    std::memcpy(state->browserExe, browserExe.data(), browserExe.size());
    std::memset(state->hostname, 0, sizeof(state->hostname));
    std::memcpy(state->hostname, hostname.data(), hostname.size());
    std::atomic_thread_fence(std::memory_order_release);
    InterlockedIncrement(generation);
    ReleaseMutex(impl_->writeMutex);
    return true;
#else
    (void)ownerProcessId; (void)ownerNonce; (void)updatedTickMs; (void)focused;
    (void)route; (void)browserExe; (void)hostname;
    return false;
#endif
}

void BrowserContextManager::ClearIfOwned(
        std::uint32_t ownerProcessId, std::uint64_t ownerNonce,
        std::uint64_t updatedTickMs) noexcept {
#ifdef _WIN32
    if (!impl_->state || !impl_->writeMutex) return;
    const DWORD wait = WaitForSingleObject(impl_->writeMutex, 1000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return;
    auto* state = const_cast<BrowserContextState*>(impl_->state);
    if (IsBrowserContextOwnedBy(*state, ownerProcessId, ownerNonce)) {
        auto* generation = reinterpret_cast<volatile LONG*>(&state->generation);
        InterlockedIncrement(generation);
        state->focused = 0;
        state->route = BrowserRoute::Default;
        state->updatedTickMs = updatedTickMs;
        std::atomic_thread_fence(std::memory_order_release);
        InterlockedIncrement(generation);
    }
    ReleaseMutex(impl_->writeMutex);
#else
    (void)ownerProcessId; (void)ownerNonce; (void)updatedTickMs;
#endif
}

} // namespace NextKey
