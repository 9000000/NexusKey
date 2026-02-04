// NexusKey - TelexEngine Adapter Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "TelexEngineAdapter.h"

namespace NextKey {

TelexEngineAdapter::TelexEngineAdapter(const TypingConfig& config)
    : engine_(std::make_unique<Telex::TelexEngine>(config)) {
}

TelexEngineAdapter::~TelexEngineAdapter() = default;

bool TelexEngineAdapter::ProcessKeyDown(uint32_t vkCode, bool shiftPressed) {
    if (!keyTranslator_.IsCharacterKey(vkCode)) {
        return false;
    }

    wchar_t c = keyTranslator_.VirtualKeyToChar(vkCode, shiftPressed);
    if (c != 0) {
        engine_->PushChar(c);
        return true;
    }
    return false;
}

void TelexEngineAdapter::ProcessBackspace() {
    engine_->Backspace();
}

std::wstring TelexEngineAdapter::GetComposition() const {
    return engine_->Peek();
}

std::wstring TelexEngineAdapter::Commit() {
    return engine_->Commit();
}

void TelexEngineAdapter::Reset() {
    engine_->Reset();
}

bool TelexEngineAdapter::HasComposition() const {
    return engine_->Count() > 0;
}

}  // namespace NextKey
