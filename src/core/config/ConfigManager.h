// NexusKey - Configuration Manager
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <vector>
#include "TypingConfig.h"
#include "core/UIConfig.h"
#include "core/SystemConfig.h"

namespace NextKey {

/// Per-app override settings (encoding + input method)
struct AppOverrideEntry {
    int8_t inputMethod = -1;       // -1=inherit global, 0=Telex, 1=VNI, 2=SimpleTelex
    int8_t encodingOverride = -1;  // -1=inherit global, 0-4=CodeTable value
};

/// Manages loading and saving of configuration from TOML file
/// FR5: Load configuration from TOML at word boundary
/// FR8: Engine works with compiled defaults when config missing
class ConfigManager {
public:
    /// Load config from file, returns nullopt if file doesn't exist or is invalid
    [[nodiscard]] static std::optional<TypingConfig> LoadFromFile(const std::wstring& path);

    /// Save config to file, returns true on success
    [[nodiscard]] static bool SaveToFile(const std::wstring& path, const TypingConfig& config);

    /// Get the config file path (exe dir or %APPDATA% fallback)
    [[nodiscard]] static std::wstring GetConfigPath();

    /// Load config with automatic path resolution
    /// Returns compiled defaults if no config file found
    [[nodiscard]] static TypingConfig LoadOrDefault();

    /// Load UI config from file, returns nullopt if not found
    [[nodiscard]] static std::optional<UIConfig> LoadUIConfig(const std::wstring& path);

    /// Save UI config to file (merges with existing config)
    [[nodiscard]] static bool SaveUIConfig(const std::wstring& path, const UIConfig& config);

    /// Load UI config with automatic path resolution
    [[nodiscard]] static UIConfig LoadUIConfigOrDefault();

    /// Load hotkey config from file
    [[nodiscard]] static std::optional<HotkeyConfig> LoadHotkeyConfig(const std::wstring& path);

    /// Save hotkey config to file (merges with existing config)
    [[nodiscard]] static bool SaveHotkeyConfig(const std::wstring& path, const HotkeyConfig& config);

    /// Load hotkey config with automatic path resolution
    [[nodiscard]] static HotkeyConfig LoadHotkeyConfigOrDefault();

    /// Load excluded apps list from config (hard exclusion — block toggle)
    [[nodiscard]] static std::vector<std::wstring> LoadExcludedApps(const std::wstring& path);

    /// Load soft-excluded apps list from config (default English on focus, allow toggle)
    [[nodiscard]] static std::vector<std::wstring> LoadSoftExcludedApps(const std::wstring& path);

    /// Save excluded apps lists to config (both hard + soft, merges with existing)
    [[nodiscard]] static bool SaveExcludedApps(const std::wstring& path,
                                                const std::vector<std::wstring>& hardApps,
                                                const std::vector<std::wstring>& softApps);

    /// Load TSF apps list from config (apps that use TSF engine instead of hook)
    [[nodiscard]] static std::vector<std::wstring> LoadTsfApps(const std::wstring& path);

    /// Save TSF apps list to config (merges with existing)
    [[nodiscard]] static bool SaveTsfApps(const std::wstring& path, const std::vector<std::wstring>& apps);

    /// Load system config from file
    [[nodiscard]] static std::optional<SystemConfig> LoadSystemConfig(const std::wstring& path);

    /// Save system config to file (merges with existing config)
    [[nodiscard]] static bool SaveSystemConfig(const std::wstring& path, const SystemConfig& config);

    /// Load system config with automatic path resolution
    [[nodiscard]] static SystemConfig LoadSystemConfigOrDefault();

    /// Load convert config from file
    [[nodiscard]] static std::optional<ConvertConfig> LoadConvertConfig(const std::wstring& path);

    /// Save convert config to file (merges with existing config)
    [[nodiscard]] static bool SaveConvertConfig(const std::wstring& path, const ConvertConfig& config);

    /// Load convert config with automatic path resolution
    [[nodiscard]] static ConvertConfig LoadConvertConfigOrDefault();

    /// Load macro table (shorthand → expansion) from config
    [[nodiscard]] static std::unordered_map<std::wstring, std::wstring> LoadMacros(const std::wstring& path);

    /// Save macro table to config (merges with existing)
    [[nodiscard]] static bool SaveMacros(const std::wstring& path,
                                          const std::unordered_map<std::wstring, std::wstring>& macros);

    /// Load per-app override entries (encoding + behavior overrides)
    [[nodiscard]] static std::unordered_map<std::wstring, AppOverrideEntry> LoadAppOverrides(const std::wstring& path);

    /// Save per-app override entries
    [[nodiscard]] static bool SaveAppOverrides(const std::wstring& path,
                                                const std::unordered_map<std::wstring, AppOverrideEntry>& entries);

private:
    static std::wstring GetExeDirectory();
    static std::wstring GetAppDataDirectory();
    static bool DirectoryWritable(const std::wstring& path);
};

}  // namespace NextKey
