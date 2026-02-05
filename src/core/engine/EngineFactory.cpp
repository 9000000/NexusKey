// NexusKey - Input Engine Factory Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "EngineFactory.h"
#include "TelexEngine.h"
#include "VniEngine.h"

namespace NextKey {

std::unique_ptr<IInputEngine> EngineFactory::Create(const TypingConfig& config) {
    return Create(config.inputMethod);
}

std::unique_ptr<IInputEngine> EngineFactory::Create(InputMethod method) {
    switch (method) {
        case InputMethod::VNI:
            return std::make_unique<Vni::VniEngine>();
        case InputMethod::Telex:
        default:
            return std::make_unique<Telex::TelexEngine>();
    }
}

}  // namespace NextKey
