// VKey browser native-messaging protocol
// SPDX-License-Identifier: GPL-3.0-only

#include "NativeMessaging.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace NextKey::BrowserHost {
namespace {

void SkipSpace(std::string_view text, std::size_t& pos) noexcept {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
}

bool ReadString(std::string_view text, std::size_t& pos, std::string& out) {
    SkipSpace(text, pos);
    if (pos >= text.size() || text[pos++] != '"') return false;
    const std::size_t begin = pos;
    while (pos < text.size() && text[pos] != '"') {
        // Protocol values are lower-case ASCII. Reject escapes/control chars
        // instead of growing a general-purpose JSON implementation here.
        if (text[pos] == '\\' || static_cast<unsigned char>(text[pos]) < 0x20) return false;
        ++pos;
    }
    if (pos >= text.size()) return false;
    out.assign(text.substr(begin, pos - begin));
    ++pos;
    return true;
}

bool ReadLiteral(std::string_view text, std::size_t& pos, std::string_view literal) noexcept {
    SkipSpace(text, pos);
    if (text.substr(pos, literal.size()) != literal) return false;
    pos += literal.size();
    return true;
}

bool IsAllowedBrowser(std::string_view value) noexcept {
    constexpr std::string_view browsers[] = {
        "chrome.exe", "msedge.exe", "brave.exe", "firefox.exe", "vivaldi.exe",
        "opera.exe"
    };
    return std::find(std::begin(browsers), std::end(browsers), value) != std::end(browsers);
}

bool IsSafeHostname(std::string_view value) noexcept {
    if (value.size() >= 256) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
            || ch == '.' || ch == '-' || ch == ':';
    });
}

} // namespace

bool ParseNativeMessage(std::string_view json, NativeMessage& out,
                        std::string& error) noexcept {
    try {
        if (json.empty() || json.size() > kMaxNativeMessageBytes) {
            error = "message_size";
            return false;
        }
        std::size_t pos = 0;
        SkipSpace(json, pos);
        if (pos >= json.size() || json[pos++] != '{') {
            error = "json_object";
            return false;
        }

        bool haveProtocol = false, haveBrowser = false, haveHostname = false;
        bool haveRoute = false, haveMode = false, haveFocused = false;
        NativeMessage parsed{};
        while (true) {
            SkipSpace(json, pos);
            if (pos < json.size() && json[pos] == '}') { ++pos; break; }
            std::string key;
            if (!ReadString(json, pos, key)) { error = "json_key"; return false; }
            SkipSpace(json, pos);
            if (pos >= json.size() || json[pos++] != ':') { error = "json_colon"; return false; }

            if (key == "protocol") {
                if (haveProtocol) { error = "duplicate_field"; return false; }
                SkipSpace(json, pos);
                const std::size_t begin = pos;
                while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;
                int value = 0;
                const auto result = std::from_chars(json.data() + begin, json.data() + pos, value);
                if (result.ec != std::errc{} || (value != 1 && value != 2)) {
                    error = "protocol";
                    return false;
                }
                parsed.protocol = value;
                haveProtocol = true;
            } else if (key == "browser") {
                if (haveBrowser) { error = "duplicate_field"; return false; }
                if (!ReadString(json, pos, parsed.browserExe)) { error = "browser"; return false; }
                haveBrowser = true;
            } else if (key == "hostname") {
                if (haveHostname) { error = "duplicate_field"; return false; }
                if (!ReadString(json, pos, parsed.hostname)) { error = "hostname"; return false; }
                haveHostname = true;
            } else if (key == "route") {
                if (haveRoute) { error = "duplicate_field"; return false; }
                std::string route;
                if (!ReadString(json, pos, route)) { error = "route"; return false; }
                if (route == "default") parsed.route = BrowserRoute::Default;
                else if (route == "english") parsed.route = BrowserRoute::ForceEnglish;
                else if (route == "tsf") parsed.route = BrowserRoute::ForceTsf;
                else { error = "route"; return false; }
                haveRoute = true;
            } else if (key == "mode") {
                if (haveMode) { error = "duplicate_field"; return false; }
                std::string mode;
                if (!ReadString(json, pos, mode)) { error = "mode"; return false; }
                if (mode == "default") parsed.mode = BrowserMode::Default;
                else if (mode == "vietnamese") parsed.mode = BrowserMode::Vietnamese;
                else if (mode == "english") parsed.mode = BrowserMode::English;
                else { error = "mode"; return false; }
                haveMode = true;
            } else if (key == "focused") {
                if (haveFocused) { error = "duplicate_field"; return false; }
                if (ReadLiteral(json, pos, "true")) parsed.focused = true;
                else if (ReadLiteral(json, pos, "false")) parsed.focused = false;
                else { error = "focused"; return false; }
                haveFocused = true;
            } else {
                // Extensions under our fixed allow-list should not need extra
                // fields. Rejecting unknown data keeps the native boundary tiny.
                error = "unknown_field";
                return false;
            }

            SkipSpace(json, pos);
            if (pos < json.size() && json[pos] == ',') { ++pos; continue; }
            if (pos < json.size() && json[pos] == '}') { ++pos; break; }
            error = "json_separator";
            return false;
        }
        SkipSpace(json, pos);
        if (pos != json.size() || !haveProtocol || !haveBrowser || !haveHostname
            || !haveRoute || !haveFocused || (parsed.protocol == 2 && !haveMode)
            || (parsed.protocol == 1 && haveMode)) {
            error = "required_fields";
            return false;
        }
        if (!IsAllowedBrowser(parsed.browserExe)) { error = "browser_allowlist"; return false; }
        if (!IsSafeHostname(parsed.hostname)) { error = "hostname_chars"; return false; }
        if (parsed.focused && parsed.hostname.empty()) { error = "hostname_empty"; return false; }
        out = std::move(parsed);
        error.clear();
        return true;
    } catch (...) {
        error = "parse_exception";
        return false;
    }
}

bool TryReadModeEvent(const BrowserContextState& state,
                      std::uint32_t ownerProcessId,
                      std::uint64_t ownerNonce,
                      std::uint32_t lastSequence,
                      NativeModeEvent& out) noexcept {
    if (!state.HasValidHeader() || state.modeEventSequence == 0
        || state.modeEventSequence == lastSequence
        || state.modeEventOwnerProcessId != ownerProcessId
        || state.modeEventOwnerNonce != ownerNonce
        || (state.modeEventMode != BrowserMode::Vietnamese
            && state.modeEventMode != BrowserMode::English))
        return false;
    const std::string_view hostname{state.modeEventHostname};
    if (hostname.empty() || !IsSafeHostname(hostname)) return false;
    out.sequence = state.modeEventSequence;
    out.hostname.assign(hostname);
    out.mode = state.modeEventMode;
    return true;
}

std::string SerializeModeEvent(const NativeModeEvent& event) {
    const char* mode = event.mode == BrowserMode::Vietnamese
        ? "vietnamese"
        : "english";
    return "{\"ok\":true,\"protocol\":2,\"event\":\"mode-changed\",\"hostname\":\""
        + event.hostname + "\",\"mode\":\"" + mode + "\"}";
}

} // namespace NextKey::BrowserHost
