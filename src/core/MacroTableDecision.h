// VKey - Macro Table Refresh Decision
// Copyright (c) 2024-2026 PhatMT. All rights reserved.
// SPDX-License-Identifier: AGPL-3.0-only
//
// Pure decision: given the macro table's load state and the generation the
// EXE published in SharedState, decide whether the TSF DLL should re-read the
// table from TOML, keep serving what it already has, or do nothing.
//
// Consumer: `EngineController::ApplySharedState` / `CheckConfigEvent`
// (Win32/TSF-only). The rule lives here so it has Linux GTest coverage.
//
// Background (#227 / #231 / #209): the key path used to DROP the table on a
// generation bump, leaving the DLL with no macros until the next
// OnSetFocus(TRUE). Chromium hosts fire OnSetFocus exactly once, at activation
// — measured in the reporter's own logs: 1 focus event against 9 settings saves
// across a 30-minute session — so every macro stayed dead for the rest of that
// host process's life after any settings save.
//
// Waiting for focus is therefore not a recovery path at all in those hosts, so
// the key path is allowed one TOML read per generation. It is bounded by
// construction: a successful read latches (loaded, gen), after which every
// later keystroke at that generation decides `None` and touches no file. The
// read side of ConfigManager takes no cross-process lock (`ConfigFileLock`
// guards only the Save* functions) and `TomlFileCache` keys on mtime, so the
// real cost is one attribute stat plus one parse of a small file per settings
// save — cheaper than the sync COM edit session the TSF key path already runs
// on every keystroke.
//
// `KeepStale` remains for the one case worth backing off on: the read already
// failed at this generation. Retrying it on every keystroke would stat the file
// forever, so the key path keeps serving the table it has and leaves the retry
// to focus/init or the next real config change. Serving a stale table always
// beats serving none.

#pragma once

#include <cstdint>

namespace NextKey {

/// What to do with the in-memory macro table on this tick.
enum class MacroTableAction : uint8_t {
    None,       // already current for this generation — leave it alone
    Reload,     // re-read the table from TOML now
    KeepStale,  // read already failed at this generation from the key path —
                // keep serving the current table, retry at focus/init
};

struct MacroTableInputs {
    bool loaded{false};        // a successful disk read has been latched
    uint8_t loadedGen{0};      // generation that latched table came from
    uint8_t stateGen{0};       // SharedState::configGeneration (live)
    bool allowDiskRead{false}; // caller runs outside the typing path
};

/// A table is current only when a successful read is latched AND it came from
/// the generation the EXE is currently publishing.
///
/// `allowDiskRead` no longer gates the read itself — it only decides whether a
/// generation whose read ALREADY failed is worth re-attempting. That keeps
/// key-path I/O at one attempt per generation while focus/init stays free to
/// keep retrying a config it has never managed to read.
[[nodiscard]] constexpr MacroTableAction DecideMacroTable(
    const MacroTableInputs& in) noexcept {
    if (in.loaded && in.loadedGen == in.stateGen) return MacroTableAction::None;
    if (in.allowDiskRead) return MacroTableAction::Reload;
    // Key path: read once when the generation actually moves. Same generation
    // with nothing latched means the attempt already happened and failed.
    return in.loadedGen != in.stateGen ? MacroTableAction::Reload
                                       : MacroTableAction::KeepStale;
}

}  // namespace NextKey
