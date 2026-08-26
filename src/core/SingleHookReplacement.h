// VKey - single-active hook replacement transaction
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace NextKey {

enum class SingleHookUnhookStatus {
    Removed,
    AlreadyAbsent,
    Failed,
};

struct SingleHookReplacementOps {
    void* context = nullptr;
    SingleHookUnhookStatus (*unhook)(
        void*, std::uintptr_t) noexcept = nullptr;
    std::uintptr_t (*install)(void*) noexcept = nullptr;
};

enum class SingleHookReplacementStatus {
    Replaced,
    UnhookFailed,
    InstallFailed,
};

[[nodiscard]] inline SingleHookReplacementStatus ReplaceSingleHook(
    std::uintptr_t& activeHandle,
    const SingleHookReplacementOps& ops) noexcept {
    if (activeHandle != 0) {
        if (ops.unhook == nullptr
            || ops.unhook(ops.context, activeHandle)
                == SingleHookUnhookStatus::Failed) {
            return SingleHookReplacementStatus::UnhookFailed;
        }
        activeHandle = 0;
    }

    if (ops.install == nullptr) {
        return SingleHookReplacementStatus::InstallFailed;
    }

    activeHandle = ops.install(ops.context);
    return activeHandle != 0
        ? SingleHookReplacementStatus::Replaced
        : SingleHookReplacementStatus::InstallFailed;
}

}  // namespace NextKey
