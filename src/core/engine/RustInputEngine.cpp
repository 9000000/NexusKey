// VKey - Rust engine adapter
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial

#include "RustInputEngine.h"

#include "vkey_engine.h"  // vendored C ABI (extern/vkey_engine/include)

#include <cstdint>
#include <cstdlib>
#include <vector>

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

// Anchor whose address sits inside *this* module — used to locate the library
// next to whichever binary linked the adapter (VKeyApp.exe or, crucially, the
// in-process VKeyTSF.dll loaded into arbitrary host apps like Chrome/Word).
const int kModuleAnchor = 0;

// Absolute path of `vkey_engine` next to the module containing this code, or
// empty if it can't be determined. A bare LoadLibrary/dlopen would search the
// *host* process directory, which never holds the engine for the TSF DLL.
#if defined(_WIN32)
std::wstring SiblingLibraryPath() {
    HMODULE self = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&kModuleAnchor), &self)) {
        return {};
    }
    wchar_t path[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    std::wstring p(path, n);
    const size_t slash = p.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    p.resize(slash + 1);
    p += L"vkey_engine.dll";
    return p;
}
#else
std::string SiblingLibraryPath() {
    Dl_info info{};
    if (::dladdr(&kModuleAnchor, &info) == 0 || !info.dli_fname) {
        return {};
    }
    std::string p(info.dli_fname);
    const size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    p.resize(slash + 1);
    p += "libvkey_engine.so";
    return p;
}
#endif

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
    bool ok = false;
    std::wstring reason;  // diagnostic when !ok; empty when ok
};

void* OpenLibrary() {
    // Resolution order: explicit VKEY_ENGINE_LIB override (tests) → next to this
    // module (the only path that works for the in-process TSF DLL) → bare name
    // via the OS search path as a last resort.
#if defined(_WIN32)
    {
        wchar_t* path = nullptr;
        size_t len = 0;
        if (_wdupenv_s(&path, &len, L"VKEY_ENGINE_LIB") == 0 && path) {
            HMODULE h = ::LoadLibraryW(path);
            free(path);
            if (h) {
                return h;
            }
        }
    }
    if (const std::wstring sibling = SiblingLibraryPath(); !sibling.empty()) {
        if (HMODULE h = ::LoadLibraryW(sibling.c_str())) {
            return h;
        }
    }
    return ::LoadLibraryW(L"vkey_engine.dll");
#else
    if (const char* path = std::getenv("VKEY_ENGINE_LIB")) {
        if (void* h = ::dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
            return h;
        }
    }
    if (const std::string sibling = SiblingLibraryPath(); !sibling.empty()) {
        if (void* h = ::dlopen(sibling.c_str(), RTLD_NOW | RTLD_LOCAL)) {
            return h;
        }
    }
    return ::dlopen("libvkey_engine.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

template <typename Fn>
Fn Resolve(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<Fn>(::GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return reinterpret_cast<Fn>(::dlsym(lib, name));
#endif
}

const EngineApi& Api() {
    // C++11 "magic static": initialized once, thread-safe.
    static const EngineApi api = [] {
        EngineApi a;
        void* lib = OpenLibrary();
        if (!lib) {
            a.reason = L"failed to load vkey_engine library (dlopen/LoadLibrary)";
            return a;
        }
        a.create = Resolve<decltype(a.create)>(lib, "vkey_engine_create");
        a.destroy = Resolve<decltype(a.destroy)>(lib, "vkey_engine_destroy");
        a.reset = Resolve<decltype(a.reset)>(lib, "vkey_engine_reset");
        a.push_char = Resolve<decltype(a.push_char)>(lib, "vkey_engine_push_char");
        a.backspace = Resolve<decltype(a.backspace)>(lib, "vkey_engine_backspace");
        a.peek_utf16 = Resolve<decltype(a.peek_utf16)>(lib, "vkey_engine_peek_utf16");
        a.commit_utf16 = Resolve<decltype(a.commit_utf16)>(lib, "vkey_engine_commit_utf16");
        a.count = Resolve<decltype(a.count)>(lib, "vkey_engine_count");
        a.abi_version = Resolve<decltype(a.abi_version)>(lib, "vkey_engine_abi_version");
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
        const bool symbolsResolved =
            a.create && a.destroy && a.reset && a.push_char && a.backspace &&
            a.peek_utf16 && a.commit_utf16 && a.count && a.abi_version &&
            a.is_english_word && a.is_tone_escaped && a.has_active_quick_consonant &&
            a.peek_raw_utf16 && a.seed_text_utf16 && a.last_commit_was_corrected &&
            a.set_custom_keymap;
        if (!symbolsResolved) {
            a.reason = L"vkey_engine library is missing a required exported symbol";
            return a;
        }
        const uint32_t libVersion = a.abi_version();
        if (libVersion != VKEY_ENGINE_ABI_VERSION) {
            a.reason = L"vkey_engine ABI version mismatch (lib=" + std::to_wstring(libVersion) +
                        L", expected=" + std::to_wstring(VKEY_ENGINE_ABI_VERSION) + L")";
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

// Encode a wstring as UTF-16 code units for the ABI. On Windows wchar_t is
// already UTF-16; on Linux it is a 32-bit scalar, so widen surrogates here.
// Vietnamese is entirely BMP, so the surrogate branch is defensive only.
std::vector<uint16_t> ToUtf16(const std::wstring& text) {
    std::vector<uint16_t> out;
    out.reserve(text.size());
    for (const wchar_t wc : text) {
        const uint32_t c = static_cast<uint32_t>(wc);
        if (c <= 0xFFFF) {
            out.push_back(static_cast<uint16_t>(c));
        } else if (c <= 0x10FFFF) {
            const uint32_t v = c - 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
        }
    }
    return out;
}

// Bounded engine: the active composition never exceeds this many UTF-16 units.
constexpr size_t kTextCap = 256;

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

bool RustInputEngine::SeedFromText(const std::wstring& text) {
    if (!handle_) {
        Reset();
        return false;
    }
    // Literal restore: the engine reproduces the text so the English-word check
    // and continued typing/backspace work. Re-toning the restored glyphs is not
    // faithfully supported (needs a raw snapshot). On failure the engine is left
    // reset, per the IInputEngine contract.
    const std::vector<uint16_t> units = ToUtf16(text);
    const bool ok =
        Api().seed_text_utf16(static_cast<VKeyEngine*>(handle_), units.data(), units.size());
    Refresh();
    return ok;
}

}  // namespace NextKey
