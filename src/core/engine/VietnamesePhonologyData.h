// NexusKey - Vietnamese Phonology Shared Data
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-NexusKey-Commercial
//
// Single source of truth for Vietnamese phonological rule data consumed by
// the project's two phonotactics validators:
//   - core/engine/Phonotactics.cpp           (wstring_view path / Path 2)
//   - core/engine/PhonotacticsValidator.cpp  (CharState path / Path 1, hot)
//
// This header is the entry point for the T2.1 phonology consolidation work
// (see docs/TODO.md "Vietnamese-rule consolidation"). Day-1 lifts the front-
// vowel classifier shared by both files. Subsequent days will lift VCPair
// per-nucleus allowed-coda bitmasks, onset agreement matrix, and closed/
// pending vowel sets, then wrap the data behind an `IPhonologyRules` plugin
// contract matching the existing IOutputInjector / ICodeTableConverter
// patterns.
//
// All entries are `constexpr` / `noexcept` — zero runtime cost, no allocation.

#pragma once

namespace NextKey {
namespace Phonology {

// Front vowels for orthographic rules (c/k, g/gh, ng/ngh agreement).
// Modifier marks (ê = e+circumflex, etc.) do not change the front/back class:
// callers pass the *base* (post-Decompose) wchar.
[[nodiscard]] constexpr bool IsFrontBaseVowel(wchar_t base) noexcept {
    return base == L'e' || base == L'i' || base == L'y';
}

}  // namespace Phonology
}  // namespace NextKey
