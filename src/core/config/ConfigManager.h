// NexusKey - Configuration Manager
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>
#include <optional>
#include "core/TypingConfig.h"

namespace NextKey {

/// Manages loading and saving of configuration from TOML file
/// FR5: Load configuration from TOML at word boundary
/// FR8: Engine works with compiled defaults when config missing
class ConfigManager {
public:
    /// Load config from file, returns nullopt if file doesn't exist or is invalid
    static std::optional<TypingConfig> LoadFromFile(const std::wstring& path);
    
    /// Save config to file, returns true on success
    static bool SaveToFile(const std::wstring& path, const TypingConfig& config);
    
    /// Get the config file path (exe dir or %APPDATA% fallback)
    static std::wstring GetConfigPath();
    
    /// Load config with automatic path resolution
    /// Returns compiled defaults if no config file found
    static TypingConfig LoadOrDefault();
    
private:
    static std::wstring GetExeDirectory();
    static std::wstring GetAppDataDirectory();
    static bool DirectoryWritable(const std::wstring& path);
};

}  // namespace NextKey
