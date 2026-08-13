// VKey - Rust engine adapter
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only

#include "RustInputEngine.h"

#include "RustEngineLoader.h"

#include "vkey_engine.h"  // vendored C ABI (extern/vkey_engine/include)

#include <array>
#include <cstdint>
#include <utility>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE  // dladdr
#  endif
#  include <dlfcn.h>
#endif

namespace NextKey {
namespace {

// Resolved entry points of the prebuilt library. Loaded once, lazily.
struct EngineApi {
    VKeyEngine* (*create)(uint32_t, uint32_t) = nullptr;
    void (*destroy)(VKeyEngine*) = nullptr;
    void (*reset)(VKeyEngine*) = nullptr;
    void (*push_char)(VKeyEngine*, uint32_t) = nullptr;
    void (*backspace)(VKeyEngine*) = nullptr;
    size_t (*peek_utf16)(const VKeyEngine*, uint16_t*, size_t) = nullptr;
    size_t (*commit_utf16)(VKeyEngine*, uint16_t*, size_t) = nullptr;
    size_t (*count)(const VKeyEngine*) = nullptr;
    uint32_t (*abi_version)(void) = nullptr;
    uint32_t (*runtime_status)(void) = nullptr;
    // ABI v2 host query surface.
    bool (*is_english_word)(const VKeyEngine*) = nullptr;
    bool (*is_tone_escaped)(const VKeyEngine*) = nullptr;
    bool (*has_active_quick_consonant)(const VKeyEngine*) = nullptr;
    size_t (*peek_raw_utf16)(const VKeyEngine*, uint16_t*, size_t) = nullptr;
    bool (*seed_text_utf16)(VKeyEngine*, const uint16_t*, size_t) = nullptr;
    // ABI v3 host query surface.
    bool (*last_commit_was_corrected)(const VKeyEngine*) = nullptr;
    // ABI v4: user-defined custom keymap.
    void (*set_custom_keymap)(VKeyEngine*, const uint8_t*, size_t) = nullptr;
    // ABI v5: spell-check exclusions. Process-global (no engine handle) --
    // must be called before create() to affect the engine being constructed.
    bool (*set_spell_exclusions_utf16)(const uint16_t*, size_t) = nullptr;
    // ABI v7: bounded committed-word replay eligibility.
    bool (*should_replay_key_after_raw_utf16)(
        const VKeyEngine*, const uint16_t*, size_t, uint32_t) = nullptr;
    bool ok = false;
    std::wstring reason;  // diagnostic when !ok; empty when ok
};

template <typename Fn>
Fn Resolve(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<Fn>(::GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return reinterpret_cast<Fn>(::dlsym(lib, name));
#endif
}

const EngineApi& Api() {
    // Trust verification and symbol resolution happen once per process, never per key.
    static const EngineApi api = [] {
        EngineApi a;
        RustEngineLibraryResult loaded = LoadRustEngineLibrary();
        if (!loaded.handle) {
            a.reason = std::move(loaded.reason);
            return a;
        }
        void* lib = loaded.handle;
        a.create = Resolve<decltype(a.create)>(lib, "vkey_engine_create");
        a.destroy = Resolve<decltype(a.destroy)>(lib, "vkey_engine_destroy");
        a.reset = Resolve<decltype(a.reset)>(lib, "vkey_engine_reset");
        a.push_char = Resolve<decltype(a.push_char)>(lib, "vkey_engine_push_char");
        a.backspace = Resolve<decltype(a.backspace)>(lib, "vkey_engine_backspace");
        a.peek_utf16 = Resolve<decltype(a.peek_utf16)>(lib, "vkey_engine_peek_utf16");
        a.commit_utf16 = Resolve<decltype(a.commit_utf16)>(lib, "vkey_engine_commit_utf16");
        a.count = Resolve<decltype(a.count)>(lib, "vkey_engine_count");
        a.abi_version = Resolve<decltype(a.abi_version)>(lib, "vkey_engine_abi_version");
        a.runtime_status =
            Resolve<decltype(a.runtime_status)>(lib, "vkey_engine_runtime_status");
        a.is_english_word =
            Resolve<decltype(a.is_english_word)>(lib, "vkey_engine_is_english_word");
        a.is_tone_escaped =
            Resolve<decltype(a.is_tone_escaped)>(lib, "vkey_engine_is_tone_escaped");
        a.has_active_quick_consonant = Resolve<decltype(a.has_active_quick_consonant)>(
            lib, "vkey_engine_has_active_quick_consonant");
        a.peek_raw_utf16 =
            Resolve<decltype(a.peek_raw_utf16)>(lib, "vkey_engine_peek_raw_utf16");
        a.seed_text_utf16 =
            Resolve<decltype(a.seed_text_utf16)>(lib, "vkey_engine_seed_text_utf16");
        a.last_commit_was_corrected = Resolve<decltype(a.last_commit_was_corrected)>(
            lib, "vkey_engine_last_commit_was_corrected");
        a.set_custom_keymap = Resolve<decltype(a.set_custom_keymap)>(
            lib, "vkey_engine_set_custom_keymap");
        a.set_spell_exclusions_utf16 = Resolve<decltype(a.set_spell_exclusions_utf16)>(
            lib, "vkey_engine_set_spell_exclusions_utf16");
        a.should_replay_key_after_raw_utf16 =
            Resolve<decltype(a.should_replay_key_after_raw_utf16)>(
                lib, "vkey_engine_should_replay_key_after_raw_utf16");
        const bool symbolsResolved =
            a.create && a.destroy && a.reset && a.push_char && a.backspace &&
            a.peek_utf16 && a.commit_utf16 && a.count && a.abi_version && a.runtime_status &&
            a.is_english_word && a.is_tone_escaped && a.has_active_quick_consonant &&
            a.peek_raw_utf16 && a.seed_text_utf16 && a.last_commit_was_corrected &&
            a.set_custom_keymap && a.set_spell_exclusions_utf16 &&
            (VKEY_ENGINE_ABI_VERSION < 7u || a.should_replay_key_after_raw_utf16);
        if (!symbolsResolved) {
            CloseRustEngineLibrary(lib);
            a.reason = L"vkey_engine library is missing a required exported symbol";
            return a;
        }
        // A floor, not an equality: this binary must be able to load an engine
        // newer than the header it was built against, otherwise a VKeyTSF.dll left
        // behind by a deferred update refuses the engine installed beside it and
        // drops to the C++ engine with no user-visible sign. Safe because the ABI
        // only ever gains symbols, and every symbol this build needs was already
        // resolved by name above.
        const uint32_t libVersion = a.abi_version();
        if (libVersion < VKEY_ENGINE_ABI_VERSION) {
            CloseRustEngineLibrary(lib);
            a.reason = L"vkey_engine ABI is older than this build (lib=" + std::to_wstring(libVersion) +
                        L", needs>=" + std::to_wstring(VKEY_ENGINE_ABI_VERSION) + L")";
            return a;
        }
        const uint32_t runtimeStatus = a.runtime_status();
        if (runtimeStatus != VKEY_ENGINE_RUNTIME_OK) {
            CloseRustEngineLibrary(lib);
            a.reason = L"vkey_engine runtime identity check failed (status=" +
                       std::to_wstring(runtimeStatus) + L")";
            return a;
        }
        a.ok = true;
        return a;
    }();
    return api;
}

uint32_t MapMethod(InputMethod method) {
    switch (method) {
        case InputMethod::VNI:
            return VKEY_METHOD_VNI;
        case InputMethod::SimpleTelex:
            return VKEY_METHOD_SIMPLE_TELEX;
        case InputMethod::Combined:
            return VKEY_METHOD_COMBINED;
        case InputMethod::UserDefined:
            return VKEY_METHOD_USER_DEFINED;
        case InputMethod::Telex:
        default:
            return VKEY_METHOD_TELEX;
    }
}

uint32_t MapFeatures(const TypingConfig& c) {
    uint32_t f = 0;
    if (c.modernOrtho) f |= VKEY_FEAT_MODERN_ORTHOGRAPHY;
    if (c.quickStartConsonant) f |= VKEY_FEAT_QUICK_START_CONSONANT;
    if (c.quickConsonant) f |= VKEY_FEAT_QUICK_CONSONANT;
    if (c.quickEndConsonant) f |= VKEY_FEAT_QUICK_END_CONSONANT;
    if (c.spellCheckEnabled) f |= VKEY_FEAT_SPELL_CHECK;
    if (c.spellSuggestEnabled && c.autoRestoreEnabled) f |= VKEY_FEAT_SPELL_SUGGEST;
    if (c.allowEnglishBypass) f |= VKEY_FEAT_ALLOW_ENGLISH_BYPASS;
    if (c.allowZwjf) f |= VKEY_FEAT_ALLOW_ZWJF;
    return f;
}

// Bounded engine: the active composition never exceeds this many UTF-16 units.
constexpr size_t kTextCap = 256;

struct Utf16Text {
    std::array<uint16_t, kTextCap> units{};
    size_t length = 0;
    bool valid = true;
};

// Stack-only encoding keeps hook-path UTF-16 conversion allocation-free.
Utf16Text ToUtf16(std::wstring_view text) {
    Utf16Text out;
    for (const wchar_t wc : text) {
        const uint32_t c = static_cast<uint32_t>(wc);
        if (c <= 0xFFFF) {
            if (out.length == out.units.size()) {
                out.valid = false;
                break;
            }
            out.units[out.length++] = static_cast<uint16_t>(c);
        } else if (c <= 0x10FFFF) {
            if (out.length + 2 > out.units.size()) {
                out.valid = false;
                break;
            }
            const uint32_t v = c - 0x10000;
            out.units[out.length++] = static_cast<uint16_t>(0xD800 + (v >> 10));
            out.units[out.length++] = static_cast<uint16_t>(0xDC00 + (v & 0x3FF));
        } else {
            out.valid = false;
            break;
        }
    }
    return out;
}

}  // namespace

bool RustInputEngine::LibraryAvailable() {
    return Api().ok;
}

std::wstring RustInputEngine::UnavailableReason() {
    return Api().reason;
}

RustInputEngine::RustInputEngine(const TypingConfig& config) {
    const EngineApi& api = Api();
    if (api.ok) {
        // Process-global spell exclusions must be set before create() -- the
        // Rust side snapshots the active set into the engine at creation time.
        if (config.spellExclusions.empty()) {
            api.set_spell_exclusions_utf16(nullptr, 0);
        } else {
            // Concatenate exclusions as newline-delimited UTF-16 text.
            std::wstring exclusions_text;
            for (size_t i = 0; i < config.spellExclusions.size(); ++i) {
                if (i > 0) {
                    exclusions_text += L'\n';
                }
                exclusions_text += config.spellExclusions[i];
            }
            api.set_spell_exclusions_utf16(
                reinterpret_cast<const uint16_t*>(exclusions_text.data()),
                exclusions_text.size());
        }
        handle_ = api.create(MapMethod(config.inputMethod), MapFeatures(config));
        if (handle_ && config.inputMethod == InputMethod::UserDefined) {
            api.set_custom_keymap(
                static_cast<VKeyEngine*>(handle_),
                reinterpret_cast<const uint8_t*>(config.customKeyMap.data()),
                config.customKeyMap.size());
        }
    }
    // Rule 11 (hook hot path): pre-size the buffers so the per-keystroke Refresh()
    // assigns reuse capacity and never allocate (composition is bounded to
    // kTextCap UTF-16 units).
    peek_.reserve(kTextCap);
    raw_.reserve(kTextCap);
}

RustInputEngine::~RustInputEngine() {
    if (handle_) {
        Api().destroy(static_cast<VKeyEngine*>(handle_));
    }
}

void RustInputEngine::Refresh() {
    peek_.clear();
    raw_.clear();
    count_ = 0;
    if (!handle_) {
        return;
    }
    const EngineApi& api = Api();
    auto* engine = static_cast<VKeyEngine*>(handle_);
    uint16_t buf[kTextCap];
    // Vietnamese output is entirely in the BMP, so one UTF-16 unit == one scalar:
    // the peek length already is the scalar count, so skip the extra count() FFI
    // round-trip on the per-keystroke hot path. Widening to wchar_t is correct on
    // both 16-bit (Windows) and 32-bit (Linux) wchar_t.
    const size_t needed = api.peek_utf16(engine, buf, kTextCap);
    count_ = needed;
    peek_.assign(buf, buf + (needed > kTextCap ? kTextCap : needed));
    // Cache the raw keystrokes so the noexcept PeekRawView() is a cheap view; the
    // hook hot path reads it every keystroke.
    const size_t rawNeeded = api.peek_raw_utf16(engine, buf, kTextCap);
    raw_.assign(buf, buf + (rawNeeded > kTextCap ? kTextCap : rawNeeded));
}

void RustInputEngine::PushChar(wchar_t c) {
    if (handle_) {
        Api().push_char(static_cast<VKeyEngine*>(handle_), static_cast<uint32_t>(c));
    }
    Refresh();
}

void RustInputEngine::Backspace() {
    if (handle_) {
        Api().backspace(static_cast<VKeyEngine*>(handle_));
    }
    Refresh();
}

std::wstring RustInputEngine::Commit() {
    std::wstring out;
    if (handle_) {
        uint16_t buf[kTextCap];
        size_t n = Api().commit_utf16(static_cast<VKeyEngine*>(handle_), buf, kTextCap);
        if (n > kTextCap) {
            n = kTextCap;
        }
        out.assign(buf, buf + n);
    }
    peek_.clear();
    raw_.clear();
    count_ = 0;
    return out;
}

void RustInputEngine::Reset() {
    if (handle_) {
        Api().reset(static_cast<VKeyEngine*>(handle_));
    }
    peek_.clear();
    raw_.clear();
    count_ = 0;
}

bool RustInputEngine::HasActiveQuickConsonant() const {
    return handle_ && Api().has_active_quick_consonant(static_cast<VKeyEngine*>(handle_));
}

bool RustInputEngine::IsEnglishWord() const {
    return handle_ && Api().is_english_word(static_cast<VKeyEngine*>(handle_));
}

bool RustInputEngine::IsToneEscaped() const {
    return handle_ && Api().is_tone_escaped(static_cast<VKeyEngine*>(handle_));
}

bool RustInputEngine::LastCommitWasCorrected() const {
    return handle_ && Api().last_commit_was_corrected(static_cast<VKeyEngine*>(handle_));
}

bool RustInputEngine::ShouldReplayCommittedKey(
    std::wstring_view rawInput, wchar_t key) const {
    const EngineApi& api = Api();
    if (!handle_ || !api.should_replay_key_after_raw_utf16) {
        return false;
    }
    const Utf16Text raw = ToUtf16(rawInput);
    return raw.valid && api.should_replay_key_after_raw_utf16(
        static_cast<const VKeyEngine*>(handle_), raw.units.data(), raw.length,
        static_cast<uint32_t>(key));
}

bool RustInputEngine::SeedFromText(const std::wstring& text) {
    if (!handle_) {
        Reset();
        return false;
    }
    // Literal restore: the engine reproduces the text so the English-word check
    // and continued typing/backspace work. Re-toning the restored glyphs is not
    // faithfully supported (needs a raw snapshot). On failure the engine is left
    // reset, per the IInputEngine contract.
    const Utf16Text units = ToUtf16(text);
    if (!units.valid) {
        Reset();
        return false;
    }
    const bool ok = Api().seed_text_utf16(
        static_cast<VKeyEngine*>(handle_), units.units.data(), units.length);
    Refresh();
    return ok;
}

}  // namespace NextKey
