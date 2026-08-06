// VKey - Input Engine Factory Implementation
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only

#include "EngineFactory.h"
#include "TypingEngine.h"
#include "core/Logger.h"

#ifdef VKEY_USE_RUST_ENGINE
#include "RustInputEngine.h"
#endif

namespace NextKey {

bool EngineFactory::WillUseRustEngine(const TypingConfig& config) {
#ifdef VKEY_USE_RUST_ENGINE
    return config.spellSuggestEnabled && RustInputEngine::LibraryAvailable();
#else
    (void)config;
    return false;
#endif
}

bool EngineFactory::RustEngineExpectedButUnavailable(const TypingConfig& config) {
#ifdef VKEY_USE_RUST_ENGINE
    return config.spellSuggestEnabled && !RustInputEngine::LibraryAvailable();
#else
    (void)config;
    return false;
#endif
}

std::unique_ptr<IInputEngine> EngineFactory::Create(const TypingConfig& config) {
#ifdef VKEY_USE_RUST_ENGINE
    if (WillUseRustEngine(config)) {
        Logger::Log(L"[Engine] Using Rust engine");
        return std::make_unique<RustInputEngine>(config);
    }
    if (config.spellSuggestEnabled) {
        Logger::Log(L"[Engine] Rust engine unavailable (%ls), falling back to TypingEngine",
                    RustInputEngine::UnavailableReason().c_str());
    }
#endif
    // All input methods route through TypingEngine (unified engine).
    // Mode dispatch happens inside TypingEngine via IsTelexMode()/IsVniMode().
    return std::make_unique<TypingEngine>(config);
}

std::unique_ptr<IInputEngine> EngineFactory::Create(InputMethod method) {
    TypingConfig config;
    config.inputMethod = method;
    return Create(config);
}

}  // namespace NextKey
