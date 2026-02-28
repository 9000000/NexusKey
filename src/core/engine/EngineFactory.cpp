// NexusKey - Input Engine Factory Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "EngineFactory.h"
#include "TelexEngine.h"
#include "VniEngine.h"

namespace NextKey {

std::unique_ptr<IInputEngine> EngineFactory::Create(const TypingConfig& config) {
    switch (config.inputMethod) {
        case InputMethod::VNI:
            return std::make_unique<Vni::VniEngine>(config);
        case InputMethod::SimpleTelex:
        case InputMethod::Telex:
        default:
            return std::make_unique<Telex::TelexEngine>(config);
    }
}

std::unique_ptr<IInputEngine> EngineFactory::Create(InputMethod method) {
    TypingConfig config;
    config.inputMethod = method;
    return Create(config);
}

}  // namespace NextKey
