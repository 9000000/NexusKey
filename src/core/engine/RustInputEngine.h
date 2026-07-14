// VKey - Rust engine adapter
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-VKey-Commercial

#pragma once

#include "IInputEngine.h"
#include "../config/TypingConfig.h"

#include <string>

namespace NextKey {

/// IInputEngine backed by the prebuilt, closed-source `vkey_engine` library,
/// reached through its C ABI (v2) and loaded at runtime (no link-time toolchain
/// matching). The NexusKey pipeline keeps talking to IInputEngine unchanged;
/// this adapter is the only component that knows the Rust engine exists.
///
/// The full IInputEngine surface is wired to the engine: composition loop plus
/// the host query surface (IsEnglishWord, IsToneEscaped, PeekRaw, quick-consonant
/// state). SeedFromText performs a literal restore — enough for the English-word
/// check and continued typing/backspace into a committed word; re-applying a
/// tone/modifier to the restored glyphs is not faithfully supported (that needs
/// a raw snapshot captured at commit time in the engine).
class RustInputEngine : public IInputEngine {
public:
    explicit RustInputEngine(const TypingConfig& config);
    ~RustInputEngine() override;

    RustInputEngine(const RustInputEngine&) = delete;
    RustInputEngine& operator=(const RustInputEngine&) = delete;

    void PushChar(wchar_t c) override;
    void Backspace() override;
    [[nodiscard]] const std::wstring& Peek() const override { return peek_; }
    [[nodiscard]] std::wstring Commit() override;
    void Reset() override;
    [[nodiscard]] size_t Count() const override { return count_; }
    [[nodiscard]] bool HasActiveQuickConsonant() const override;
    [[nodiscard]] bool SeedFromText(const std::wstring& text) override;
    [[nodiscard]] bool IsEnglishWord() const override;
    [[nodiscard]] bool IsToneEscaped() const override;
    [[nodiscard]] std::wstring PeekRaw() const override { return raw_; }
    [[nodiscard]] std::wstring_view PeekRawView() const noexcept override { return raw_; }
    [[nodiscard]] bool LastCommitWasCorrected() const override;

    /// Whether the prebuilt engine library loaded and its ABI version matched.
    /// EngineFactory uses this to fall back to the in-tree C++ engine.
    [[nodiscard]] static bool LibraryAvailable();

    /// Empty when LibraryAvailable(); otherwise a short diagnostic of why the
    /// library didn't load (dlopen failure, missing symbol, or ABI mismatch).
    [[nodiscard]] static std::wstring UnavailableReason();

private:
    void* handle_ = nullptr;  // opaque VKeyEngine*
    std::wstring peek_;
    std::wstring raw_;   // case-preserved physical keys, for PeekRaw/PeekRawView
    size_t count_ = 0;

    void Refresh();
};

}  // namespace NextKey
