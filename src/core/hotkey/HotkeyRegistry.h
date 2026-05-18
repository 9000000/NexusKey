// VKey — Unified hotkey registry (single source of truth for user-rebindable
// hotkey triggers). Pure Linux-portable: VK codes treated as plain integers.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace toml::inline v3 {
class array;
}  // namespace toml

namespace NextKey {

/// Action that fires when a Trigger matches.
enum class Intent : uint8_t {
    CancelComposition = 0,  // End current composition + restore raw keys (Esc default)
    SkipMacro         = 1,  // Skip macro expansion for the next word (Esc default)
    ToggleEnabled     = 2,  // Toggle global ENABLED flag (Ctrl alone / 2×Alt default)
};

/// Modifier bitmask. Defined independently of Win32 MOD_* constants so the
/// type is unit-testable on Linux.
inline constexpr uint32_t MOD_CTRL  = 0x01;
inline constexpr uint32_t MOD_SHIFT = 0x02;
inline constexpr uint32_t MOD_ALT   = 0x04;
inline constexpr uint32_t MOD_WIN   = 0x08;

/// A single input pattern that fires an Intent. Covers:
///   - Single tap:       `{vk, mods=0}`
///   - Chord:            `{vk, mods=MOD_CTRL|MOD_SHIFT}`
///   - Double-tap:       `{vk, mods=0, doubleTap=true}`
///   - Modifier-alone:   `{vk=VK_CONTROL|VK_MENU|VK_SHIFT|VK_LWIN|VK_RWIN, mods=0}`
///                       (Matches() fires on keyUp; caller must have verified
///                        no other key was pressed during the modifier window.)
struct Trigger {
    uint32_t vk        = 0;
    uint32_t mods      = 0;
    bool     doubleTap = false;

    bool operator==(const Trigger&) const noexcept = default;
};

/// Registry of (Intent → list of Triggers). Owns dispatch logic.
class HotkeyRegistry {
public:
    /// True iff `(vk, mods, isDoubleTap, keyUp)` matches any Trigger for `intent`.
    ///
    /// `keyUp` distinguishes DOWN vs UP events. Tap/Chord/DoubleTap fire on DOWN
    /// (`keyUp == false`). Modifier-alone fires on UP (`keyUp == true`).
    [[nodiscard]] bool Matches(Intent   intent,
                               uint32_t vk,
                               uint32_t mods,
                               bool     isDoubleTap,
                               bool     keyUp) const noexcept;

    /// Load `[[hotkey]]` array entries. Unknown intent strings are skipped.
    /// Replaces existing triggers — caller may want Defaults() first.
    void Load(const toml::array& cfg);

    /// Serialize current triggers into a TOML array (caller wraps as `[[hotkey]]`).
    void Save(toml::array& cfg) const;

    /// Factory bindings — restores the 3 legacy NexusKey hotkeys:
    ///   - Esc                → CancelComposition
    ///   - Esc                → SkipMacro
    ///   - Ctrl alone         → ToggleEnabled
    ///   - 2×Alt              → ToggleEnabled
    [[nodiscard]] static HotkeyRegistry Defaults();

    /// Migration helper — build a registry from the three legacy TypingConfig
    /// fields. Used on first launch when no `[[hotkeys]]` section exists in
    /// config.toml. Each flag-off case maps to "no trigger" for that intent
    /// (preserves the user's prior disable choice).
    ///
    /// `tempOffMethodValue` encodes the legacy `TempOffMethod` enum:
    ///   0 = None  → no ToggleEnabled trigger
    ///   1 = DupAlt → 2×Alt → ToggleEnabled
    ///   2 = Ctrl  → Ctrl-alone → ToggleEnabled
    [[nodiscard]] static HotkeyRegistry FromLegacyFields(
        bool    escRestoreRawEnabled,
        bool    tempOffMacroByEsc,
        uint8_t tempOffMethodValue) noexcept;

    /// Direct trigger list access for UI rendering.
    [[nodiscard]] const std::vector<Trigger>& TriggersFor(Intent intent) const noexcept;

    /// Add a trigger for `intent`. Used by UI capture mode and Load().
    void AddTrigger(Intent intent, Trigger trigger);

    /// Remove all triggers (test helper / "Restore defaults" UI).
    void Clear() noexcept;

private:
    std::unordered_map<Intent, std::vector<Trigger>> triggers_;
};

/// True if `vk` is one of the 5 modifier keys (Ctrl/Shift/Alt/LWin/RWin).
[[nodiscard]] bool IsModifierKey(uint32_t vk) noexcept;

}  // namespace NextKey
