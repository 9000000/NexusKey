// TomlLoader.h -- parses a TOML corpus file into TestCase structs.
//
// Schema:
//   [[tests]]
//   name           = "ghost-key-toans-bs3"   (required)
//   target_app     = "notepad"               (optional, default "notepad")
//   keys           = "toans\b\b\bi"          (required, raw Telex + escapes)
//   expected       = "toi"                   (required)
//   inter_key_us   = 1000                    (optional, default 10000)
//   budget_p99_us  = 1000                    (optional, default 0 = no budget)
//
// Two entry points:
//   LoadFile(path)  -- read from disk
//   LoadString(s)   -- parse from in-memory string (used by unit tests)
//
// Both return LoadResult: on success `cases` is populated and `error` empty;
// on failure `cases` is empty and `error` describes the problem.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "TestCase.h"

namespace NextKey::TestRunner::TomlLoader {

struct LoadResult {
    std::vector<TestCase> cases;
    std::string error;  // empty on success
};

[[nodiscard]] LoadResult LoadFile(const std::filesystem::path& path);
[[nodiscard]] LoadResult LoadString(std::string_view tomlText);

}  // namespace NextKey::TestRunner::TomlLoader
