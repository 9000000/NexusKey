#include "TomlLoader.h"

#include <fstream>
#include <sstream>

#include <toml.hpp>

#include "Encoding.h"
#include "KeyEscapes.h"

namespace NextKey::TestRunner::TomlLoader {

namespace {

// Parses a single [[tests]] table. On failure, `error` is set and returns
// nullopt; caller aborts the whole file load.
struct ParseOneResult {
    std::optional<TestCase> testCase;
    std::string error;
};

ParseOneResult ParseOneTest(const toml::table& tbl, std::size_t index) {
    ParseOneResult r;
    TestCase tc;

    auto nameOpt = tbl["name"].value<std::string>();
    if (!nameOpt || nameOpt->empty()) {
        std::ostringstream os;
        os << "[[tests]] #" << index << " missing required field 'name'";
        r.error = os.str();
        return r;
    }
    tc.name = *nameOpt;

    tc.targetApp = tbl["target_app"].value_or<std::string>("notepad");

    auto keysOpt = tbl["keys"].value<std::string>();
    if (!keysOpt) {
        r.error = "test '" + tc.name + "' missing required field 'keys'";
        return r;
    }
    auto resolved = KeyEscapes::Resolve(Encoding::Utf8ToUtf16(*keysOpt));
    if (!resolved) {
        r.error = "test '" + tc.name + "' has invalid escape in 'keys'";
        return r;
    }
    tc.keys = std::move(*resolved);

    auto expectedOpt = tbl["expected"].value<std::string>();
    if (!expectedOpt) {
        r.error = "test '" + tc.name + "' missing required field 'expected'";
        return r;
    }
    tc.expected = Encoding::Utf8ToUtf16(*expectedOpt);

    if (auto v = tbl["inter_key_us"].value<int64_t>(); v && *v >= 0) {
        tc.interKeyMicros = static_cast<uint32_t>(*v);
    }
    if (auto v = tbl["budget_p99_us"].value<int64_t>(); v && *v >= 0) {
        tc.budgetP99Micros = static_cast<uint32_t>(*v);
    }

    r.testCase = std::move(tc);
    return r;
}

LoadResult LoadFromTable(const toml::table& root) {
    LoadResult result;

    const auto* arr = root["tests"].as_array();
    if (!arr || arr->empty()) {
        result.error = "missing or empty [[tests]] array";
        return result;
    }

    std::size_t index = 0;
    for (const auto& node : *arr) {
        const auto* tbl = node.as_table();
        if (!tbl) {
            std::ostringstream os;
            os << "[[tests]] #" << index << " is not a table";
            result.error = os.str();
            result.cases.clear();
            return result;
        }
        auto one = ParseOneTest(*tbl, index);
        if (!one.error.empty()) {
            result.error = std::move(one.error);
            result.cases.clear();
            return result;
        }
        result.cases.push_back(std::move(*one.testCase));
        ++index;
    }
    return result;
}

}  // namespace

LoadResult LoadFile(const std::filesystem::path& path) {
    LoadResult result;
    try {
        const auto root = toml::parse_file(path.string());
        return LoadFromTable(root);
    } catch (const toml::parse_error& e) {
        std::ostringstream os;
        os << "TOML parse error in " << path << ": " << e.description();
        result.error = os.str();
        return result;
    } catch (const std::exception& e) {
        result.error = std::string("Load error: ") + e.what();
        return result;
    }
}

LoadResult LoadString(std::string_view tomlText) {
    LoadResult result;
    try {
        const auto root = toml::parse(tomlText);
        return LoadFromTable(root);
    } catch (const toml::parse_error& e) {
        result.error = std::string("TOML parse error: ") + e.description().data();
        return result;
    } catch (const std::exception& e) {
        result.error = std::string("Load error: ") + e.what();
        return result;
    }
}

}  // namespace NextKey::TestRunner::TomlLoader
