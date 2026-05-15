# VKeyTestRunner — E2E stress test harness for VKey IME.
# Two targets:
#   - VKeyTestRunnerTests : cross-platform GTest suite for parser/encoder logic
#   - VKeyTestRunner      : Windows-only console runner driving SendInput
# Both are EXCLUDE_FROM_ALL — invoke explicitly:
#   cmake --build <dir> --target VKeyTestRunnerTests
#   cmake --build <dir> --target VKeyTestRunner --config Debug

# ---------------------------------------------------------------------------
# Tests (cross-platform — Linux + Windows). Includes Telex port, key escape
# parser, and TOML loader. None of these depend on Win32 APIs.
# ---------------------------------------------------------------------------
add_executable(VKeyTestRunnerTests EXCLUDE_FROM_ALL
    src/CaseResult.h
    src/CliConvert.h
    src/EditDistance.h
    src/HookLogParser.h
    src/HookLogParser.cpp
    src/JunitXmlWriter.h
    src/JunitXmlWriter.cpp
    src/KeyEscapes.h
    src/KeyEscapes.cpp
    src/PerfCsvWriter.h
    src/PerfCsvWriter.cpp
    src/TestCase.h
    src/TomlLoader.h
    src/TomlLoader.cpp
    tests/CliConvertTest.cpp
    tests/EditDistanceTest.cpp
    tests/HookLogParserTest.cpp
    tests/JunitXmlWriterTest.cpp
    tests/KeyEscapesTest.cpp
    tests/PerfCsvWriterTest.cpp
    tests/TelexTest.cpp
    tests/TomlLoaderTest.cpp
)

target_include_directories(VKeyTestRunnerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/extern/tomlplusplus   # toml.hpp
)

target_link_libraries(VKeyTestRunnerTests PRIVATE GTest::gtest_main)

set_target_properties(VKeyTestRunnerTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

if(MSVC)
    target_link_options(VKeyTestRunnerTests PRIVATE /SUBSYSTEM:CONSOLE)
endif()

include(GoogleTest)
gtest_discover_tests(VKeyTestRunnerTests)

# ---------------------------------------------------------------------------
# Main runner — Windows-only (uses SendInput, clipboard, foreground HWND)
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(VKeyTestRunner EXCLUDE_FROM_ALL
        src/main.cpp
        src/CaseResult.h
        src/CliConvert.h
        src/ClipboardReader.h
        src/ClipboardReader.cpp
        src/EditDistance.h
        src/Encoding.h
        src/HookLogParser.h
        src/HookLogParser.cpp
        src/JunitXmlWriter.h
        src/JunitXmlWriter.cpp
        src/KeyEscapes.h
        src/KeyEscapes.cpp
        src/PerfCsvWriter.h
        src/PerfCsvWriter.cpp
        src/SendInputDriver.h
        src/SendInputDriver.cpp
        src/TestCase.h
        src/Telex.h
        src/TomlLoader.h
        src/TomlLoader.cpp
    )

    target_include_directories(VKeyTestRunner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern/tomlplusplus    # toml.hpp
    )

    # Win32 libs needed downstream (D3+: SendInput, clipboard, foreground HWND, hi-res timer)
    target_link_libraries(VKeyTestRunner PRIVATE
        user32
        winmm
    )

    set_target_properties(VKeyTestRunner PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    )

    if(MSVC)
        target_link_options(VKeyTestRunner PRIVATE /SUBSYSTEM:CONSOLE)
    endif()
endif()
