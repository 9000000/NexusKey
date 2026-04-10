// NexusKey - Subprocess Runner Functions
// SPDX-License-Identifier: GPL-3.0-only
//
// Each Run*Subprocess() is [[noreturn]] — called from command-line routing
// in wWinMain, runs a Sciter dialog, then ExitProcess(0).

#pragma once

namespace NextKey {

[[noreturn]] void RunSettingsSubprocess();
[[noreturn]] void RunExcludedAppsSubprocess();
[[noreturn]] void RunTsfAppsSubprocess();
[[noreturn]] void RunMacroSubprocess();
[[noreturn]] void RunConvertToolSubprocess();
[[noreturn]] void RunAboutSubprocess();
[[noreturn]] void RunAppOverridesSubprocess();
[[noreturn]] void RunSpellExclusionsSubprocess();

}  // namespace NextKey
