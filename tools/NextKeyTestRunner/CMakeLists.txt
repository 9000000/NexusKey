# NextKeyTestRunner — E2E stress test harness (Windows-only)
# Excluded from ALL build — invoke explicitly:
#   cmake --build build --target NextKeyTestRunner --config Debug

if(NOT WIN32)
    return()
endif()

add_executable(NextKeyTestRunner EXCLUDE_FROM_ALL
    src/main.cpp
)

target_include_directories(NextKeyTestRunner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
