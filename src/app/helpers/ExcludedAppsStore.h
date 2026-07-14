// VKey - Excluded / Forced-VN app list store
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pure data logic for the per-app E/V list, shared by the Sciter dialog
// (ExcludedAppsDialog) and the Classic dialog (ClassicExcludedAppsDialog).
// UI reactions (message boxes, list controls, prompts) stay per call site —
// same split as the ParseConfigLines note in AppHelpers.h.

#pragma once

#include "core/config/ConfigManager.h"
#include "core/WinStrings.h"
#include "helpers/AppHelpers.h"

#include <algorithm>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace NextKey {

/// (exe basename, lowercase, mode). Mode values match the dialogs'
/// kModeE / kModeV: 0 = E (excluded/English), 1 = force-Vietnamese.
using TaggedAppList = std::vector<std::pair<std::wstring, int>>;

inline constexpr int kTaggedModeE = 0;
inline constexpr int kTaggedModeV = 1;

/// True for any of VKey's own exe names — never allowed in app lists
/// (Sciter + Lite + Classic; VKeyLite ships with OUTPUT_NAME=VKeyClassic).
[[nodiscard]] inline bool IsVKeyOwnExe(const std::wstring& lowerName) noexcept {
    return lowerName == L"vkey.exe" || lowerName == L"vkeylite.exe" ||
           lowerName == L"vkeyclassic.exe";
}

/// Load excluded (E) + forced-VN (V) apps as one tagged list, disjoint by
/// name (excluded wins — mirror ConfigSnapshotBuilder), sorted by name.
[[nodiscard]] inline TaggedAppList LoadTaggedAppList() {
    const auto path = ConfigManager::GetConfigPath();
    TaggedAppList list;
    for (auto& e : ConfigManager::LoadAllExcludedApps(path)) {
        list.emplace_back(std::move(e), kTaggedModeE);
    }
    for (auto& v : ConfigManager::LoadForcedVnApps(path)) {
        bool dup = false;
        for (auto& a : list) { if (a.first == v) { dup = true; break; } }
        if (!dup) list.emplace_back(std::move(v), kTaggedModeV);
    }
    std::sort(list.begin(), list.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return list;
}

/// Partition the tagged list back into the two TOML arrays, save both,
/// and signal the running engine.
inline void SaveTaggedAppList(const TaggedAppList& list) {
    std::vector<std::wstring> excluded, forcedVn;
    for (auto& app : list) {
        (app.second == kTaggedModeV ? forcedVn : excluded).push_back(app.first);
    }
    const auto path = ConfigManager::GetConfigPath();
    (void)ConfigManager::SaveExcludedApps(path, excluded);
    (void)ConfigManager::SaveForcedVnApps(path, forcedVn);
    SignalConfigChange();
}

/// Merge one import-file line into the list. Per-app mode tag (D4):
/// "name|V" → force-V, plain "name" → E — untagged lines stay E so files
/// exported before this feature import unchanged. Dedup by name → update mode.
inline void MergeTaggedAppLine(TaggedAppList& list, const std::string& utf8Line) {
    std::wstring entry = Utf8ToWide(utf8Line);
    int mode = kTaggedModeE;
    auto bar = entry.find_last_of(L'|');
    if (bar != std::wstring::npos) {
        if (ToLowerAscii(entry.substr(bar + 1)) == L"v") mode = kTaggedModeV;
        entry = entry.substr(0, bar);
    }
    std::wstring lower = ToLowerAscii(entry);
    if (lower.empty()) return;
    for (auto& a : list) {
        if (a.first == lower) { a.second = mode; return; }
    }
    list.emplace_back(lower, mode);
}

/// Write the export-file format: comment header + sorted "name" / "name|V" lines.
inline void WriteTaggedAppList(std::ostream& out, TaggedAppList list) {
    out << ";VKey Excluded Apps (name = English/excluded, name|V = force Vietnamese)\n";
    std::sort(list.begin(), list.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& app : list) {
        out << WideToUtf8(app.first);
        if (app.second == kTaggedModeV) out << "|V";
        out << "\n";
    }
}

}  // namespace NextKey
