// VKey - Rust engine adapter
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "IInputEngine.h"
#include "../config/TypingConfig.h"

#include <string>

namespace NextKey {

/// IInputEngine adapter for the trusted, runtime-loaded vkey_engine C ABI.
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

    /// Whether the library passed artifact, ABI and runtime-identity checks.
    [[nodiscard]] static bool LibraryAvailable();

    /// Empty when LibraryAvailable(); otherwise a bounded diagnostic.
    [[nodiscard]] static std::wstring UnavailableReason();

private:
    void* handle_ = nullptr;  // opaque VKeyEngine*
    std::wstring peek_;
    std::wstring raw_;   // case-preserved physical keys, for PeekRaw/PeekRawView
    size_t count_ = 0;

    void Refresh();
};

}  // namespace NextKey
