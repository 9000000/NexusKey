// NexusKey - VNI Input Method Engine
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "IInputEngine.h"
#include "SpellChecker.h"
#include "core/config/TypingConfig.h"
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
    size_t rawIdx = 0;  // rawInput_ index when this state was created (for backspace sync)

    [[nodiscard]] bool IsVowel() const noexcept;
    [[nodiscard]] constexpr bool IsD() const noexcept { return base == L'd'; }
    [[nodiscard]] constexpr bool HasModifier() const noexcept { return mod != Modifier::None; }
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
    [[nodiscard]] std::wstring Peek() const override;
    [[nodiscard]] std::wstring Commit() override;
    void Reset() override;
    [[nodiscard]] size_t Count() const noexcept override;
    void ToggleTempSpellOff() override;

private:
    // Processing
    bool ProcessModifier(wchar_t c);
    bool ProcessTone(wchar_t c);
    void ProcessChar(wchar_t c, size_t rawIdx);
    
    // Character composition
    wchar_t ComposeChar(const CharState& state) const;
    CharState* FindToneTarget();
    CharState* FindToneTargetClassic();
    CharState* FindToneTargetModern();
    CharState* FindToneTargetImpl(const uint8_t table[6][6], bool checkTriphthongs);
    
    // Spell check
    void UpdateSpellState();

    // State
    std::vector<CharState> states_;
    std::wstring rawInput_;
    TypingConfig config_;
    bool spellCheckDisabled_ = false;
    bool tempSpellOff_ = false;          // true when user toggled temp spell bypass via Ctrl
    bool quickConsonantOnly_ = false;    // true when buffer is only quick consonant expansion
    bool quickConsonantEscaped_ = false; // true after backspace undoes quick consonant
    size_t quickConsonantIdx_ = SIZE_MAX; // states_ index of quick consonant result char
};

}  // namespace Vni
}  // namespace NextKey
