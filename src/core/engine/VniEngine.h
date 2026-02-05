// NexusKey - VNI Input Method Engine
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "IInputEngine.h"
#include "core/TypingConfig.h"
#include <vector>

namespace NextKey {
namespace Vni {

/// VNI modifier types
enum class Modifier : uint8_t {
    None = 0,
    Circumflex,  // â ê ô (key 6)
    Breve,       // ă (key 8)
    Horn,        // ơ ư (key 7)
    Stroke       // đ (key 9)
};

/// VNI tone types (keys 1-5)
enum class Tone : uint8_t {
    None = 0,
    Acute,   // sắc (key 1)
    Grave,   // huyền (key 2)
    Hook,    // hỏi (key 3)
    Tilde,   // ngã (key 4)
    Dot      // nặng (key 5)
};

/// Character state for VNI engine
struct CharState {
    wchar_t base = 0;
    Modifier mod = Modifier::None;
    Tone tone = Tone::None;
    bool isUpper = false;
    
    bool IsVowel() const;
    bool IsD() const { return base == L'd'; }
};

/// VNI Input Method Engine
/// Implements IInputEngine for VNI typing method
/// Uses table-driven architecture like TelexEngine
class VniEngine : public IInputEngine {
public:
    explicit VniEngine(const TypingConfig& config = TypingConfig{});
    
    // IInputEngine interface
    void PushChar(wchar_t c) override;
    void Backspace() override;
    std::wstring Peek() const override;
    std::wstring Commit() override;
    void Reset() override;
    size_t Count() const override;
    
private:
    // Processing
    bool ProcessModifier(wchar_t c);
    bool ProcessTone(wchar_t c);
    void ProcessChar(wchar_t c);
    
    // Character composition
    wchar_t ComposeChar(const CharState& state) const;
    CharState* FindToneTarget();
    
    // State
    std::vector<CharState> states_;
    std::wstring rawInput_;
    TypingConfig config_;
};

}  // namespace Vni
}  // namespace NextKey
