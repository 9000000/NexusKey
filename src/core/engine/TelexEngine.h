// NexusKey - Telex Engine Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "IInputEngine.h"
#include "SpellChecker.h"
#include "core/config/TypingConfig.h"
#include <vector>
#include <string>

namespace NextKey {
namespace Telex {

/// Modifier type for Vietnamese vowels
enum class Modifier : uint8_t {
    None,       // a e i o u y
    Circumflex, // â ê ô
    Breve,      // ă
    Horn        // ơ ư
};

/// Tone type for Vietnamese
enum class Tone : uint8_t {
    None,   // no tone
    Acute,  // sắc (s)
    Grave,  // huyền (f)
    Hook,   // hỏi (r)
    Tilde,  // ngã (x)
    Dot     // nặng (j)
};

/// Internal state for each character - THE CORE ABSTRACTION
/// IME converts keys into STATE; letters are merely a consequence.
struct CharState {
    wchar_t base = 0;               // Base letter (lowercase): a e i o u y d or consonant
    Modifier mod = Modifier::None;  // Vowel modifier
    Tone tone = Tone::None;         // Tone mark
    bool isUpper = false;           // Preserve original case
    bool synthetic = false;         // True if created by P8 standalone 'w' (not a real keystroke)
    size_t rawIdx = 0;              // rawInput_ index when this state was created (for backspace sync)
    size_t toneRawIdx = SIZE_MAX;   // rawInput_ index of consumed tone key (for escape removal)

    [[nodiscard]] constexpr bool IsVowel() const noexcept {
        return base == L'a' || base == L'e' || base == L'i' ||
               base == L'o' || base == L'u' || base == L'y';
    }
    [[nodiscard]] constexpr bool CanHaveMod() const noexcept {
        // a -> â, ă; e -> ê; o -> ô, ơ; u -> ư
        return base == L'a' || base == L'e' || base == L'o' || base == L'u';
    }
    [[nodiscard]] constexpr bool IsD() const noexcept { return base == L'd'; }
    [[nodiscard]] constexpr bool IsEmpty() const noexcept { return base == 0; }
};

/// Telex engine states
enum class TelexStates {
    Valid,       // Valid Vietnamese word
    Invalid,     // Not a valid Vietnamese word
    Committed    // After commit
};

/// Telex input method engine - STATE-BASED ARCHITECTURE
///
/// Key principle: IME tracks STATE, not precomposed Unicode.
/// Unicode is only produced when Peek() or Commit() is called.
///
/// This makes:
/// - Backspace clean (pop entire char state)
/// - Escape clean (retype tone/mod to clear)
/// - No reverse maps needed
/// - TSF composition state always in sync
class TelexEngine : public IInputEngine {
public:
    TelexEngine() : TelexEngine(TypingConfig{}) {}
    explicit TelexEngine(const TypingConfig& config);
    ~TelexEngine() override = default;

    TelexEngine(const TelexEngine&) = delete;
    TelexEngine& operator=(const TelexEngine&) = delete;

    // IInputEngine implementation
    void PushChar(wchar_t c) override;
    void Backspace() override;
    [[nodiscard]] std::wstring Peek() const override;
    [[nodiscard]] std::wstring Commit() override;
    void Reset() override;
    [[nodiscard]] size_t Count() const noexcept override;
    void ToggleTempSpellOff() override;

    [[nodiscard]] TelexStates GetState() const noexcept { return state_; }

private:
    // Input processing
    bool ProcessTone(wchar_t c);      // s, f, r, x, j
    bool ProcessClearTone();          // z — remove existing tone
    bool ProcessModifier(wchar_t c);  // w, [], aa, ee, oo, dd
    void ProcessChar(wchar_t c);      // Regular character

    // Find target for tone/modifier application
    size_t FindToneTarget() const;
    size_t FindToneTargetClassic() const;
    size_t FindToneTargetModern() const;

    // Auto ươ: convert 'uơ' to 'ươ' when followed by another character
    void ApplyAutoUO();

    // Move tone to horn vowel when horn modifier is added
    // Example: "cuả" + w → "cửa" (tone moves from a to ư)
    void RelocateToneToHornVowel();

    // W-Modifier processing (explicit priority order)
    bool ProcessWModifier(wchar_t c);

    // D-Modifier processing (dd → đ)
    bool ProcessDModifier(wchar_t c);

    // QU Cluster detection (qu is consonant cluster, u is not vowel)
    bool IsInQUCluster() const;

    // Compose single CharState to Unicode
    static wchar_t Compose(const CharState& s);

    // Compose all states to string
    std::wstring ComposeAll() const;

    // Remove consumed raw entry and adjust all indices
    void EraseConsumedRaw(size_t idx);

    // Spell check: validate syllable structure after each keystroke
    void UpdateSpellState();

    // State
    std::vector<CharState> states_;   // Internal state buffer
    std::vector<wchar_t> rawInput_;   // Raw keys for escape
    TypingConfig config_;
    TelexStates state_ = TelexStates::Valid;
    bool spellCheckDisabled_ = false; // true when buffer is invalid syllable
    bool tempSpellOff_ = false;       // true when user toggled temp spell bypass via Ctrl
};

}  // namespace Telex
}  // namespace NextKey
