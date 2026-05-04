# NextKeyTestRunner — E2E stress test harness for NexusKey IME.
# Two targets:
#   - NextKeyTestRunnerTests : cross-platform GTest suite for parser/encoder logic
#   - NextKeyTestRunner      : Windows-only console runner driving SendInput
# Both are EXCLUDE_FROM_ALL — invoke explicitly:
#   cmake --build <dir> --target NextKeyTestRunnerTests
#   cmake --build <dir> --target NextKeyTestRunner --config Debug

# ---------------------------------------------------------------------------
# Tests (cross-platform — Linux + Windows). Includes Telex port, key escape
# parser, and TOML loader. None of these depend on Win32 APIs.
# ---------------------------------------------------------------------------
add_executable(NextKeyTestRunnerTests EXCLUDE_FROM_ALL
    src/KeyEscapes.h
    src/KeyEscapes.cpp
    src/TomlLoader.h
    src/TomlLoader.cpp
    tests/KeyEscapesTest.cpp
    tests/TelexTest.cpp
    tests/TomlLoaderTest.cpp
)

target_include_directories(NextKeyTestRunnerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/extern/tomlplusplus   # toml.hpp
)

target_link_libraries(NextKeyTestRunnerTests PRIVATE GTest::gtest_main)

set_target_properties(NextKeyTestRunnerTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

if(MSVC)
    target_link_options(NextKeyTestRunnerTests PRIVATE /SUBSYSTEM:CONSOLE)
endif()

include(GoogleTest)
gtest_discover_tests(NextKeyTestRunnerTests)

# ---------------------------------------------------------------------------
# Main runner — Windows-only (uses SendInput, clipboard, foreground HWND)
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(NextKeyTestRunner EXCLUDE_FROM_ALL
        src/main.cpp
        src/ClipboardReader.h
        src/ClipboardReader.cpp
        src/Encoding.h
        src/KeyEscapes.h
        src/KeyEscapes.cpp
        src/SendInputDriver.h
        src/SendInputDriver.cpp
        src/TestCase.h
        src/Telex.h
        src/TomlLoader.h
        src/TomlLoader.cpp
    )

    target_include_directories(NextKeyTestRunner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern/tomlplusplus    # toml.hpp
    )

    # Win32 libs needed downstream (D3+: SendInput, clipboard, foreground HWND, hi-res timer)
    target_link_libraries(NextKeyTestRunner PRIVATE
        user32
        winmm
    )

    set_target_properties(NextKeyTestRunner PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
    )

    if(MSVC)
        target_link_options(NextKeyTestRunner PRIVATE /SUBSYSTEM:CONSOLE)
    endif()
endif()
