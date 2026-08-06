// VKey - Vietnamese tone-placement rules
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <string_view>

namespace NextKey::Phonology {

/// Returns the index in a rendered Vietnamese vowel nucleus where the tone
/// mark belongs. Returns SIZE_MAX when the sequence contains no vowel.
[[nodiscard]] std::size_t FindTonePosition(
    std::wstring_view vowelSeq,
    std::wstring_view coda,
    bool modernOrtho) noexcept;

}  // namespace NextKey::Phonology
