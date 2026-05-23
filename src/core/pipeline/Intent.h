// src/core/pipeline/Intent.h
//
// Output intent — features emit one or more Intents per Handled keystroke.
// Coordinator accumulates intents into the OutputChannel which serializes them
// into a single Win32 SendInput batch (plus injector-specific quirks).
// Features NEVER call SendInput directly. Pattern B resolution in design.
//
// `Intent` is a std::variant alias so callers can use std::holds_alternative
// / std::get / std::visit directly without member-template gymnastics.
#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace NextKey::Pipeline {

namespace Intents {
    struct Backspace { unsigned count; };
    struct Text      { std::wstring text; };
    struct Reinject  { std::uint16_t vk; };
}

using Intent = std::variant<Intents::Backspace, Intents::Text, Intents::Reinject>;

}  // namespace NextKey::Pipeline
